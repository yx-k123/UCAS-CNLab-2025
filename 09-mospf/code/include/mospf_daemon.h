#ifndef __MOSPF_DAEMON_H__
#define __MOSPF_DAEMON_H__

#include "base.h"
#include "types.h"
#include "list.h"

typedef struct {
    u32 rid;            // Router ID
    u32 dist;           // 距离
    int visited;        // 是否已访问
    u32 next_hop;       // 下一跳 IP (对于第一跳)
    iface_info_t *iface;// 出接口 (对于第一跳)
} dijkstra_node_t;

#define MAX_NODES 10

void mospf_init();
void mospf_run();
void handle_mospf_packet(iface_info_t *iface, char *packet, int len);
void *sending_mospf_hello_thread(void *param);
void *sending_mospf_lsu_thread(void *param);
void *checking_nbr_thread(void *param);
void *checking_database_thread(void *param);
void sending_mospf_lsu();
void update_rtable();
void print_mospf_db();

#endif
