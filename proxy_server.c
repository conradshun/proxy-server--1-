#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <windows.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define close closesocket
    #define ssize_t int
    typedef int socklen_t;
    #define strdup _strdup
    #define strncasecmp _strnicmp
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <sys/wait.h>
    #include <signal.h>
    #include <errno.h>
#endif

#define BUFFER_SIZE 4096
#define MAX_REQUEST_SIZE 8192

// Function prototypes
#ifdef _WIN32
DWORD WINAPI handle_client(LPVOID lpParam);
#else
void handle_client(int client_socket);
#endif
int parse_http_request(char *request, char *method, char *host, char *path, int *port);
int connect_to_server(const char *host, int port);
void send_error_response(int socket, int error_code, const char *message);

#ifndef _WIN32
void sigchld_handler(int sig);
#endif

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port number\n");
        exit(1);
    }

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        exit(1);
    }
#else
    signal(SIGCHLD, sigchld_handler);
#endif

    // Create socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("socket creation failed");
        exit(1);
    }

    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        exit(1);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        exit(1);
    }

    if (listen(server_socket, 10) < 0) {
        perror("listen failed");
        exit(1);
    }

    printf("Server listening on port %d\n", port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
#ifndef _WIN32
            if (errno == EINTR) continue;
#endif
            perror("accept failed");
            continue;
        }

        printf("Client connected from %s:%d\n",
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

#ifdef _WIN32
        HANDLE thread = CreateThread(NULL, 0, handle_client, (LPVOID)(intptr_t)client_socket, 0, NULL);
        if (thread) {
            CloseHandle(thread);
        } else {
            fprintf(stderr, "Failed to create thread\n");
            close(client_socket);
        }
#else
        pid_t pid = fork();
        if (pid == 0) {
            close(server_socket);
            handle_client(client_socket);
            close(client_socket);
            exit(0);
        } else if (pid > 0) {
            close(client_socket);
        } else {
            perror("fork failed");
            close(client_socket);
        }
#endif
    }

    close(server_socket);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}

#ifdef _WIN32
DWORD WINAPI handle_client(LPVOID lpParam) {
    int client_socket = (int)(intptr_t)lpParam;
#else
void handle_client(int client_socket) {
#endif
    char request[MAX_REQUEST_SIZE];
    char method[16], host[256], path[1024];
    int port;

    ssize_t bytes_received = recv(client_socket, request, sizeof(request) - 1, 0);
    if (bytes_received <= 0) {
        send_error_response(client_socket, 400, "Bad Request");
        return 0;
    }

    request[bytes_received] = '\0';
    printf("Received request:\n%s\n", request);

    if (parse_http_request(request, method, host, path, &port) != 0) {
        send_error_response(client_socket, 400, "Bad Request");
        return 0;
    }

    printf("Parsed: Method=%s, Host=%s, Port=%d, Path=%s\n",
           method, host, port, path);

    // --- Serve local responses if this is for ourselves ---
    if (strcmp(host, "localhost") == 0 && port == 8080) {
        if (strcmp(path, "/favicon.ico") == 0) {
            const char *favicon_resp =
                "HTTP/1.1 204 No Content\r\n"
                "Connection: close\r\n\r\n";
            send(client_socket, favicon_resp, strlen(favicon_resp), 0);
#ifdef _WIN32
            close(client_socket);
#endif
            return 0;
        }

        const char *body = "<html><body><h1>67 proxy server</h1></body></html>";
        char response[512];
        snprintf(response, sizeof(response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            strlen(body), body);

        send(client_socket, response, strlen(response), 0);
#ifdef _WIN32
        close(client_socket);
#endif
        return 0;
    }

    // --- Normal proxy behavior for other hosts ---
    char *rest_of_request = strstr(request, "\r\n");
    if (!rest_of_request) {
        send_error_response(client_socket, 400, "Bad Request");
        return 0;
    }
    rest_of_request += 2;

    char new_request[MAX_REQUEST_SIZE];
    int new_request_len = snprintf(new_request, sizeof(new_request),
        "%s %s HTTP/1.1\r\n%s", method, path, rest_of_request);

    int server_socket = connect_to_server(host, port);
    if (server_socket < 0) {
        send_error_response(client_socket, 502, "Bad Gateway");
        return 0;
    }

    if (send(server_socket, new_request, new_request_len, 0) < 0) {
        perror("Failed to send request to server");
        close(server_socket);
        send_error_response(client_socket, 502, "Bad Gateway");
        return 0;
    }

    char buffer[BUFFER_SIZE];
    ssize_t bytes;
    while ((bytes = recv(server_socket, buffer, sizeof(buffer), 0)) > 0) {
        if (send(client_socket, buffer, bytes, 0) < 0) {
            perror("Failed to send response to client");
            break;
        }
    }

    close(server_socket);
#ifdef _WIN32
    close(client_socket);
#endif
    return 0;
}

int parse_http_request(char *request, char *method, char *host, char *path, int *port) {
    char *request_line_end = strstr(request, "\r\n");
    if (!request_line_end) return -1;

    char request_line[1024];
    size_t line_len = request_line_end - request;
    if (line_len >= sizeof(request_line)) return -1;
    strncpy(request_line, request, line_len);
    request_line[line_len] = '\0';

    char url[1024];
    if (sscanf(request_line, "%15s %1023s", method, url) != 2) {
        return -1;
    }

    strcpy(path, "/");
    *port = 80;
    host[0] = '\0';

    if (strncmp(url, "http://", 7) == 0) {
        char *url_start = url + 7;
        char *path_start = strchr(url_start, '/');
        if (path_start) {
            strcpy(path, path_start);
            *path_start = '\0';
        } else {
            strcpy(path, "/");
        }
        char *port_start = strchr(url_start, ':');
        if (port_start) {
            *port_start = '\0';
            *port = atoi(port_start + 1);
            strcpy(host, url_start);
        } else {
            strcpy(host, url_start);
        }
    } else {
        strcpy(path, url);
        char *headers = strdup(request);
        char *line = strtok(headers, "\r\n");
        while ((line = strtok(NULL, "\r\n")) != NULL) {
            if (strncasecmp(line, "Host:", 5) == 0) {
                char *host_value = line + 5;
                while (*host_value == ' ') host_value++;
                char *port_start = strchr(host_value, ':');
                if (port_start) {
                    *port_start = '\0';
                    *port = atoi(port_start + 1);
                }
                strcpy(host, host_value);
                break;
            }
        }
        free(headers);
    }

    if (strlen(host) == 0) {
        return -1;
    }

    return 0;
}

int connect_to_server(const char *host, int port) {
    struct addrinfo hints, *result, *rp;
    char port_str[16];
    int sock = -1;

    snprintf(port_str, sizeof(port_str), "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port_str, &hints, &result) != 0) {
        fprintf(stderr, "Failed to resolve host: %s\n", host);
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock < 0) continue;
        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(sock);
        sock = -1;
    }

    freeaddrinfo(result);
    return sock;
}

void send_error_response(int socket, int error_code, const char *message) {
    char response[512];
    snprintf(response, sizeof(response),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<html><body><h1>%d %s</h1></body></html>",
        error_code, message, (unsigned)(strlen(message) + 38), error_code, message);
    send(socket, response, strlen(response), 0);
}

#ifndef _WIN32
void sigchld_handler(int sig) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}
#endif
