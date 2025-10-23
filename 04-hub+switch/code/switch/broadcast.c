#include "include/base.h"
#include <stdio.h>
#include <string.h>

// XXX ifaces are stored in instace->iface_list
extern ustack_t *instance;

extern void iface_send_packet(iface_info_t *iface, const char *packet, int len);

void broadcast_packet(iface_info_t *iface, const char *packet, int len)
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
