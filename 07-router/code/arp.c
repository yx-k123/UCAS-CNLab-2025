#include "include/arp.h"
#include "include/base.h"
#include "include/types.h"
#include "include/ether.h"
#include "include/arpcache.h"
#include "include/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// #include "log.h"

// send an arp request: encapsulate an arp request packet, send it out through
// iface_send_packet
void arp_send_request(iface_info_t *iface, u32 dst_ip)
{
	// fprintf(stderr, "TODO: send arp request when lookup failed in arpcache.\n");
	char packet[sizeof(struct ether_header) + sizeof(struct ether_arp)];
	struct ether_header *eh = (struct ether_header *)packet;
	struct ether_arp *arp_hdr = (struct ether_arp *)(packet + sizeof(struct ether_header));
	// fill ethernet header
	memset(eh->ether_dhost, 0xff, ETH_ALEN); 						// dest mac:broadcast
	memcpy(eh->ether_shost, iface->mac, ETH_ALEN); 			// src mac
	eh->ether_type = htons(ETH_P_ARP); 					 	   // type:0806 ARP
	// fill arp header
	arp_hdr->arp_hrd = htons(ARPHRD_ETHER); 					   	// hardware type: Ethernet
	arp_hdr->arp_pro = htons(ETH_P_IP); 						   	// protocol type: 0800 IPV4
	arp_hdr->arp_hln = ETH_ALEN; 										   	// hardware address length: 6
	arp_hdr->arp_pln = 4; 												  	// protocol address length: 4
	arp_hdr->arp_op = htons(ARPOP_REQUEST); 						// operation: 1 request
	memcpy(arp_hdr->arp_sha, iface->mac, ETH_ALEN); 				// sender mac
	arp_hdr->arp_spa = htonl(iface->ip); 								// sender ip
	memset(arp_hdr->arp_tha, 0x00, ETH_ALEN); 							// target mac: unknown
	arp_hdr->arp_tpa = htonl(dst_ip); 							// target ip
	iface_send_packet(iface, packet, sizeof(packet)); 					// send packet
}

// send an arp reply packet: encapsulate an arp reply packet, send it out
// through iface_send_packet
void arp_send_reply(iface_info_t *iface, struct ether_arp *req_hdr)
{
	// fprintf(stderr, "TODO: send arp reply when receiving arp request.\n");
	char packet[sizeof(struct ether_header) + sizeof(struct ether_arp)];
	struct ether_header *eh = (struct ether_header *)packet;
	struct ether_arp *arp_hdr = (struct ether_arp *)(packet + sizeof(struct ether_header));
	// fill ethernet header
	memcpy(eh->ether_dhost, req_hdr->arp_sha, ETH_ALEN);
	memcpy(eh->ether_shost, iface->mac, ETH_ALEN);
	eh->ether_type = htons(ETH_P_ARP);
	// fill arp header
	arp_hdr->arp_hrd = htons(ARPHRD_ETHER);
	arp_hdr->arp_pro = htons(ETH_P_IP);
	arp_hdr->arp_hln = ETH_ALEN;
	arp_hdr->arp_pln = 4;
	arp_hdr->arp_op = htons(ARPOP_REPLY);
	memcpy(arp_hdr->arp_sha, iface->mac, ETH_ALEN);
	arp_hdr->arp_spa = htonl(iface->ip);
	memcpy(arp_hdr->arp_tha, req_hdr->arp_sha, ETH_ALEN);
	arp_hdr->arp_tpa = req_hdr->arp_spa;
	iface_send_packet(iface, packet, sizeof(packet));
}

void handle_arp_packet(iface_info_t *iface, char *packet, int len)
{
	// fprintf(stderr, "TODO: process arp packet: arp request & arp reply.\n");
	if (len < sizeof(struct ether_header) + sizeof(struct ether_arp)) {
        return; 
    }

	struct ether_arp *arp_hdr = (struct ether_arp *)(packet + sizeof(struct ether_header));
	u16 op = ntohs(arp_hdr->arp_op);
	u32 spa = ntohl(arp_hdr->arp_spa);
	u32 tpa = ntohl(arp_hdr->arp_tpa);

	if (op == ARPOP_REQUEST) {
		if (tpa == iface->ip) {
			arpcache_insert(spa, arp_hdr->arp_sha);
			arp_send_reply(iface, arp_hdr);
		}
	}
	else if (op == ARPOP_REPLY) {
		arpcache_insert(spa, arp_hdr->arp_sha);
	}
}

// send (IP) packet through arpcache lookup 
//
// Lookup the mac address of dst_ip in arpcache. If it is found, fill the
// ethernet header and emit the packet by iface_send_packet, otherwise, pending 
// this packet into arpcache, and send arp request.
void iface_send_packet_by_arp(iface_info_t *iface, u32 dst_ip, char *packet, int len)
{
	struct ether_header *eh = (struct ether_header *)packet;
	memcpy(eh->ether_shost, iface->mac, ETH_ALEN);
	eh->ether_type = htons(ETH_P_IP);

	u8 dst_mac[ETH_ALEN];
	int found = arpcache_lookup(dst_ip, dst_mac);
	if (found) {
		log(DEBUG, "found the mac of %x, send this packet", dst_ip);
		memcpy(eh->ether_dhost, dst_mac, ETH_ALEN);
		iface_send_packet(iface, packet, len);
	}
	else {
		log(DEBUG, "lookup %x failed, pend this packet", dst_ip);
		arpcache_append_packet(iface, dst_ip, packet, len);
	}
}
