#include "include/nat.h"
#include "include/ip.h"
#include "include/icmp.h"
#include "include/list.h"
#include "include/tcp.h"
#include "include/rtable.h"
#include "include/log.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

static struct nat_table nat;

// get the interface from iface name
static iface_info_t *if_name_to_iface(const char *if_name)
{
	iface_info_t *iface = NULL;
	list_for_each_entry(iface, &instance->iface_list, list) {
		if (strcmp(iface->name, if_name) == 0)
			return iface;
	}

	log(ERROR, "Could not find the desired interface according to if_name '%s'", if_name);
	return NULL;
}

// determine the direction of the packet, DIR_IN / DIR_OUT / DIR_INVALID
static int get_packet_direction(char *packet)
{
	//fprintf(stdout, "TODO: determine the direction of this packet.\n");
	struct iphdr *ip = packet_to_ip_hdr(packet);
	rt_entry_t *rt = longest_prefix_match(ntohl(ip->saddr));

	if (rt->iface->index == nat.internal_iface->index) {
		return DIR_OUT;
	} else if (rt->iface->index == nat.external_iface->index) {
		return DIR_IN;
	}

	return DIR_INVALID;
}

// do translation for the packet: replace the ip/port, recalculate ip & tcp
// checksum, update the statistics of the tcp connection
void do_translation(iface_info_t *iface, char *packet, int len, int dir)
{
    pthread_mutex_lock(&nat.lock);

    struct iphdr *ip = packet_to_ip_hdr(packet);
    struct tcphdr *tcp = packet_to_tcp_hdr(packet);

    // 获取当前数据包信息（主机序）
    u32 saddr = ntohl(ip->saddr);
    u32 daddr = ntohl(ip->daddr);
    u16 sport = ntohs(tcp->sport);
    u16 dport = ntohs(tcp->dport);

    struct nat_mapping *mapping = NULL;
    
    // 1. 确定 Remote IP/Port 并计算哈希
    u32 remote_ip = (dir == DIR_OUT) ? daddr : saddr;
    u16 remote_port = (dir == DIR_OUT) ? dport : sport;
    
    u8 hash = hash8((char *)&remote_ip, 4); 
    struct list_head *head = &nat.nat_mapping_list[hash];
    struct nat_mapping *entry;

    // 2. 查找现有映射
    list_for_each_entry(entry, head, list) {
        if (dir == DIR_OUT) {
            // 出站匹配：源IP/端口（内网） + 对端IP/端口
            if (entry->internal_ip == saddr && entry->internal_port == sport &&
                entry->remote_ip == remote_ip && entry->remote_port == remote_port) {
                mapping = entry;
                break;
            }
        } else { // DIR_IN
            // 入站匹配：目的IP/端口（公网） + 对端IP/端口
            if (entry->external_ip == daddr && entry->external_port == dport &&
                entry->remote_ip == remote_ip && entry->remote_port == remote_port) {
                mapping = entry;
                break;
            }
        }
    }

    // 3. 处理映射未找到的情况 (Miss)
    if (mapping == NULL) {
        if (dir == DIR_OUT) {
            // SNAT: 分配新端口并建立映射
            int i;
            u16 assigned_port = 0;
            // 遍历端口池寻找可用端口
            for (i = NAT_PORT_MIN; i <= NAT_PORT_MAX; i++) {
                if (nat.assigned_ports[i] == 0) {
                    nat.assigned_ports[i] = 1;
                    assigned_port = i;
                    break;
                }
            }

            // 如果端口耗尽，无法转换，直接丢弃或返回
            if (assigned_port == 0) {
                pthread_mutex_unlock(&nat.lock);
                return; 
            }

            // 创建新映射
            mapping = (struct nat_mapping *)malloc(sizeof(struct nat_mapping));
            if (!mapping) { 
				pthread_mutex_unlock(&nat.lock); 
				return; 
			}
            
            memset(mapping, 0, sizeof(struct nat_mapping));
            list_add_tail(&mapping->list, head);

            mapping->internal_ip = saddr;
            mapping->internal_port = sport;
            mapping->external_ip = nat.external_iface->ip;
            mapping->external_port = assigned_port;
            mapping->remote_ip = remote_ip;
            mapping->remote_port = remote_port;
            mapping->update_time = time(NULL);

        } else { // DIR_IN
            // DNAT: 查找匹配的DNAT规则
            struct dnat_rule *rule;
            struct dnat_rule *found_rule = NULL;

            // 遍历规则链表
            list_for_each_entry(rule, &nat.rules, list) {
                if (rule->external_ip == daddr && rule->external_port == dport) {
                    found_rule = rule;
                    break;
                }
            }

            if (found_rule) {
                // 命中规则，建立新的连接跟踪映射
                mapping = (struct nat_mapping *)malloc(sizeof(struct nat_mapping));
                if (!mapping) { 
					pthread_mutex_unlock(&nat.lock); 
					return; 
				}

                memset(mapping, 0, sizeof(struct nat_mapping));
                list_add_tail(&mapping->list, head);

                mapping->internal_ip = found_rule->internal_ip;
                mapping->internal_port = found_rule->internal_port;
                mapping->external_ip = daddr;   // 此时 external_ip 就是目的 IP
                mapping->external_port = dport;
                mapping->remote_ip = remote_ip; // 此时 remote_ip 是源 IP
                mapping->remote_port = remote_port;
                mapping->update_time = time(NULL);
            } else {
                // 既没有现有连接，也没有DNAT规则，这是非法入站包，丢弃
                pthread_mutex_unlock(&nat.lock);
                return;
            }
        }
    }

    // 4. 更新连接状态 (SEQ/ACK/FIN)
    mapping->update_time = time(NULL);

    u32 seq = ntohl(tcp->seq);
    u32 ack = ntohl(tcp->ack);
    int payload_len = len - (ip->ihl * 4) - (tcp->off * 4); // 计算TCP负载长度
    u32 seq_end = seq + payload_len + ((tcp->flags & (TCP_SYN|TCP_FIN)) ? 1 : 0);

    if (dir == DIR_OUT) {
        // 内网发出的包：更新 internal 状态
        mapping->conn.internal_seq_end = (seq_end > mapping->conn.internal_seq_end) ? seq_end : mapping->conn.internal_seq_end;
        if (tcp->ack) mapping->conn.internal_ack = ack;
        if (tcp->flags & TCP_FIN) mapping->conn.internal_fin = 1;
    } else {
        // 外网发入的包：更新 external 状态
        mapping->conn.external_seq_end = (seq_end > mapping->conn.external_seq_end) ? seq_end : mapping->conn.external_seq_end;
        if (tcp->ack) mapping->conn.external_ack = ack;
        if (tcp->flags & TCP_FIN) mapping->conn.external_fin = 1;
    }


    // 5. 执行地址转换
    if (dir == DIR_OUT) {
        // SNAT: 改源地址 (Internal -> External)
        ip->saddr = htonl(mapping->external_ip);
        tcp->sport = htons(mapping->external_port);
    } else {
        // DNAT: 改目的地址 (External -> Internal)
        ip->daddr = htonl(mapping->internal_ip);
        tcp->dport = htons(mapping->internal_port);
    }

    ip->checksum = ip_checksum(ip);
    tcp->checksum = tcp_checksum(ip, tcp);

    pthread_mutex_unlock(&nat.lock);

    ip_send_packet(packet, len);
}

