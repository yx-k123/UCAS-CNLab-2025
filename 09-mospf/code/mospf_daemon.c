#include "include/mospf_daemon.h"
#include "include/base.h"
#include "include/mospf_proto.h"
#include "include/mospf_nbr.h"
#include "include/mospf_database.h"
#include "include/rtable.h" 
#include <limits.h> 

#include "include/ip.h"

#include "include/list.h"
#include "include/log.h"

#include <bits/pthreadtypes.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

extern ustack_t *instance;

pthread_mutex_t mospf_lock;

void mospf_init()
{	
	setbuf(stdout, NULL);
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

void mospf_run()
{
	pthread_t hello, lsu, nbr, db;
	// print_rtable();
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
	// fprintf(stdout, "TODO: neighbor list timeout operation.\n");
	iface_info_t *iface = NULL;
	while (1) {
		pthread_mutex_lock(&mospf_lock);
		list_for_each_entry(iface, &instance->iface_list, list) {
			mospf_nbr_t *nbr = NULL, *nbr_q = NULL;
			list_for_each_entry_safe(nbr, nbr_q, &iface->nbr_list, list) {
				nbr->alive++;
				if (nbr->alive > iface->helloint * 3) {
					list_delete_entry(&nbr->list);
					free(nbr);
					iface->num_nbr--;
					sending_mospf_lsu();
				}
			}
		}
		pthread_mutex_unlock(&mospf_lock);
		sleep(1);
	}
	return NULL;
}

void *checking_database_thread(void *param)
{
	// fprintf(stdout, "TODO: link state database timeout operation.\n");
	while (1) {
		pthread_mutex_lock(&mospf_lock);
		mospf_db_entry_t *db_entry = NULL, *db_q = NULL;
		list_for_each_entry_safe(db_entry, db_q, &mospf_db, list) {
			db_entry->alive++;
			if (db_entry->alive > MOSPF_DATABASE_TIMEOUT) {
				fprintf(stdout, "DEBUG: Router " IP_FMT " timed out, removing from DB.\n", 
                        HOST_IP_FMT_STR(db_entry->rid));
                list_delete_entry(&db_entry->list);
                free(db_entry);
            }
		}
		pthread_mutex_unlock(&mospf_lock);
		sleep(1);
	}

	return NULL;
}

void handle_mospf_hello(iface_info_t *iface, const char *packet, int len)
{
	// fprintf(stdout, "TODO: handle mOSPF Hello message.\n");
	struct mospf_header *eh = (struct mospf_header *)packet;
	struct iphdr *ip = (struct iphdr *)(packet + ETHER_HDR_SIZE);
	struct mospf_hdr *mospf = (struct mospf_hdr *)((char *)ip + IP_HDR_SIZE(ip));
	struct mospf_hello *hello = (struct mospf_hello *)((char *)mospf + MOSPF_HDR_SIZE);
	
	u32 rid = ntohl(mospf->rid);
	u32 src_ip = ntohl(ip->saddr);
	u32 mask = ntohl(hello->mask);

	pthread_mutex_lock(&mospf_lock);

	int found = 0;
	mospf_nbr_t *nbr = NULL;

	list_for_each_entry(nbr, &iface->nbr_list, list) {
		if (nbr->nbr_id == rid) {
			found = 1;
			nbr->alive = 0;
			break;
		}
	}
	if (!found) {
		nbr = (mospf_nbr_t *)malloc(sizeof(mospf_nbr_t));
		if (nbr) {
			nbr->nbr_id = rid;
			nbr->nbr_ip = src_ip;
			nbr->nbr_mask = mask;
			nbr->alive = 0;
			list_add_tail(&nbr->list, &iface->nbr_list);
			iface->num_nbr++;
			sending_mospf_lsu();
		}	
	}	
	pthread_mutex_unlock(&mospf_lock);
}

void *sending_mospf_lsu_thread(void *param)
{
	// fprintf(stdout, "TODO: send mOSPF LSU message periodically.\n");
	while (1) {
		pthread_mutex_lock(&mospf_lock);
		sending_mospf_lsu();
		pthread_mutex_unlock(&mospf_lock);
		sleep(MOSPF_DEFAULT_LSUINT);
	}
	return NULL;
}

void handle_mospf_lsu(iface_info_t *iface, char *packet, int len)
{
    // fprintf(stdout, "TODO: handle mOSPF LSU message.\n");
    struct iphdr *ip = (struct iphdr *)(packet + ETHER_HDR_SIZE);
    struct mospf_hdr *mospf = (struct mospf_hdr *)((char *)ip + IP_HDR_SIZE(ip));
    struct mospf_lsu *lsu = (struct mospf_lsu *)((char *)mospf + MOSPF_HDR_SIZE);
    struct mospf_lsa *lsa = (struct mospf_lsa *)((char *)lsu + MOSPF_LSU_SIZE);

    u32 nadv = ntohl(lsu->nadv);
    u32 rid = ntohl(mospf->rid);
    u32 seq = ntohs(lsu->seq);

    if (rid == instance->router_id) {
        return;
    }

    pthread_mutex_lock(&mospf_lock);

    int found = 0;
    int is_update = 0;
    mospf_db_entry_t *db_entry = NULL;
    list_for_each_entry(db_entry, &mospf_db, list) {
        if (db_entry->rid == rid) {
            found = 1;
            if (db_entry->seq < seq) {
                is_update = 1;
                db_entry->seq = seq;
                db_entry->nadv = nadv;
                db_entry->alive = 0;
                free(db_entry->array);
                db_entry->array = (struct mospf_lsa *)malloc(MOSPF_LSA_SIZE * nadv);
                memcpy(db_entry->array, lsa, MOSPF_LSA_SIZE * nadv);
            }
            break;
        }
    }
    if (!found) {
        db_entry = (mospf_db_entry_t *)malloc(sizeof(mospf_db_entry_t));
        if (db_entry) {
            db_entry->rid = rid;
            db_entry->seq = seq;
            db_entry->alive = 0;
            db_entry->nadv = nadv;
            db_entry->array = (struct mospf_lsa *)malloc(MOSPF_LSA_SIZE * nadv);
            memcpy(db_entry->array, lsa, MOSPF_LSA_SIZE * nadv);
            list_add_tail(&db_entry->list, &mospf_db);
            is_update = 1;
        }
    }
    pthread_mutex_unlock(&mospf_lock);

    if (is_update) {
        update_rtable();
        print_mospf_db();

        lsu->ttl--;
        if (lsu->ttl > 0) 
        {
            iface_info_t *out_iface = NULL;
            list_for_each_entry (out_iface, &instance->iface_list, list) 
            {
                if (out_iface == iface) continue;

                mospf_nbr_t *nbr = NULL;
                list_for_each_entry(nbr, &out_iface->nbr_list, list) 
                {
                    if (nbr->nbr_id != rid) 
                    {
                        char *forward = (char *)malloc(len);
                        struct iphdr * iph = packet_to_ip_hdr(forward);
                        struct mospf_hdr * mospfh = (struct mospf_hdr *)(forward + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE);

                        memcpy(forward, packet, len);
                        
                        iph->saddr = htonl(out_iface->ip);
                        iph->daddr = htonl(nbr->nbr_ip);
                        iph->checksum = ip_checksum(iph);

                        mospfh->checksum = mospf_checksum(mospfh);
                        
                        ip_send_packet(forward, len);
                    }
                }
            }
        }
    }
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

void sending_mospf_lsu()
{
    iface_info_t *iface = NULL;
    int total_nadv = 0;
    list_for_each_entry(iface, &instance->iface_list, list) {
        if (iface->num_nbr > 0) {
            total_nadv += iface->num_nbr;
        } else {
            total_nadv += 1;
        }
    }

    struct mospf_lsa *lsa_array = (struct mospf_lsa *)malloc(MOSPF_LSA_SIZE * total_nadv);
    memset(lsa_array, 0, MOSPF_LSA_SIZE * total_nadv);

    int idx = 0;
    list_for_each_entry(iface, &instance->iface_list, list) {
        if (iface->num_nbr == 0) {
            lsa_array[idx].mask = htonl(iface->mask);
            lsa_array[idx].rid = htonl(0);
            lsa_array[idx].network = htonl(iface->ip & iface->mask);	
            idx++;
        } else {
            mospf_nbr_t *nbr = NULL;
            list_for_each_entry(nbr, &iface->nbr_list, list) {
                lsa_array[idx].mask = htonl(iface->mask);
                lsa_array[idx].rid = htonl(nbr->nbr_id);
                lsa_array[idx].network = htonl(iface->ip & iface->mask);
                idx++;
            }
        }
    }

    instance->sequence_num++;
    int lsu_len = MOSPF_HDR_SIZE + MOSPF_LSU_SIZE + MOSPF_LSA_SIZE * total_nadv;
    list_for_each_entry(iface, &instance->iface_list, list) {
        mospf_nbr_t *nbr = NULL;
        list_for_each_entry(nbr, &iface->nbr_list, list) {
            int len = lsu_len + IP_BASE_HDR_SIZE + ETHER_HDR_SIZE;
            char *packet = (char *)malloc(len);
            if (!packet) continue;

            memset(packet, 0, len);

            // 1. Fill MOSPF Header & Body
            struct mospf_hdr *mospf = (struct mospf_hdr *)(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE);
            struct mospf_lsu *lsu = (struct mospf_lsu *)(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE);
            struct mospf_lsa *lsa = (struct mospf_lsa *)(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_LSU_SIZE);

            mospf_init_lsu(lsu, total_nadv);
            mospf_init_hdr(mospf, MOSPF_TYPE_LSU, lsu_len, instance->router_id, instance->area_id);

            memcpy(lsa, lsa_array, MOSPF_LSA_SIZE * total_nadv);

            mospf->checksum = mospf_checksum(mospf);

            // 2. Fill IP Header
            struct iphdr *ip = (struct iphdr *)(packet + ETHER_HDR_SIZE);
            ip_init_hdr(ip, iface->ip, nbr->nbr_ip, lsu_len + IP_BASE_HDR_SIZE, IPPROTO_MOSPF);

            ip->checksum = ip_checksum(ip);

            // 3. Fill Ethernet Header
            struct ether_header *eh = (struct ether_header *)packet;
            u8 nbr_mac[ETH_ALEN] = {0};
            memcpy(eh->ether_dhost, nbr_mac, ETH_ALEN);
            memcpy(eh->ether_shost, iface->mac, ETH_ALEN);
            eh->ether_type = htons(ETH_P_IP);

            // 4. Send
            ip_send_packet(packet, len);
        }
    }
    free(lsa_array);
}

void update_rtable()
{
    fprintf(stdout, "Re-calculating routing table...\n");
    pthread_mutex_lock(&mospf_lock);

    // 1. delete old dynamic routes
    rt_entry_t *entry, *q;
    list_for_each_entry_safe(entry, q, &rtable, list) {
        if (entry->gw != 0) { // not a directly connected route
            list_delete_entry(&entry->list);
            free(entry);
        }
    }

    // 2. Initialize Dijkstra data structure
    dijkstra_node_t nodes[MAX_NODES];
    int num_nodes = 0;

    // Add self (Root)
    nodes[num_nodes].rid = instance->router_id;
    nodes[num_nodes].dist = 0;
    nodes[num_nodes].visited = 0;
    nodes[num_nodes].next_hop = 0;
    nodes[num_nodes].iface = NULL;
    num_nodes++;

    // Add all routers from LSDB to the node list
    mospf_db_entry_t *db_e;
    list_for_each_entry(db_e, &mospf_db, list) {
        if (num_nodes >= MAX_NODES) break;
        nodes[num_nodes].rid = db_e->rid;
        nodes[num_nodes].dist = (u32)-1;
        nodes[num_nodes].visited = 0;
        nodes[num_nodes].next_hop = 0;
        nodes[num_nodes].iface = NULL;
        num_nodes++;
    }

    // 3. Dijkstra Algorithm
    while (1) {
        // Find the unvisited node u with the smallest distance
        int u_idx = -1;
        u32 min_dist = (u32)-1;

        for (int i = 0; i < num_nodes; i++) {
            if (!nodes[i].visited && nodes[i].dist < min_dist) {
                min_dist = nodes[i].dist;
                u_idx = i;
            }
        }

        if (u_idx == -1) break; // All reachable nodes have been visited
        nodes[u_idx].visited = 1;

        // Traverse neighbors v of u
        // Get LSA information of u
        int nadv = 0;
        struct mospf_lsa *lsa_array = NULL;

        if (nodes[u_idx].rid == instance->router_id) {
            // If u is self, get neighbors from iface_list
            iface_info_t *iface;
            list_for_each_entry(iface, &instance->iface_list, list) {
                mospf_nbr_t *nbr;
                list_for_each_entry(nbr, &iface->nbr_list, list) {
                    // Find the neighbor's position in the nodes array
                    for (int v = 0; v < num_nodes; v++) {
                        if (nodes[v].rid == nbr->nbr_id) {
                            // Relaxation
                            if (nodes[u_idx].dist + 1 < nodes[v].dist) {
                                nodes[v].dist = nodes[u_idx].dist + 1;
                                nodes[v].next_hop = nbr->nbr_ip; // First hop next hop
                                nodes[v].iface = iface;          // First hop outgoing interface
                            }
                            break;
                        }
                    }
                }
            }
        } else {
            // If u is other router, get LSA from LSDB
            mospf_db_entry_t *db = NULL;
            list_for_each_entry(db, &mospf_db, list) {
                if (db->rid == nodes[u_idx].rid) {
                    lsa_array = db->array;
                    nadv = db->nadv;
                    break;
                }
            }

            if (!lsa_array) continue;

            for (int i = 0; i < nadv; i++) {
                u32 neighbor_rid = ntohl(lsa_array[i].rid);
                u32 network = ntohl(lsa_array[i].network);
                u32 mask = ntohl(lsa_array[i].mask);

                int found = 0;
                rt_entry_t *entry_check;
                list_for_each_entry(entry_check, &rtable, list) {
                    if (entry_check->dest == network && entry_check->mask == mask) {
                        found = 1;
                        break;
                    }
                }

                if (!found) {
                    if (nodes[u_idx].dist != (u32)-1) {
                        rt_entry_t *new_entry = (rt_entry_t *)malloc(sizeof(rt_entry_t));
                        memset(new_entry, 0, sizeof(rt_entry_t)); 

                        new_entry->dest = network;
                        new_entry->mask = mask;
                        new_entry->gw = nodes[u_idx].next_hop; 
                        new_entry->iface = nodes[u_idx].iface;
                        
                        if (new_entry->iface) {
                            strcpy(new_entry->if_name, new_entry->iface->name);
                        }

                        list_add_tail(&new_entry->list, &rtable);
                    }
                }

                if (neighbor_rid != 0) {
                    for (int v = 0; v < num_nodes; v++) {
                        if (nodes[v].rid == neighbor_rid) {
                            if (nodes[u_idx].dist + 1 < nodes[v].dist) {
                                nodes[v].dist = nodes[u_idx].dist + 1;
                                nodes[v].next_hop = nodes[u_idx].next_hop; 
                                nodes[v].iface = nodes[u_idx].iface; 
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
    // print_rtable();
    pthread_mutex_unlock(&mospf_lock);
}

void print_mospf_db()
{
    pthread_mutex_lock(&mospf_lock);
    
    fprintf(stdout, "MOSPF Database entries:\n");
    fprintf(stdout, "RID\t\tNetwork\t\tMask\t\tNeighbor\n");
    fprintf(stdout, "----------------------------------------------------------------\n");

    mospf_db_entry_t *db_entry = NULL;
    list_for_each_entry(db_entry, &mospf_db, list) {
        for (int i = 0; i < db_entry->nadv; i++) {
            struct mospf_lsa *lsa = &db_entry->array[i];
            u32 db_rid = db_entry->rid;
            u32 net = ntohl(lsa->network);
            u32 m = ntohl(lsa->mask);
            u32 neigh = ntohl(lsa->rid);
            fprintf(stdout, IP_FMT "\t" IP_FMT "\t" IP_FMT "\t" IP_FMT "\n",
                    HOST_IP_FMT_STR(db_rid),
                    HOST_IP_FMT_STR(net),
                    HOST_IP_FMT_STR(m),
                    HOST_IP_FMT_STR(neigh));
        }
    }
    fprintf(stdout, "\n");
    
    pthread_mutex_unlock(&mospf_lock);
}