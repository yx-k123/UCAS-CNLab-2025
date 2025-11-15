#include "include/arpcache.h"
#include "include/arp.h"
#include "include/ether.h"
#include "include/icmp.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

static arpcache_t arpcache;

// initialize IP->mac mapping, request list, lock and sweeping thread
void arpcache_init()
{
	bzero(&arpcache, sizeof(arpcache_t));

	init_list_head(&(arpcache.req_list));

	pthread_mutex_init(&arpcache.lock, NULL);

	pthread_create(&arpcache.thread, NULL, arpcache_sweep, NULL);
}

// release all the resources when exiting
void arpcache_destroy()
{
	pthread_mutex_lock(&arpcache.lock);

	struct arp_req *req_entry = NULL, *req_q;
	list_for_each_entry_safe(req_entry, req_q, &(arpcache.req_list), list) {
		struct cached_pkt *pkt_entry = NULL, *pkt_q;
		list_for_each_entry_safe(pkt_entry, pkt_q, &(req_entry->cached_packets), list) {
			list_delete_entry(&(pkt_entry->list));
			free(pkt_entry->packet);
			free(pkt_entry);
		}

		list_delete_entry(&(req_entry->list));
		free(req_entry);
	}

	pthread_kill(arpcache.thread, SIGTERM);

	pthread_mutex_unlock(&arpcache.lock);
}

// lookup the IP->mac mapping
//
// traverse the table to find whether there is an entry with the same IP
// and mac address with the given arguments
int arpcache_lookup(u32 ip4, u8 mac[ETH_ALEN])
{
	// fprintf(stderr, "TODO: lookup ip address in arp cache.\n");
	pthread_mutex_lock(&arpcache.lock);
	for (int i = 0; i < MAX_ARP_SIZE; i++) {
		if (arpcache.entries[i].valid && arpcache.entries[i].ip4 == ip4) {
			memcpy(mac, arpcache.entries[i].mac, ETH_ALEN);
			pthread_mutex_unlock(&arpcache.lock);
			return 1;
		}
	}

	pthread_mutex_unlock(&arpcache.lock);	
	return 0;
}

// append the packet to arpcache
//
// Lookup in the list which stores pending packets, if there is already an
// entry with the same IP address and iface (which means the corresponding arp
// request has been sent out), just append this packet at the tail of that entry
// (the entry may contain more than one packet); otherwise, malloc a new entry
// with the given IP address and iface, append the packet, and send arp request.
void arpcache_append_packet(iface_info_t *iface, u32 ip4, char *packet, int len)
{
	// fprintf(stderr, "TODO: append the ip address if lookup failed, and send arp request if necessary.\n");
	pthread_mutex_lock(&arpcache.lock);
	struct arp_req *req_entry = NULL;
	list_for_each_entry(req_entry, &arpcache.req_list, list) {
		if (req_entry->ip4 == ip4 && req_entry->iface == iface) {
			struct cached_pkt *new_pkt = (struct cached_pkt *)malloc(sizeof(struct cached_pkt));
			new_pkt->packet = (char *)malloc(len);
			memcpy(new_pkt->packet, packet, len);
			new_pkt->len = len;
			list_add_tail(&new_pkt->list, &req_entry->cached_packets);
			pthread_mutex_unlock(&arpcache.lock);
			return;
		}
  	}

	// not found, create a new arp_req entry
	req_entry = (struct arp_req *)malloc(sizeof(struct arp_req));
	req_entry->iface = iface;
	req_entry->ip4 = ip4;
	req_entry->sent = time(NULL);
	req_entry->retries = 0;
	init_list_head(&req_entry->cached_packets);
	list_add_tail(&req_entry->list, &arpcache.req_list);

	struct cached_pkt *new_pkt = (struct cached_pkt *)malloc(sizeof(struct cached_pkt));
	new_pkt->packet = (char *)malloc(len);
	memcpy(new_pkt->packet, packet, len);
	new_pkt->len = len;
	list_add_tail(&new_pkt->list, &req_entry->cached_packets);

	pthread_mutex_unlock(&arpcache.lock);

	arp_send_request(iface, ip4);
}