void nat_translate_packet(iface_info_t *iface, char *packet, int len)
{
	int dir = get_packet_direction(packet);
	if (dir == DIR_INVALID) {
		log(ERROR, "invalid packet direction, drop it.");
		icmp_send_packet(packet, len, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH);
		free(packet);
		return ;
	}

	struct iphdr *ip = packet_to_ip_hdr(packet);
	if (ip->protocol != IPPROTO_TCP) {
		log(ERROR, "received non-TCP packet (0x%0hhx), drop it", ip->protocol);
		free(packet);
		return ;
	}

	do_translation(iface, packet, len, dir);
}

// check whether the flow is finished according to FIN bit and sequence number
// XXX: seq_end is calculated by `tcp_seq_end` in tcp.h
static int is_flow_finished(struct nat_connection *conn)
{
    return (conn->internal_fin && conn->external_fin) && \
            (conn->internal_ack >= conn->external_seq_end) && \
            (conn->external_ack >= conn->internal_seq_end);
}

// nat timeout thread: find the finished flows, remove them and free port
// resource
void *nat_timeout()
{
	while (1) {
		// fprintf(stdout, "TODO: sweep finished flows periodically.\n");
		pthread_mutex_lock(&nat.lock);

		time_t now = time(NULL);

		for (int i = 0; i < HASH_8BITS; i++) {
			struct list_head *head = &nat.nat_mapping_list[i];
			struct nat_mapping *mapping, *q;

			list_for_each_entry_safe(mapping, q, head, list) {
				// 检查是否超时或连接已完成
				if (is_flow_finished(&mapping->conn) || \
					(now - mapping->update_time >= TCP_ESTABLISHED_TIMEOUT)) {
					
					// 释放分配的端口
					if (mapping->external_port >= NAT_PORT_MIN && mapping->external_port <= NAT_PORT_MAX) {
						nat.assigned_ports[mapping->external_port] = 0;
					}

					// 从链表中移除并释放内存
					list_delete_entry(&mapping->list);
					free(mapping);
				}
			}
		}

		pthread_mutex_unlock(&nat.lock);
		sleep(1);
	}

	return NULL;
}

