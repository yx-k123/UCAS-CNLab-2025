#include "include/tcp.h"
#include "include/tcp_sock.h"
#include "include/tcp_timer.h"

#include "include/log.h"
#include "include/ring_buffer.h"

#include <stdlib.h>

#ifndef max
#   define max(x,y) ((x)>(y) ? (x) : (y))
#endif
#ifndef min
#   define min(x,y) ((x)<(y) ? (x) : (y))
#endif
#define TCP_MSS 1460

#ifndef list_for_each
#define list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)
#endif
// update the snd_wnd of tcp_sock
//
// if the snd_wnd before updating is zero, notify tcp_sock_send (wait_send)
static inline void tcp_update_window(struct tcp_sock *tsk, struct tcp_cb *cb)
{
	u16 old_snd_wnd = tsk->snd_wnd;
	tsk->snd_wnd = cb->rwnd;
	if (old_snd_wnd == 0)
		wake_up(tsk->wait_send);
}

// update the snd_wnd safely: cb->ack should be between snd_una and snd_nxt
static inline void tcp_update_window_safe(struct tcp_sock *tsk, struct tcp_cb *cb)
{
	if (less_or_equal_32b(tsk->snd_una, cb->ack) && less_or_equal_32b(cb->ack, tsk->snd_nxt))
		tcp_update_window(tsk, cb);
}

#ifndef max
#	define max(x,y) ((x)>(y) ? (x) : (y))
#endif

// check whether the sequence number of the incoming packet is in the receiving
// window
static inline int is_tcp_seq_valid(struct tcp_sock *tsk, struct tcp_cb *cb)
{
	u32 rcv_end = tsk->rcv_nxt + max(tsk->rcv_wnd, 1);
	if (less_than_32b(cb->seq, rcv_end) && less_or_equal_32b(tsk->rcv_nxt, cb->seq_end)) {
		return 1;
	}
	else {
		log(ERROR, "received packet with invalid seq, drop it.");
		return 0;
	}
}

void tcp_rcv_ofo_pkt(struct tcp_sock *tsk, struct tcp_cb *cb)
{
	struct data_packet *dp = new_data_block(cb->flags, cb->seq, cb->pl_len, cb->payload);
	struct data_packet *tmp;
	struct list_head *pos = &tsk->rcv_ofo_buf;
	
	list_for_each(pos, &tsk->rcv_ofo_buf) {
		tmp = list_entry(pos, struct data_packet, list);
		if (less_than_32b(dp->seq, tmp->seq)) {
			break;
		}
	}
	list_add_tail(&dp->list, pos);
}

