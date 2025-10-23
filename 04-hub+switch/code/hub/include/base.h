#ifndef __BASE_H__
#define __BASE_H__

#include "types.h"
#include "ether.h"
#include "list.h"

#include <arpa/inet.h>

typedef struct {
	struct list_head iface_list;  	// 链表头，存储所有网络接口的信息
	int nifs;                     	// 网络接口的数量
	struct pollfd *fds;           	// 用于监听网络接口事件的 pollfd 数组
} ustack_t;  

extern ustack_t *instance;        	// 全局变量，指向 ustack_t 实例

typedef struct {
	struct list_head list;  	  	// 链表节点，用于将该接口加入 iface_list 链表
	
	int fd;                 	  	// 文件描述符，标识该接口的原始套接字
	int index;              		// 接口索引，唯一标识该网络接口
	u8 mac[ETH_ALEN];       		// 接口的 MAC 地址
	char name[16];          		// 接口名称（如 "eth0"）
} iface_info_t;  					// 网络接口信息结构体

void init_ustack();
iface_info_t *fd_to_iface(int fd);
void iface_send_packet(iface_info_t *iface, const char *packet, int len);

void broadcast_packet(iface_info_t *iface, const char *packet, int len);

#endif
