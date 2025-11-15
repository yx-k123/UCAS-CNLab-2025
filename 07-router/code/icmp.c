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
    struct iphdr* in_ip_hdr = packet_to_ip_hdr(in_pkt);

    int ip_hlen = IP_HDR_SIZE(in_ip_hdr);
    int orig_ip_len = ntohs(in_ip_hdr->tot_len);
    int icmp_len;

    if (type == ICMP_ECHOREPLY) {
        icmp_len = orig_ip_len - ip_hlen;                
    } else {
        icmp_len = ICMP_HDR_SIZE + 4 + ip_hlen + 8;       
    }

    int total_len = ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + icmp_len;
    char* packet = malloc(total_len);
    if (!packet) return;
    memset(packet, 0, total_len);

    // Ethernet
    struct ether_header *eh = (struct ether_header *)packet;
    eh->ether_type = htons(ETH_P_IP);

    // IP
    struct iphdr* ip_hdr = (struct iphdr*)(packet + ETHER_HDR_SIZE);
    ip_init_hdr(ip_hdr, ntohl(in_ip_hdr->daddr), ntohl(in_ip_hdr->saddr),
                IP_BASE_HDR_SIZE + icmp_len, IPPROTO_ICMP);

    // ICMP
    struct icmphdr* icmp_hdr = (struct icmphdr*)(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE);
    if (type == ICMP_ECHOREPLY) {
        struct icmphdr* in_icmp_hdr =
            (struct icmphdr*)((char*)in_pkt + ETHER_HDR_SIZE + ip_hlen);
        memcpy(icmp_hdr, in_icmp_hdr, icmp_len);  
        icmp_hdr->type = ICMP_ECHOREPLY;
        icmp_hdr->code = 0;
        icmp_hdr->checksum = 0;
    } else {
        icmp_hdr->type = type;
        icmp_hdr->code = code;
        icmp_hdr->checksum = 0;
        memset((char*)icmp_hdr + ICMP_HDR_SIZE, 0, 4);
        memcpy((char*)icmp_hdr + ICMP_HDR_SIZE + 4, in_ip_hdr, ip_hlen + 8);
    }

    icmp_hdr->checksum = checksum((u16*)icmp_hdr, icmp_len, 0);

    ip_send_packet(packet, total_len);
}
