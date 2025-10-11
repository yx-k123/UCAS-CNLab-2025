#include <ctype.h>
#include <errno.h>
#include <strings.h>
#include <unistd.h>
#include <malloc.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdbool.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <resolv.h>
#include "openssl/ssl.h"
#include "openssl/err.h"
#include <pthread.h>  // 新增

void *http_server(void *arg);
void *https_server(void *arg);
int read_headers_fd(int fd, char *buf, int cap);
int read_headers_ssl(SSL *ssl, char *buf, int cap);
void parse_req_line_host(const char *req, char *method, size_t msz, char *path, size_t psz, char *host, size_t hsz);
void send_404_ssl(SSL *ssl);
bool parse_range_header(const char *req, int *start, int *end);

int main()
{   
    // init SSL Library
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();

    // enable TLS method
    const SSL_METHOD *method = TLS_server_method();
    SSL_CTX *ctx = SSL_CTX_new(method);

    // load certificate and private key
	if (SSL_CTX_use_certificate_file(ctx, "./keys/cnlab.cert", SSL_FILETYPE_PEM) <= 0) {
		perror("load cert failed");
		exit(1);
	}
	if (SSL_CTX_use_PrivateKey_file(ctx, "./keys/cnlab.prikey", SSL_FILETYPE_PEM) <= 0) {
		perror("load prikey failed");
		exit(1);
	}

    // create two threads for http and https server
    pthread_t t_http_server, t_https_server;
    pthread_create(&t_http_server, NULL, http_server, ctx);
    pthread_create(&t_https_server, NULL, https_server, ctx);

    pthread_join(t_http_server, NULL);
    pthread_join(t_https_server, NULL);
    return 0;
}

void *http_server(void *arg)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Opening socket failed");
        exit(1);
    }
    int enable = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        exit(1);
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Binding socket failed");
        exit(1);
    }
    if (listen(sock, 10) < 0) {
        perror("Listening on socket failed");
        exit(1);
    }
    printf("HTTP server listening on port 80\n");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock < 0) {
            perror("Accepting connection failed");
            continue;
        }

        // always redirect to HTTPS
        char request[4096] = {0};
        read_headers_fd(client_sock, request, sizeof(request));
        char method[16], path[1024], host[256];
        parse_req_line_host(request, method, sizeof(method), path, sizeof(path), host, sizeof(host));
        if (path[0]==0) strcpy(path, "/");
        char response[2048];
        snprintf(response, sizeof(response),
                 "HTTP/1.1 301 Moved Permanently\r\n"
                 "Location: https://%s%s\r\n"
                 "Content-Length: 0\r\n"
                 "Connection: close\r\n"
                 "\r\n", host, path);
        send(client_sock, response, strlen(response), 0);
        close(client_sock);
    }
    close(sock);
}

void *https_server(void *arg)
{
    SSL_CTX *ctx = (SSL_CTX *)arg;
    // init socket, listening to port 443
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Opening socket failed");
        exit(1);
    }
    int enable = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        exit(1);
    }  
    struct sockaddr_in addr;
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(443);
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        exit(1);
    }
    if (listen(sock, 10) < 0) {
        perror("Listening on socket failed");
        exit(1);
    }
    printf("HTTPS server listening on port 443\n"); 

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);           
        int client_sock = accept(sock, (struct sockaddr*)&client_addr, &len);
        if (client_sock < 0) {
            perror("Accept failed");
            exit(1);
        }
        SSL *ssl = SSL_new(ctx);
        SSL_set_fd(ssl, client_sock);
        if (SSL_accept(ssl) <= 0) {
            ERR_print_errors_fp(stderr);
            SSL_free(ssl);
            close(client_sock);
            continue;
        }

        char request[4096] = {0};
        read_headers_ssl(ssl, request, sizeof(request));
        char method[16]={0}, url[1024]={0}, host[256]={0};
        parse_req_line_host(request, method, sizeof(method), url, sizeof(url), host, sizeof(host));

        if (strcasecmp(method, "GET") != 0) {
            send_404_ssl(ssl);
        } else {
            if (url[0]==0) strcpy(url, "/");
            const char *p = url[0] == '/' ? url + 1 : url;
            const char *base = strrchr(p, '/');
            base = base ? base + 1 : p;
            char filepath[2048] = {0};
            if (*base == '\0') {
                snprintf(filepath, sizeof(filepath), "./index.html");
            } else {
                if (strstr(base, "..")) {
                    snprintf(filepath, sizeof(filepath), "./index.html");
                } else {
                    snprintf(filepath, sizeof(filepath), "./%s", base);
                }
            }

            FILE *fp = fopen(filepath, "rb");
            if (!fp) {
                send_404_ssl(ssl);
            } else {
                fseek(fp, 0, SEEK_END);
                long fsize = ftell(fp);
                fseek(fp, 0, SEEK_SET);

                int range_start = 0, range_end = (int)fsize - 1;
                bool is_partial = parse_range_header(request, &range_start, &range_end);
                if (is_partial) {
                    if (range_start < 0) range_start = 0;
                    if (range_end < 0 || range_end >= fsize) range_end = (int)fsize - 1;
                    if (range_start > range_end) {
                        send_404_ssl(ssl);
                        fclose(fp);
                        SSL_shutdown(ssl);
                        SSL_free(ssl);
                        close(client_sock);
                        continue;
                    }
                }
                long content_length = range_end - range_start + 1;

                char header[2048];
                if (is_partial) {
                    snprintf(header, sizeof(header),
                             "HTTP/1.1 206 Partial Content\r\n"
                             "Content-Length: %ld\r\n"
                             "Content-Range: bytes %d-%d/%ld\r\n"
                             "Connection: close\r\n"
                             "\r\n",
                             content_length, range_start, range_end, fsize);
                } else {
                    snprintf(header, sizeof(header),
                             "HTTP/1.1 200 OK\r\n"
                             "Content-Length: %ld\r\n"
                             "Connection: close\r\n"
                             "\r\n",
                             content_length);
                }
                SSL_write(ssl, header, strlen(header));

                fseek(fp, range_start, SEEK_SET);
                char buffer[8192];
                long to_send = content_length;
                while (to_send > 0) {
                    size_t n = fread(buffer, 1, to_send > sizeof(buffer) ? sizeof(buffer) : to_send, fp);
                    if (n <= 0) break;
                    SSL_write(ssl, buffer, (int)n);
                    to_send -= (long)n;
                }
                fclose(fp);
            }
        }
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(client_sock);
    }

    close(sock);
    SSL_CTX_free(ctx);
    return 0;
}

