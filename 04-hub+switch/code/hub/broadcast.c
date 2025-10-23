#include "include/base.h"
#include <stdio.h>
#include <string.h>

extern ustack_t *instance;

// the memory of ``packet'' will be free'd in handle_packet().
void broadcast_packet(iface_info_t *iface, const char *packet, int len)  // 广播数据包
{
	// TODO: broadcast packet 
	// fprintf(stdout, "TODO: broadcast packet.\n");
	iface_info_t *itf = NULL;
	list_for_each_entry(itf, &instance->iface_list, list) {
		if (strcmp(itf->name, iface->name) != 0) {
			iface_send_packet(itf, packet, len);
			// log(DEBUG, "broadcast packet to %s", itf->name);
		}
	}	
}