int parse_config(const char *filename)
{
	// fprintf(stdout, "TODO: parse config file, including i-iface, e-iface (and dnat-rules if existing).\n");
    char line[256];
    FILE *fp = fopen(filename, "rb");
    char type[128], name[128], exter[64], inter[64];
    while (!feof(fp) && !ferror(fp)) {
        strcpy(line, "\n");
        fgets(line, sizeof(line), fp);
        if (line[0] == '\n') break;
        sscanf(line, "%s %s", type, name);
        type[14] = '\0';
        if (strcmp(type, "internal-iface") == 0) {
            printf("Internal-iface: %s .\n", name);
            nat.internal_iface = if_name_to_iface(name);
        } else if (strcmp(type, "external-iface") == 0) {
            printf("External-iface: %s .\n", name);
            nat.external_iface = if_name_to_iface(name);
        } else printf("config iface failed : %s .\n", type);
    }
    u32 ip4, ip3, ip2, ip1, ip;
    u16 port;
    while (!feof(fp) && !ferror(fp)) {
        strcpy(line, "\n");
        fgets(line, sizeof(line), fp);
        if (line[0] == '\n') break;
        sscanf(line, "%s %s %s %s", type, exter, name, inter);
        type[10] = '\0';
        if (strcmp(type, "dnat-rules") == 0) {
            printf("[Dnat] Loading rule item : %s to %s.\n", exter, inter);
            struct dnat_rule *rule = (struct dnat_rule*)malloc(sizeof(struct dnat_rule));
            list_add_tail(&rule->list, &nat.rules);

            sscanf(exter, "%[^:]:%hu", name, &port);
            sscanf(name, "%u.%u.%u.%u", &ip4, &ip3, &ip2, &ip1);
            ip = (ip4 << 24) | (ip3 << 16) | (ip2 << 8) | (ip1);
            rule->external_ip = ip;
            rule->external_port = port;
            printf("External ip(u32) : %08x ; port : %hu\n", ip, port);

            sscanf(inter, "%[^:]:%hu", name, &port);
            sscanf(name, "%u.%u.%u.%u", &ip4, &ip3, &ip2, &ip1);
            ip = (ip4 << 24) | (ip3 << 16) | (ip2 << 8) | (ip1);
            rule->internal_ip = ip;
            rule->internal_port = port;
            printf("Internal ip(us3) : %08x ; port : %hu\n", ip, port);
        }
        else printf("config rules failed : %s .\n", type);
    }
    return 0;
}

// initialize
void nat_init(const char *config_file)
{
	memset(&nat, 0, sizeof(nat));

	for (int i = 0; i < HASH_8BITS; i++)
		init_list_head(&nat.nat_mapping_list[i]);

	init_list_head(&nat.rules);

	// seems unnecessary
	memset(nat.assigned_ports, 0, sizeof(nat.assigned_ports));

	parse_config(config_file);

	pthread_mutex_init(&nat.lock, NULL);

	pthread_create(&nat.thread, NULL, nat_timeout, NULL);
}

void nat_exit()
{
	//fprintf(stdout, "TODO: release all resources allocated.\n");
	pthread_mutex_lock(&nat.lock);

	for (int i = 0; i < HASH_8BITS; i++) {
		struct nat_mapping *entry, *q;
		list_for_each_entry_safe(entry, q, &nat.nat_mapping_list[i], list) {
			list_delete_entry(&entry->list);
			free(entry);
		}
	}

	pthread_kill(nat.thread, SIGTERM);
	pthread_mutex_unlock(&nat.lock);
}