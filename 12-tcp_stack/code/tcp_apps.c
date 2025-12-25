#include "include/tcp_sock.h"

#include "include/log.h"

#include <unistd.h>

// tcp server application, listens to port (specified by arg) and serves only one
// connection request
void *tcp_server(void *arg)
{
    u16 port = *(u16 *)arg;
    struct tcp_sock *tsk = alloc_tcp_sock();

    struct sock_addr addr;
    addr.ip = htonl(0);
    addr.port = port;
    if (tcp_sock_bind(tsk, &addr) < 0) {
        log(ERROR, "tcp_sock bind to port %hu failed", ntohs(port));
        exit(1);
    }

    if (tcp_sock_listen(tsk, 3) < 0) {
        log(ERROR, "tcp_sock listen failed");
        exit(1);
    }

    log(DEBUG, "listen to port %hu.", ntohs(port));

    struct tcp_sock *csk = tcp_sock_accept(tsk);

    log(DEBUG, "accept a connection.");

    char buf[1000];  // 接收缓冲区
    while (1) {
        int recv_len = tcp_sock_read(csk, buf, sizeof(buf) - 1);
        if (recv_len > 0) {
            buf[recv_len] = '\0';
            // 添加前缀 "server echoes: "
            char response[1024];
            sprintf(response, "server echoes: %s", buf);
            tcp_sock_write(csk, response, strlen(response));
        } else if (recv_len == 0) {
            // 连接关闭
            break;
        }
    }

    tcp_sock_close(csk);
    return NULL;
}

// tcp client application, connects to server (ip:port specified by arg), each
// time sends one bulk of data and receives one bulk of data 
void *tcp_client(void *arg)
{
    struct sock_addr *skaddr = arg;
    struct tcp_sock *tsk = alloc_tcp_sock();

    if (tcp_sock_connect(tsk, skaddr) < 0) {
        log(ERROR, "tcp_sock connect to server ("IP_FMT":%hu) failed.", \
                NET_IP_FMT_STR(skaddr->ip), ntohs(skaddr->port));
        exit(1);
    }

    char data[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int data_len = strlen(data);
    char buf[1000];  // 接收缓冲区

    for (int i = 0; i < 10; i++) {  // 发送 10 次（不少于 5 次）
        // 构造字符串：data[i:] + data[:i+1]
        char new_data[100];
        int len1 = data_len - i;
        int len2 = i + 1;
        memcpy(new_data, data + i, len1);
        memcpy(new_data + len1, data, len2);
        new_data[len1 + len2] = '\0';
		log(DEBUG, "Client sending: %s", new_data);
        // 发送数据
        tcp_sock_write(tsk, new_data, strlen(new_data));

        // 接收响应
        int recv_len = tcp_sock_read(tsk, buf, sizeof(buf) - 1);
        if (recv_len > 0) {
            buf[recv_len] = '\0';
            log(DEBUG, "Received: %s", buf);
        }

        sleep(1);  // 间隔 1 秒
    }

    tcp_sock_close(tsk);
    return NULL;
}

