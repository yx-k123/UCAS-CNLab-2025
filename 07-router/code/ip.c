#include "include/ip.h"
#include "include/icmp.h"
#include "include/rtable.h"
#include "include/arp.h"

#include <stdio.h>
#include <stdlib.h>

// handle ip packet
//
// If the packet is ICMP echo request and the destination IP address is equal to
// the IP address of the iface, send ICMP echo reply; otherwise, forward the
// packet.
void handle_ip_packet(iface_info_t *iface, char *packet, int len)
{
	// fprintf(stderr, "TODO: handle ip packet.\n");
	struct iphdr *ip_hdr = packet_to_ip_hdr(packet);
	u32 dest_ip = ntohl(ip_hdr->daddr);
	u32 iface_ip = iface->ip;
	if (dest_ip == iface_ip) {
		if (ip_hdr->protocol == IPPROTO_ICMP) {
			struct icmphdr *icmp_hdr = (struct icmphdr *)IP_DATA(ip_hdr);
			if (icmp_hdr->type == ICMP_ECHOREQUEST) {
				icmp_send_packet(packet, len, ICMP_ECHOREPLY, 0);
			}
		} 
	} else {
		// forward the packet
		rt_entry_t *entry = longest_prefix_match(dest_ip);
		if (entry == NULL) {
			icmp_send_packet(packet, len, ICMP_DEST_UNREACH, ICMP_NET_UNREACH);
		} else {
			if (--ip_hdr->ttl == 0) {
				icmp_send_packet(packet, len, ICMP_TIME_EXCEEDED, ICMP_EXC_TTL);
			} else {
				u32 nxt_hop = entry->gw == 0 ? dest_ip : entry->gw;
				ip_hdr->checksum = ip_checksum(ip_hdr);
				iface_send_packet_by_arp(entry->iface, nxt_hop, packet, len);
			}
		}
	}
}