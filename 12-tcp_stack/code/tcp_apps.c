#include "include/tcp_sock.h"

#include "include/log.h"

#include <stdio.h>
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

	FILE *fp = fopen("server-output.dat", "w+");
    log(DEBUG, "start to receive data.");
    char buffer[1000];
    int len_read, len_write;
    while (1) {
        len_read = tcp_sock_read(csk, buffer, sizeof(buffer));
        // log(DEBUG, "read %d bytes data from tcp sock.", len_read);
        if (len_read <= 0) {
            break;
        } else if (len_read > 0) {
            len_write = fwrite(buffer, sizeof(char), len_read, fp);
        }
    }
    fclose(fp);

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

    FILE *fp = fopen("client-input.dat", "r");
    log(DEBUG, "start to send data.");
    char *data = (char *)malloc(10000000*sizeof(char));
    if (!data) {
        log(ERROR, "malloc data buffer failed.");
        exit(1);
    }
    int data_len = 0;
    while ((data[data_len++] = fgetc(fp)) != EOF);
    data_len --;
    log(DEBUG, "data length = %d.", data_len);
    fclose(fp);
    tcp_sock_write(tsk, data, data_len);
    free(data);
    tcp_sock_close(tsk);
    
    return NULL;
}

