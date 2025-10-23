#include "include/headers.h"
#include "include/base.h"
#include "include/ether.h"
#include "include/log.h"

#include <sys/types.h>
#include <ifaddrs.h>

void handle_packet(iface_info_t *iface, char *packet, int len)
{
	broadcast_packet(iface, packet, len);
	free(packet);
}

void ustack_run()
{
	struct sockaddr_ll addr;  // 原始套接字地址结构体
	socklen_t addr_len = sizeof(addr);  // 存储地址结构体的长度，用于 recvfrom 函数
	char buf[ETH_FRAME_LEN];  // 数据缓冲区
	int len;  // 接收数据包的长度

	while (1) {
		int ready = poll(instance->fds, instance->nifs, -1);
		if (ready < 0) {
			perror("Poll failed!");
			break;
		}
		else if (ready == 0)  // 无事发生 >.<
			continue;

		for (int i = 0; i < instance->nifs; i++) {
			if (instance->fds[i].revents & POLLIN) {
				len = recvfrom(instance->fds[i].fd, buf, ETH_FRAME_LEN, 0, \
						(struct sockaddr*)&addr, &addr_len);  // 接收数据包
				if (len <= 0) {
					// log(ERROR, "receive packet error: %s", strerror(errno));
				}
				else if (addr.sll_pkttype == PACKET_OUTGOING) {
					// XXX: Linux raw socket will capture both incoming and
					// outgoing packets, while we only care about the incoming ones.

					// log(DEBUG, "received packet which is sent from the "
					// 		"interface itself, drop it.");
				}
				else {
					iface_info_t *iface = fd_to_iface(instance->fds[i].fd);
					if (!iface) 
						continue;

					char *packet = malloc(len);
					if (!packet) {
						// log(ERROR, "malloc failed when receiving packet.");
						continue;
					}
					memcpy(packet, buf, len);
					handle_packet(iface, packet, len);
				}
			}
		}
	}
}

int main(int argc, const char **argv)
{
	if (getuid() && geteuid()) {
		printf("Permission denied, should be superuser!\n");
		exit(1);
	}   // 确定是超级用户

	init_ustack();

	ustack_run();

	return 0;
}
