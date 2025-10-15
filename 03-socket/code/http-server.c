#include <netinet/in.h>
#include <openssl/ssl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define HTTP_PORT 80
#define HTTPS_PORT 443

#define OK 200
#define NOT_FOUND 404
#define Partial_Content 206
#define Moved_Permanently 301

typedef struct Header {
    char *name;
    char *value;
    struct Header *next;
} Header;

typedef struct line {
    char method[8];
    char url[256];
    char version[16];
} Line;

typedef struct Request{
    Line line;
    Header *headers;
    char *body;
} Request;

void *listen_port(void *choose_port);
void handle_https_request(SSL* ssl);
void handle_http_request(int sock);
void decode_request(char *raw_request, Request *request);

int main()
{
    pthread_t http_thread;
    pthread_t https_thread;

    int http_port = HTTP_PORT;
    int https_port = HTTPS_PORT;

    if (pthread_create(&http_thread, NULL, listen_port, &http_port)) {
        perror("creat http thread error!\n");
        exit(1);
    }
    if (pthread_create(&https_thread, NULL, listen_port, &https_port)) {
        perror("creat https thread error!\n");
        exit(1);
    }

    pthread_join(http_thread, NULL);
    pthread_join(https_thread, NULL);

    return 0;
}

void *listen_port(void *choose_port)
{
    int port = *((int *)choose_port);

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
	addr.sin_port = htons(port);

	if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		perror("Bind failed");
		exit(1);
	}
	listen(sock, 10);

	while (1) {
		struct sockaddr_in caddr;
		socklen_t len = sizeof(caddr);
		int csock = accept(sock, (struct sockaddr*)&caddr, &len);
		if (csock < 0) {
			perror("Accept failed");
			exit(1);
		}
		if(port == HTTP_PORT){
            handle_http_request(csock);
            close(csock);
        }
        if(port == HTTPS_PORT){
            SSL *ssl = SSL_new(ctx); 
		    SSL_set_fd(ssl, csock);
		    handle_https_request(ssl);
            SSL_free(ssl);
            close(csock);
        }
	}

	close(sock);
	SSL_CTX_free(ctx);

    return NULL;
}

void handle_http_request(int sock)
{
    char *request = calloc(1024, sizeof(char));
    char *response = calloc(1024, sizeof(char));
    int request_len = 0;
    int response_len = 0;
    request_len = recv(sock, request, 1024, 0);

    if (request_len < 0) {
        perror("recv failed");
        exit(1);
    }

    Request *http_request = calloc(1, sizeof(Request));
    decode_request(request, http_request);

    // 301 Moved Permanently
    char new_url[256] = "https://10.0.0.1";
    strcat(new_url, http_request->line.url);

    response_len = sprintf(response, "%s %d Moved Permanently\r\nLocation: %s\r\n\r\n", http_request->line.version, Moved_Permanently, new_url);

    if (send(sock, response, response_len, 0) < 0) {
        perror("send failed");
        exit(1);
    }
}

void handle_https_request(SSL* ssl)
{   
    char *request = calloc(1024, sizeof(char));
    char *response = calloc(1024, sizeof(char));
    int request_len = 0;
    int response_len = 0;

    if (SSL_accept(ssl) == -1){
		perror("SSL_accept failed");
		exit(1);
	}

    request_len = SSL_read(ssl, request, 1024);
    if (request_len < 0) {
        perror("SSL_read failed");
        exit(1);
    }

    Request *http_request = calloc(1, sizeof(Request));
    decode_request(request, http_request);

    int option = 0; // 0: 200 OK, 1: 206 Partial Content
    FILE *file_pointer = NULL;

    for (Header *header = http_request->headers; header != NULL; header = header->next) {
        if (strcmp(header->name, "Range") == 0) {
            option = 1;
            break;
        }
    }

    // search file
    char file_path[256] = ".";
    char *file_path_pointer = file_path + 1;
    strcat(file_path, http_request->line.url);

    if ((file_pointer = fopen(file_path_pointer - 1, "r")) == NULL) {
        response_len = sprintf(response, "%s %d Not Found\r\n\r\n", http_request->line.version, NOT_FOUND);
        SSL_write(ssl, response, response_len);
    } else {
        if (option == 0) {
            // 200 OK
            fseek(file_pointer, 0, SEEK_END);
            int file_size = ftell(file_pointer);
            fseek(file_pointer, 0, SEEK_SET);

            response_len = sprintf(response, "%s %d OK\r\nContent-Length: %d\r\n\r\n", http_request->line.version, OK, file_size);
            SSL_write(ssl, response, response_len);

            char *file_buffer = (char *)malloc(file_size);
            fread(file_buffer, 1, file_size, file_pointer);
            SSL_write(ssl, file_buffer, file_size);
            free(file_buffer);
        } else if (option == 1) {
            // 206 Partial Content
            char range_value[64];
            for (Header *header = http_request->headers; header != NULL; header = header->next) {
                if (strcmp(header->name, "Range") == 0) {
                    strcpy(range_value, header->value);
                    break;
                }
            }

            int start, end;
            if (sscanf(range_value, "bytes=%d-%d", &start, &end) != 2) {
                sscanf(range_value, "bytes=%d-", &start);
                end = -1;
            }

            fseek(file_pointer, 0, SEEK_END);
            int file_size = ftell(file_pointer);
            if (end == -1 || end >= file_size) {
                end = file_size - 1;
            }
            int content_length = end - start + 1;
            fseek(file_pointer, start, SEEK_SET);

            response_len = sprintf(response, "%s %d Partial Content\r\nContent-Length: %d\r\nContent-Range: bytes %d-%d/%d\r\n\r\n", http_request->line.version, Partial_Content, content_length, start, end, file_size);
            SSL_write(ssl, response, response_len);

            char *file_buffer = (char *)malloc(content_length);
            fread(file_buffer, 1, content_length, file_pointer);
            SSL_write(ssl, file_buffer, content_length);
            free(file_buffer);
        }
        fclose(file_pointer);
    }

    return;
}

void decode_request(char *raw_request, Request *request)
{
    char *line_end = strstr(raw_request, "\r\n");
    sscanf(raw_request, "%s %s %s", request->line.method, request->line.url, request->line.version);

    char *header_start = line_end + 2;
    char *header_end;
    Header *current_header = NULL;

    while ((header_end = strstr(header_start, "\r\n")) != NULL && header_end != header_start) {
        Header *new_header = (Header *)malloc(sizeof(Header));
        new_header->next = NULL;

        char *colon_pos = strstr(header_start, ": ");
        if (colon_pos != NULL) {
            int name_len = colon_pos - header_start;
            int value_len = header_end - (colon_pos + 2);

            new_header->name = (char *)malloc(name_len + 1);
            new_header->value = (char *)malloc(value_len + 1);

            strncpy(new_header->name, header_start, name_len);
            new_header->name[name_len] = '\0';
            strncpy(new_header->value, colon_pos + 2, value_len);
            new_header->value[value_len] = '\0';

            if (current_header == NULL) {
                request->headers = new_header;
            } else {
                current_header->next = new_header;
            }
            current_header = new_header;
        }

        header_start = header_end + 2;
    }

    if (strcmp(request->line.method, "POST") == 0) {
        request->body = strdup(header_start);
    } else {
        request->body = NULL;
    }
}