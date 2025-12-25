#include "include/icmp.h"
#include "include/ether.h"
#include "include/ip.h"
#include "include/rtable.h"
#include "include/arp.h"
#include "include/base.h"
#include "include/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// send icmp packet
void icmp_send_packet(const char *in_pkt, int len, u8 type, u8 code)
{
	//fprintf(stderr, "TODO: malloc and send icmp packet.\n");
	struct iphdr* in_ip_hdr = packet_to_ip_hdr(in_pkt);
	int packet_len;
	if(type == ICMP_ECHOREPLY){
		packet_len = len;
	}else{
		packet_len = ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + ICMP_HDR_SIZE + IP_HDR_SIZE(in_ip_hdr) + 8;
	}

	char *send_pkt = malloc(packet_len * sizeof(char));
	struct ether_header *eh = (struct ether_header *) send_pkt;
	struct iphdr *iph = packet_to_ip_hdr(send_pkt);
	struct icmphdr *icmph = (struct icmphdr *)(send_pkt + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE);
	eh->ether_type = htons(ETH_P_IP);

	rt_entry_t *entry = longest_prefix_match(ntohl(in_ip_hdr->saddr));
	ip_init_hdr(iph, entry->iface->ip, ntohl(in_ip_hdr->saddr),packet_len - ETHER_HDR_SIZE, 1);

	icmph->code = code;
	icmph->type = type;

	if(type == 0){
		memcpy(send_pkt + ETHER_HDR_SIZE + IP_HDR_SIZE(iph) + 4, in_pkt + ETHER_HDR_SIZE + IP_HDR_SIZE(in_ip_hdr) + 4, packet_len - (ETHER_HDR_SIZE + IP_HDR_SIZE(in_ip_hdr) + 4));
	}else{
		memset(send_pkt + ETHER_HDR_SIZE + IP_HDR_SIZE(iph) + 4, 0, 4);
		memcpy(send_pkt + ETHER_HDR_SIZE + IP_HDR_SIZE(iph) + 4 + 4, in_ip_hdr, IP_HDR_SIZE(in_ip_hdr) + 8);
	}
	icmph->checksum = icmp_checksum(icmph, packet_len - ETHER_HDR_SIZE - IP_BASE_HDR_SIZE);

    // FILE *fp = fopen("./log/packets.txt", "w");
	// if (fp) {
	// 	unsigned char *buf = (unsigned char *)send_pkt;
	// 	for (int i = 0; i < packet_len; i++) {
	// 		fprintf(fp, "%02x", buf[i]);
	// 		if ((i + 1) % 16 == 0)
	// 			fprintf(fp, "\n");
	// 		else
	// 			fprintf(fp, " ");
	// 	}
	// 	fprintf(fp, "\n\n");
	// 	fclose(fp);
	// }

	ip_send_packet(send_pkt, packet_len);
}