// insert the IP->mac mapping into arpcache, if there are pending packets
// waiting for this mapping, fill the ethernet header for each of them, and send
// them out
void arpcache_insert(u32 ip4, u8 mac[ETH_ALEN])
{
	// fprintf(stderr, "TODO: insert ip->mac entry, and send all the pending packets.\n");
	pthread_mutex_lock(&arpcache.lock);

	// insert arp cache
	struct arp_cache_entry *entry = (struct arp_cache_entry *)malloc(sizeof(struct arp_cache_entry));
	entry->ip4 = ip4;
	memcpy(entry->mac, mac, ETH_ALEN);
	entry->valid = 1;
	entry->added = time(NULL);
	int inserted = 0;
	for (int i = 0; i < MAX_ARP_SIZE; i++) {
		if (!arpcache.entries[i].valid) {
			arpcache.entries[i] = *entry;
			inserted = 1;
			break;
		}
	}
	if (!inserted) {
		srand(time(NULL));
		int idx = rand() % MAX_ARP_SIZE;
		arpcache.entries[idx] = *entry;
	}
	free(entry);

	// send pending packets
	struct arp_req *req_entry = NULL, *req_q;
	list_for_each_entry_safe(req_entry, req_q, &arpcache.req_list, list) {
		if (req_entry->ip4 == ip4) {
			struct cached_pkt *pkt_entry = NULL, *pkt_q;
			list_for_each_entry_safe(pkt_entry, pkt_q, &req_entry->cached_packets, list) {
				struct ether_header *eh = (struct ether_header *)pkt_entry->packet;
				memcpy(eh->ether_dhost, mac, ETH_ALEN);
				iface_send_packet(req_entry->iface, pkt_entry->packet, pkt_entry->len);
				list_delete_entry(&pkt_entry->list);
				free(pkt_entry->packet);
				free(pkt_entry);
			}
			list_delete_entry(&req_entry->list);
			free(req_entry);
		}
	}

	pthread_mutex_unlock(&arpcache.lock);
}

// sweep arpcache periodically
//
// For the IP->mac entry, if the entry has been in the table for more than 15
// seconds, remove it from the table.
// For the pending packets, if the arp request is sent out 1 second ago, while 
// the reply has not been received, retransmit the arp request. If the arp
// request has been sent 5 times without receiving arp reply, for each
// pending packet, send icmp packet (DEST_HOST_UNREACHABLE), and drop these
// packets.
void *arpcache_sweep(void *arg) 
{
	while (1) {
		sleep(1);
		// fprintf(stderr, "TODO: sweep arpcache periodically: remove old entries, resend arp requests .\n");
		pthread_mutex_lock(&arpcache.lock);
		time_t now = time(NULL);
		
		// sweep arp cache entries
		for (int i = 0; i < MAX_ARP_SIZE; i++) {
			if (arpcache.entries[i].valid && now - arpcache.entries[i].added > ARP_ENTRY_TIMEOUT) {
				arpcache.entries[i].valid = 0;
			}
		}

		// sweep arp requests
		struct arp_req *req_entry = NULL, *req_q;
		list_for_each_entry_safe(req_entry, req_q, &arpcache.req_list, list) {
			if (now - req_entry->sent >= 1) {
				if (req_entry->retries >= ARP_REQUEST_MAX_RETRIES) {
					// send icmp host unreachable for each pending packet
					struct cached_pkt *pkt_entry = NULL, *pkt_q;
					list_for_each_entry_safe(pkt_entry, pkt_q, &req_entry->cached_packets, list) {
						char *ip_packet = pkt_entry->packet + sizeof(struct ether_header);
						int ip_packet_len = pkt_entry->len - sizeof(struct ether_header);

						icmp_send_packet(ip_packet, ip_packet_len, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH);

						list_delete_entry(&pkt_entry->list);
						free(pkt_entry->packet);
						free(pkt_entry);
					}
					// delete this arp_req entry
					list_delete_entry(&req_entry->list);
					free(req_entry);
				} else {
					// resend arp request
					arp_send_request(req_entry->iface, req_entry->ip4);
					req_entry->sent = now;
					req_entry->retries += 1;
				}
			}
		}

		pthread_mutex_unlock(&arpcache.lock);
	}

	return NULL;
}