int read_headers_fd(int fd, char *buf, int cap) {
    int n = 0;
    while (n < cap - 1) {
        int r = recv(fd, buf + n, cap - 1 - n, 0);
        if (r <= 0) break;
        n += r; buf[n] = 0;
        if (strstr(buf, "\r\n\r\n")) break;
    }
    return n;
}

int read_headers_ssl(SSL *ssl, char *buf, int cap) {
    int n = 0;
    while (n < cap - 1) {
        int r = SSL_read(ssl, buf + n, cap - 1 - n);
        if (r <= 0) break;
        n += r; buf[n] = 0;
        if (strstr(buf, "\r\n\r\n")) break;
    }
    return n;
}

void parse_req_line_host(const char *req, char *method, size_t msz, char *path, size_t psz, char *host, size_t hsz) {
    if (method && msz) method[0] = 0;
    if (path && psz) path[0] = 0;
    if (host && hsz) { strncpy(host, "localhost", hsz - 1); host[hsz - 1] = 0; }

    const char *line_end = strstr(req, "\r\n");
    if (line_end) {
        char line[2048] = {0};
        size_t len = (size_t)(line_end - req);
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, req, len);
        sscanf(line, "%15s %1023s", method, path);
    }

    const char *host_start = strcasestr(req, "\nHost:");
    if (!host_start) host_start = strcasestr(req, "\r\nHost:");
    if (host_start && host) {
        host_start = strchr(host_start, ':');
        if (host_start) sscanf(host_start + 1, " %255[^\r\n]", host);
    }
}

// 将 Host 规范化为 https URL：去掉 :80，默认不带端口
static void build_https_location(const char *host_in, const char *path_in, char *out, size_t outsz) {
    char host[256] = {0};
    if (host_in && *host_in) {
        // 去空白
        while (*host_in == ' ' || *host_in == '\t') host_in++;
        strncpy(host, host_in, sizeof(host) - 1);
        host[sizeof(host) - 1] = 0;
        // 去掉结尾的端口 :80
        char *colon = strrchr(host, ':');
        if (colon) {
            int port = atoi(colon + 1);
            if (port == 80) *colon = '\0';
        }
    } else {
        strcpy(host, "localhost");
    }
    const char *path = (path_in && *path_in) ? path_in : "/";
    snprintf(out, outsz, "https://%s%s", host, path);
}

// 路径拼接：保留子目录，禁止 ..，目录自动补 index.html
static void build_safe_path(const char *url_path, char *out, size_t outsz) {
    if (!url_path || url_path[0] == '\0') {
        snprintf(out, outsz, "./index.html");
        return;
    }
    char tmp[2048] = {0};
    // 去掉开头的 /
    if (url_path[0] == '/') {
        strncpy(tmp, url_path + 1, sizeof(tmp) - 1);
    } else {
        strncpy(tmp, url_path, sizeof(tmp) - 1);
    }
    // 禁止 ..
    if (strstr(tmp, "..")) {
        snprintf(out, outsz, "./index.html");
        return;
    }
    // 目录 -> 追加 index.html
    size_t len = strlen(tmp);
    if (len == 0 || tmp[len - 1] == '/') {
        if (len < sizeof(tmp) - strlen("index.html") - 1) {
            strcat(tmp, "index.html");
        }
    }
    snprintf(out, outsz, "./%s", tmp);
}

// 简单 Content-Type（只区分 html/txt/其他）
static const char* guess_content_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (!strcasecmp(ext, ".html") || !strcasecmp(ext, ".htm")) return "text/html";
    if (!strcasecmp(ext, ".txt")) return "text/plain";
    return "application/octet-stream";
}

bool parse_range_header(const char *req, int *start, int *end) {
    const char *h = NULL;
    if (!strncasecmp(req, "Range:", 6))          h = req + 6;
    else if ((h = strcasestr(req, "\r\nRange:"))) h += 8;
    else if ((h = strcasestr(req, "\nRange:")))   h += 7;
    else return false;

    while (*h==' '||*h=='\t') h++;
    if (strncasecmp(h, "bytes=", 6) != 0) return false;
    h += 6;

    char *p = (char*)h;
    errno = 0;
    long s = strtol(p, &p, 10);
    if (p == h || errno == ERANGE || s < 0) return false;

    if (*p != '-') return false;
    p++;

    long e = -1;
    if (isdigit((unsigned char)*p)) {
        errno = 0;
        e = strtol(p, &p, 10);
        if (errno == ERANGE || e < s) return false;
    }
    *start = (int)s;
    *end   = (int)e;
    return true;
}

void send_404_ssl(SSL *ssl) {
    const char *response = "HTTP/1.1 404 Not Found\r\nContent-Length: 13\r\nConnection: close\r\n\r\n404 Not Found";
    SSL_write(ssl, response, strlen(response));
}
