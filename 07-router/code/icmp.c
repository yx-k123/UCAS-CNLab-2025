#include "include/icmp.h"
#include "include/ether.h"
#include "include/ip.h"
#include "include/rtable.h"
#include "include/arp.h"
#include "include/base.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// send icmp packet
void icmp_send_packet(const char *in_pkt, int len, u8 type, u8 code)
{
    struct iphdr* in_ip_hdr = packet_to_ip_hdr(in_pkt);

	rt_entry_t *entry = longest_prefix_match(ntohl(in_ip_hdr->saddr));
    if (!entry) return;
    
    int icmp_data_len;
    if (type == ICMP_ECHOREPLY) {
        icmp_data_len = len - IP_HDR_SIZE(in_ip_hdr);
    } else {
        icmp_data_len = ICMP_HDR_SIZE + 4 + IP_HDR_SIZE(in_ip_hdr) + 8;
    }
    
    int total_len = ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + icmp_data_len;
    char* packet = malloc(total_len);
    memset(packet, 0, total_len);
    
    // fill ethernet header
    struct ether_header *eh = (struct ether_header *)packet;
    eh->ether_type = htons(ETH_P_IP);
    
    // fill IP header
    struct iphdr* ip_hdr = (struct iphdr*)(packet + ETHER_HDR_SIZE);
    ip_hdr->version = 4;
    ip_hdr->ihl = IP_BASE_HDR_SIZE / 4;
    ip_hdr->tos = 0;
    ip_hdr->tot_len = htons(IP_BASE_HDR_SIZE + icmp_data_len);
    ip_hdr->id = 0;
    ip_hdr->frag_off = htons(IP_DF);
    ip_hdr->ttl = DEFAULT_TTL;
    ip_hdr->protocol = IPPROTO_ICMP;
    ip_hdr->saddr = in_ip_hdr->daddr; 
    ip_hdr->daddr = in_ip_hdr->saddr;
    ip_hdr->checksum = checksum((u16*)ip_hdr, IP_BASE_HDR_SIZE, 0);
    
    // fill ICMP header and data
    struct icmphdr* icmp_hdr = (struct icmphdr*)(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE);
    icmp_hdr->type = type;
    icmp_hdr->code = code;
    icmp_hdr->checksum = 0;
    
    if (type == ICMP_ECHOREPLY) {
        struct icmphdr* in_icmp_hdr = (struct icmphdr*)(in_pkt + IP_HDR_SIZE(in_ip_hdr));
        memcpy((char*)icmp_hdr + ICMP_HDR_SIZE, 
               (char*)in_icmp_hdr + ICMP_HDR_SIZE, 
               icmp_data_len - ICMP_HDR_SIZE);
    } else {
        memset((char*)icmp_hdr + ICMP_HDR_SIZE, 0, 4); 
        memcpy((char*)icmp_hdr + ICMP_HDR_SIZE + 4, in_ip_hdr, IP_HDR_SIZE(in_ip_hdr) + 8);
    }
    
    icmp_hdr->checksum = checksum((u16*)icmp_hdr, icmp_data_len, 0);
    
    // send packet
	ip_send_packet(packet, total_len);
    
	printf("free ptr: %p\n", packet);
    free(packet);
}