// Process the incoming packet according to TCP state machine. 
void tcp_process(struct tcp_sock *tsk, struct tcp_cb *cb, char *packet)
{
	// fprintf(stdout, "TODO: implement %s please.\n", __FUNCTION__);
	struct tcphdr * tcp_head = packet_to_tcp_hdr(packet);
	if(cb->flags & TCP_ACK){
		if (tsk->state == TCP_ESTABLISHED) {
			if (tsk->snd_una == cb->ack && cb->pl_len == 0 && !(cb->flags & (TCP_SYN | TCP_FIN))) {
				tsk->dupacks++;
				if (tsk->congestion_state == TCP_OPEN) {
					if (tsk->dupacks == 3) {
						tsk->ssthresh = max(tsk->cwnd / 2, 2);
						tsk->cwnd = tsk->ssthresh + 3;
						tsk->recovery_point = tsk->snd_nxt;
						tsk->congestion_state = TCP_RECOVERY;
						tcp_log_cwnd(tsk);
						if (!list_empty(&tsk->send_buf)) {
							struct data_packet *dp = list_entry(tsk->send_buf.next, struct data_packet, list);
							tcp_send_retrans_packet(tsk, dp);
						}
					} else {
						tsk->congestion_state = TCP_DISORDER;
					}
				} else if (tsk->congestion_state == TCP_DISORDER) {
					if (tsk->dupacks == 3) {
						tsk->ssthresh = max(tsk->cwnd / 2, 2);
						tsk->cwnd = tsk->ssthresh + 3;
						tsk->recovery_point = tsk->snd_nxt;
						tsk->congestion_state = TCP_RECOVERY;
						tcp_log_cwnd(tsk);
						if (!list_empty(&tsk->send_buf)) {
							struct data_packet *dp = list_entry(tsk->send_buf.next, struct data_packet, list);
							tcp_send_retrans_packet(tsk, dp);
						}
					}
				} else if (tsk->congestion_state == TCP_RECOVERY) {
					tsk->cwnd++;
					tcp_log_cwnd(tsk);
				}
			} else {
				if (tsk->congestion_state == TCP_RECOVERY) {
					if (less_than_32b(cb->ack, tsk->recovery_point)) {
						struct data_packet *dp;
						list_for_each_entry(dp, &tsk->send_buf, list) {
							if (dp->seq == cb->ack) {
								tcp_send_retrans_packet(tsk, dp);
								break;
							}
						}
					} else {
						tsk->congestion_state = TCP_OPEN;
						tsk->cwnd = tsk->ssthresh;
						tsk->dupacks = 0;
						tcp_log_cwnd(tsk);
					}
				} else {
					tsk->dupacks = 0;
					tsk->congestion_state = TCP_OPEN;
					if (tsk->cwnd * TCP_MSS < tsk->ssthresh) {
						tsk->cwnd++;
						tcp_log_cwnd(tsk);
					} else {
						tsk->cwnd_cnt++;
						if (tsk->cwnd_cnt >= tsk->cwnd) {
							tsk->cwnd++;
							tsk->cwnd_cnt = 0;
							tcp_log_cwnd(tsk);
						}
					}
				}
			}
		}

		tsk->snd_una = cb->ack;
		tcp_free_send_buf(tsk, cb);
	}

	tsk->adv_wnd = cb->rwnd;
	if (tsk->cwnd == 0) tsk->cwnd = 1;
	tsk->snd_wnd = min(tsk->adv_wnd, tsk->cwnd * TCP_MSS);
	wake_up(tsk->wait_send);

	if(tsk->state == TCP_LISTEN){
		if(tcp_head->flags & TCP_SYN){
			struct tcp_sock *child_tsk = alloc_tcp_sock();
			// Fix for double free and memory leak due to memcpy
            struct ring_buffer *rb = child_tsk->rcv_buf;
            struct synch_wait *wc = child_tsk->wait_connect;
            struct synch_wait *wa = child_tsk->wait_accept;
            struct synch_wait *wr = child_tsk->wait_recv;
            struct synch_wait *ws = child_tsk->wait_send;
            pthread_mutex_t sbl = child_tsk->send_buf_lock;
            pthread_mutex_t rbl = child_tsk->rcv_buf_lock;

			memcpy(child_tsk, tsk, sizeof(struct tcp_sock));

            child_tsk->rcv_buf = rb;
            child_tsk->wait_connect = wc;
            child_tsk->wait_accept = wa;
            child_tsk->wait_recv = wr;
            child_tsk->wait_send = ws;
            child_tsk->send_buf_lock = sbl;
            child_tsk->rcv_buf_lock = rbl;

            init_list_head(&child_tsk->list);
            init_list_head(&child_tsk->listen_queue);
            init_list_head(&child_tsk->accept_queue);
            init_list_head(&child_tsk->send_buf);
            init_list_head(&child_tsk->rcv_ofo_buf);
            memset(&child_tsk->retrans_timer, 0, sizeof(struct tcp_timer));
            memset(&child_tsk->timewait, 0, sizeof(struct tcp_timer));
            // End fix

			child_tsk->parent = tsk;
			child_tsk->sk_sip = cb->daddr;
			child_tsk->sk_sport = cb->dport;
			child_tsk->sk_dip = cb->saddr;
			child_tsk->sk_dport = cb->sport;
			child_tsk->snd_nxt = child_tsk->iss = tcp_new_iss();
			child_tsk->rcv_nxt = cb->seq + 1;

			list_add_tail(&child_tsk->list, &child_tsk->parent->listen_queue);

			struct data_packet *dp = new_data_block(TCP_SYN|TCP_ACK, child_tsk->snd_nxt, 0, NULL);
			pthread_mutex_lock(&child_tsk->send_buf_lock);
			list_add_tail(&dp->list, &child_tsk->send_buf);
			pthread_mutex_unlock(&child_tsk->send_buf_lock);

			tcp_send_control_packet(child_tsk, TCP_SYN|TCP_ACK);

			if(!child_tsk->retrans_timer.enable)
				tcp_set_retrans_timer(child_tsk);

			tcp_set_state(child_tsk, TCP_SYN_RECV);
			tcp_hash(child_tsk);
		}
		return;
	}else if(tsk->state == TCP_SYN_SENT){
		if(tcp_head->flags & (TCP_ACK | TCP_SYN)){
			wake_up(tsk->wait_connect);
			tsk->rcv_nxt = cb->seq + 1;
			tcp_send_control_packet(tsk, TCP_ACK);
			tcp_set_state(tsk, TCP_ESTABLISHED);
		}
		return;
	}else if(tsk->state == TCP_SYN_RECV){
		if(tcp_head->flags & TCP_ACK){
			if(tcp_sock_accept_queue_full(tsk->parent)){
				return;
			}
			tcp_sock_accept_enqueue(tsk);
			wake_up(tsk->parent->wait_accept);
			tcp_set_state(tsk, TCP_ESTABLISHED);
		}
		return;
	}
	if(is_tcp_seq_valid(tsk,cb) == 0){
		tcp_send_control_packet(tsk, TCP_ACK);
        return;
    }

	if (less_than_32b(cb->seq, tsk->rcv_nxt)) {
		int offset = tsk->rcv_nxt - cb->seq;
		if (offset < cb->pl_len) {
			cb->payload += offset;
			cb->pl_len -= offset;
			cb->seq += offset;
		} else {
			cb->pl_len = 0;
			// If it was a FIN packet and we already passed the seq, FIN should be ignored?
			// But is_tcp_seq_valid checks seq_end.
			// If FIN is set, seq_end = seq + pl_len + 1.
			// If seq_end > rcv_nxt, and seq < rcv_nxt.
			// If pl_len == 0, then seq_end = seq + 1.
			// If seq < rcv_nxt, then seq + 1 <= rcv_nxt.
			// So it would be invalid.
			// So if we are here, and pl_len becomes 0, it implies FIN is the only thing left?
			// Or we trimmed everything?
		}
	}

	if ((cb->pl_len > 0 || (cb->flags & TCP_FIN)) && tsk->rcv_nxt != cb->seq) {
		tcp_rcv_ofo_pkt(tsk, cb);
		tcp_send_control_packet(tsk, TCP_ACK);
		return;
	}

    if(tsk->state == TCP_ESTABLISHED){
        if (cb->pl_len > 0 || (cb->flags & TCP_FIN)) {
			if (cb->pl_len > 0) {
				pthread_mutex_lock(&tsk->rcv_buf_lock);
				if (ring_buffer_free(tsk->rcv_buf) < cb->pl_len) {
					pthread_mutex_unlock(&tsk->rcv_buf_lock);
					tcp_send_control_packet(tsk, TCP_ACK);
					return;
				}
				write_ring_buffer(tsk->rcv_buf, cb->payload, cb->pl_len);
				pthread_mutex_unlock(&tsk->rcv_buf_lock);
				tsk->rcv_nxt += cb->pl_len;
			}
			
			if (cb->flags & TCP_FIN) {
				tsk->rcv_nxt += 1;
				tcp_set_state(tsk, TCP_CLOSE_WAIT);
			}

			struct data_packet *tmp, *q;
			list_for_each_entry_safe(tmp, q, &tsk->rcv_ofo_buf, list) {
				if(tmp->seq == tsk->rcv_nxt) {
					if (tmp->len > 0) {
						pthread_mutex_lock(&tsk->rcv_buf_lock);
						if (ring_buffer_free(tsk->rcv_buf) < tmp->len) {
							pthread_mutex_unlock(&tsk->rcv_buf_lock);
							break;
						}
						write_ring_buffer(tsk->rcv_buf, tmp->packet, tmp->len);
						pthread_mutex_unlock(&tsk->rcv_buf_lock);
						wake_up(tsk->wait_recv);
					}
					
					tsk->rcv_nxt = tmp->seq_end;
					if(tmp->flags & TCP_FIN) {
						tcp_set_state(tsk, TCP_CLOSE_WAIT);
					}
					list_delete_entry(&tmp->list);
					if(tmp->packet) free(tmp->packet);
					free(tmp);
				} else {
					break;
				}
			}

			tsk->rcv_wnd = ring_buffer_free(tsk->rcv_buf);

			tcp_send_control_packet(tsk, TCP_ACK);
			wake_up(tsk->wait_recv);
        }
    }else if(tsk->state == TCP_LAST_ACK){
		if(tcp_head->flags & TCP_ACK){
			tcp_set_state(tsk, TCP_CLOSED);
			tcp_unhash(tsk);
			tcp_bind_unhash(tsk);
		}
	}else if(tsk->state == TCP_FIN_WAIT_1){
		if(tcp_head->flags & TCP_ACK){
			if(cb->flags & TCP_FIN){
				tsk->rcv_nxt = cb->seq + 1;
				tcp_send_control_packet(tsk, TCP_ACK);
				tcp_set_state(tsk, TCP_TIME_WAIT);
				tcp_set_timewait_timer(tsk);
			}else{
				tcp_set_state(tsk, TCP_FIN_WAIT_2);
			}
		}
	}else if(tsk->state == TCP_FIN_WAIT_2){
		if(tcp_head->flags & TCP_FIN){
			tsk->rcv_nxt = cb->seq + 1;
			tcp_send_control_packet(tsk, TCP_ACK);
			tcp_set_state(tsk, TCP_TIME_WAIT);
			tcp_set_timewait_timer(tsk);
		}
	}
}
