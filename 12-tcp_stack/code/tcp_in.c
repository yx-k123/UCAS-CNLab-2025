#include "include/tcp.h"
#include "include/tcp_sock.h"
#include "include/tcp_timer.h"

#include "include/log.h"
#include "include/ring_buffer.h"

#include <stdlib.h>
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

// Process the incoming packet according to TCP state machine. 
void tcp_process(struct tcp_sock *tsk, struct tcp_cb *cb, char *packet)
{
	// fprintf(stdout, "TODO: implement %s please.\n", __FUNCTION__);
	struct tcphdr * tcp_head = packet_to_tcp_hdr(packet);
	if(cb->flags & TCP_ACK){
		tsk->snd_una = cb->ack;
	}
	tsk->snd_wnd = cb->rwnd;
	wake_up(tsk->wait_send);

	if(tsk->state == TCP_LISTEN){
		if(tcp_head->flags & TCP_SYN){
			struct tcp_sock *child_tsk = alloc_tcp_sock();
			memcpy(child_tsk, tsk, sizeof(struct tcp_sock));
			child_tsk->parent = tsk;
			child_tsk->sk_sip = cb->daddr;
			child_tsk->sk_sport = cb->dport;
			child_tsk->sk_dip = cb->saddr;
			child_tsk->sk_dport = cb->sport;
			child_tsk->snd_nxt = child_tsk->iss = tcp_new_iss();
			child_tsk->rcv_nxt = cb->seq + 1;

			list_add_tail(&child_tsk->list, &child_tsk->parent->listen_queue);

			tcp_send_control_packet(child_tsk, TCP_SYN|TCP_ACK);
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
        return;
    }
    if(tsk->state == TCP_ESTABLISHED){
        if (cb->pl_len > 0 || (cb->flags & TCP_FIN)) {
            if (cb->seq == tsk->rcv_nxt) {
                if (cb->pl_len > 0) {
                    write_ring_buffer(tsk->rcv_buf, cb->payload, cb->pl_len);
                    tsk->rcv_nxt += cb->pl_len;
                }
                
                if (cb->flags & TCP_FIN) {
                    tsk->rcv_nxt += 1;
                    tcp_set_state(tsk, TCP_CLOSE_WAIT);
                }

                tsk->rcv_wnd = ring_buffer_free(tsk->rcv_buf);

                tcp_send_control_packet(tsk, TCP_ACK);
                wake_up(tsk->wait_recv);
            } else {
                tcp_send_control_packet(tsk, TCP_ACK);
            }
        }
    }else if(tsk->state == TCP_LAST_ACK){
		if(tcp_head->flags & TCP_ACK){
			tcp_set_state(tsk, TCP_CLOSED);
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
