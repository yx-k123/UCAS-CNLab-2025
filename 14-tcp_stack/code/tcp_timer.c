#include "include/tcp.h"
#include "include/tcp_timer.h"
#include "include/tcp_sock.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>

static struct list_head timer_list;

long long get_now_us() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (long long)tv.tv_sec * 1000000 + tv.tv_usec;
}

// scan the timer_list, find the tcp sock which stays for at 2*MSL, release it
void tcp_scan_timer_list()
{
	// fprintf(stdout, "TODO: implement %s please.\n", __FUNCTION__);
	struct tcp_timer *entry, *ptr;
	long long cur_time = get_now_us();
	list_for_each_entry_safe(entry, ptr, &timer_list, list) {
		if(entry->enable == 1) {
			if (entry->type == 0) { // TIME_WAIT
				if (cur_time - entry->timeout > TCP_TIMEWAIT_TIMEOUT) {
					struct tcp_sock * tsk = timewait_to_tcp_sock(entry);
					list_delete_entry(&entry->list);
					tcp_set_state(tsk,TCP_CLOSED);
					tcp_unhash(tsk);
					tcp_bind_unhash(tsk);
				}
			} else if (entry->type == 1) { // RETRANS
				struct tcp_sock *tsk = retranstimer_to_tcp_sock(entry);
				pthread_mutex_lock(&tsk->send_buf_lock);
				if (list_empty(&tsk->send_buf)) {
					pthread_mutex_unlock(&tsk->send_buf_lock);
					tcp_unset_retrans_timer(tsk);
					continue;
				}
				struct data_packet *dp = list_entry(tsk->send_buf.next, struct data_packet, list);
				long long duration = TCP_RETRANS_INTERVAL_INITIAL * (1 << dp->times);
				if (cur_time - entry->timeout > duration) {
					if (dp->times >= 3) {
						pthread_mutex_unlock(&tsk->send_buf_lock);
						tcp_send_control_packet(tsk, TCP_RST);
						tcp_set_state(tsk, TCP_CLOSED);
						tcp_unhash(tsk);
						tcp_bind_unhash(tsk);
						list_delete_entry(&entry->list);
						entry->enable = 0;
					} else {
						dp->times++;
						entry->timeout = cur_time;
						tcp_send_retrans_packet(tsk, dp);
						pthread_mutex_unlock(&tsk->send_buf_lock);
					}
				} else {
					pthread_mutex_unlock(&tsk->send_buf_lock);
				}
			}
		}
	}
}

// set the timewait timer of a tcp sock, by adding the timer into timer_list
void tcp_set_timewait_timer(struct tcp_sock *tsk)
{
	// fprintf(stdout, "TODO: implement %s please.\n", __FUNCTION__);
	tsk->timewait.enable = 1;
	tsk->timewait.type = 0;
	tsk->timewait.timeout = get_now_us();
	list_add_tail(&tsk->timewait.list, &timer_list);
}

void tcp_set_retrans_timer(struct tcp_sock *tsk)
{
	if(tsk->retrans_timer.enable) return;
	tsk->retrans_timer.type = 1;
	tsk->retrans_timer.timeout = get_now_us();
	tsk->retrans_timer.enable = 1;
	list_add_tail(&tsk->retrans_timer.list, &timer_list);
}

void tcp_unset_retrans_timer(struct tcp_sock *tsk)
{
	if(tsk->retrans_timer.enable) {
		list_delete_entry(&tsk->retrans_timer.list);
		tsk->retrans_timer.enable = 0;
	}
}

// scan the timer_list periodically by calling tcp_scan_timer_list
void *tcp_timer_thread(void *arg)
{
	init_list_head(&timer_list);
	while (1) {
		usleep(TCP_TIMER_SCAN_INTERVAL);
		tcp_scan_timer_list();
	}

	return NULL;
}
