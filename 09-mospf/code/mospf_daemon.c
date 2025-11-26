#include "include/mospf_daemon.h"
#include "include/base.h"
#include "include/mospf_proto.h"
#include "include/mospf_nbr.h"
#include "include/mospf_database.h"

#include "include/ip.h"

#include "include/list.h"
#include "include/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

extern ustack_t *instance;

pthread_mutex_t mospf_lock;

void mospf_init()
{
	pthread_mutex_init(&mospf_lock, NULL);

	instance->area_id = 0;
	// get the ip address of the first interface
	iface_info_t *iface = list_entry(instance->iface_list.next, iface_info_t, list);
	instance->router_id = iface->ip;
	instance->sequence_num = 0;
	instance->lsuint = MOSPF_DEFAULT_LSUINT;

	iface = NULL;
	list_for_each_entry(iface, &instance->iface_list, list) {
		iface->helloint = MOSPF_DEFAULT_HELLOINT;
		init_list_head(&iface->nbr_list);
	}

	init_mospf_db();
}

void *sending_mospf_hello_thread(void *param);
void *sending_mospf_lsu_thread(void *param);
void *checking_nbr_thread(void *param);
void *checking_database_thread(void *param);

void mospf_run()
{
	pthread_t hello, lsu, nbr, db;
	pthread_create(&hello, NULL, sending_mospf_hello_thread, NULL);
	pthread_create(&lsu, NULL, sending_mospf_lsu_thread, NULL);
	pthread_create(&nbr, NULL, checking_nbr_thread, NULL);
	pthread_create(&db, NULL, checking_database_thread, NULL);
}

void *sending_mospf_hello_thread(void *param)
{
    // fprintf(stdout, "TODO: send mOSPF Hello message periodically.\n");
    iface_info_t *iface = NULL;
    while (1) {
        pthread_mutex_lock(&mospf_lock); 
        
        list_for_each_entry(iface, &instance->iface_list, list) {
            int len = MOSPF_HDR_SIZE + MOSPF_HELLO_SIZE + IP_BASE_HDR_SIZE + ETHER_HDR_SIZE;
            char *packet = (char *)malloc(len);
            if (!packet) continue; 
            
            memset(packet, 0, len);

            // 1. Fill MOSPF Header & Body
            struct mospf_hdr *mospf = (struct mospf_hdr *)(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE);
            struct mospf_hello *hello = (struct mospf_hello *)(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE);
            
            mospf_init_hello(hello, iface->mask);
            mospf_init_hdr(mospf, MOSPF_TYPE_HELLO, MOSPF_HDR_SIZE + MOSPF_HELLO_SIZE, instance->router_id, instance->area_id);

            mospf->checksum = mospf_checksum(mospf); 

            // 2. Fill IP Header
            struct iphdr *ip = (struct iphdr *)(packet + ETHER_HDR_SIZE);
            ip_init_hdr(ip, iface->ip, MOSPF_ALLSPFRouters, MOSPF_HDR_SIZE + MOSPF_HELLO_SIZE + IP_BASE_HDR_SIZE, IPPROTO_MOSPF);
            
            ip->ttl = 1; 
            ip->checksum = ip_checksum(ip);

            // 3. Fill Ethernet Header
            u8 multicast_mac[ETH_ALEN] = {0x01, 0x00, 0x5e, 0x00, 0x00, 0x05};
            struct ether_header *eh = (struct ether_header *)packet;
            memcpy(eh->ether_dhost, multicast_mac, ETH_ALEN);
            memcpy(eh->ether_shost, iface->mac, ETH_ALEN);
            eh->ether_type = htons(ETH_P_IP);

            // 4. Send
            iface_send_packet(iface, packet, len);
        }
        
        pthread_mutex_unlock(&mospf_lock);

        sleep(MOSPF_DEFAULT_HELLOINT);
    }

    return NULL;
}

void *checking_nbr_thread(void *param)
{
	fprintf(stdout, "TODO: neighbor list timeout operation.\n");

	return NULL;
}

void *checking_database_thread(void *param)
{
	fprintf(stdout, "TODO: link state database timeout operation.\n");

	return NULL;
}

void handle_mospf_hello(iface_info_t *iface, const char *packet, int len)
{
	fprintf(stdout, "TODO: handle mOSPF Hello message.\n");
}

void *sending_mospf_lsu_thread(void *param)
{
	fprintf(stdout, "TODO: send mOSPF LSU message periodically.\n");

	return NULL;
}

void handle_mospf_lsu(iface_info_t *iface, char *packet, int len)
{
	fprintf(stdout, "TODO: handle mOSPF LSU message.\n");
}

void handle_mospf_packet(iface_info_t *iface, char *packet, int len)
{
	struct iphdr *ip = (struct iphdr *)(packet + ETHER_HDR_SIZE);
	struct mospf_hdr *mospf = (struct mospf_hdr *)((char *)ip + IP_HDR_SIZE(ip));

	if (mospf->version != MOSPF_VERSION) {
		log(ERROR, "received mospf packet with incorrect version (%d)", mospf->version);
		return ;
	}
	if (mospf->checksum != mospf_checksum(mospf)) {
		log(ERROR, "received mospf packet with incorrect checksum");
		return ;
	}
	if (ntohl(mospf->aid) != instance->area_id) {
		log(ERROR, "received mospf packet with incorrect area id");
		return ;
	}

	switch (mospf->type) {
		case MOSPF_TYPE_HELLO:
			handle_mospf_hello(iface, packet, len);
			break;
		case MOSPF_TYPE_LSU:
			handle_mospf_lsu(iface, packet, len);
			break;
		default:
			log(ERROR, "received mospf packet with unknown type (%d).", mospf->type);
			break;
	}
}
