#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

#define BUFFER_SIZE 4096
#define MAX_REQUEST_SIZE 8192

// Function prototypes
void handle_client(int client_socket);
int parse_http_request(char *request, char *method, char *host, char *path, int *port);
int connect_to_server(char *host, int port);
void send_error_response(int socket, int error_code, char *message);
void sigchld_handler(int sig);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    int proxy_port = atoi(argv[1]);
    if (proxy_port <= 0 || proxy_port > 65535) {
        fprintf(stderr, "Invalid port number: %s\n", argv[1]);
        exit(1);
    }

    // Set up signal handler for child processes
    signal(SIGCHLD, sigchld_handler);

    // Create socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    // Set socket options to reuse address
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        exit(1);
    }

    // Set up server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(proxy_port);

    // Bind socket
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_socket);
        exit(1);
    }

    // Listen for connections
    if (listen(server_socket, 10) < 0) {
        perror("Listen failed");
        close(server_socket);
        exit(1);
    }

    printf("Proxy server listening on port %d...\n", proxy_port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        // Accept client connection
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            if (errno == EINTR) continue; // Interrupted by signal
            perror("Accept failed");
            continue;
        }

        printf("Client connected from %s:%d\n", 
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // Fork a child process to handle the client
        pid_t pid = fork();
        if (pid == 0) {
            // Child process
            close(server_socket); // Child doesn't need the listening socket
            handle_client(client_socket);
            close(client_socket);
            exit(0);
        } else if (pid > 0) {
            // Parent process
            close(client_socket); // Parent doesn't need the client socket
        } else {
            perror("Fork failed");
            close(client_socket);
        }
    }

    close(server_socket);
    return 0;
}

void handle_client(int client_socket) {
    char request[MAX_REQUEST_SIZE];
    char method[16], host[256], path[1024];
    int port;
    
    // Read HTTP request from client
    ssize_t bytes_received = recv(client_socket, request, sizeof(request) - 1, 0);
    if (bytes_received <= 0) {
        send_error_response(client_socket, 400, "Bad Request");
        return;
    }
    
    request[bytes_received] = '\0';
    printf("Received request:\n%s\n", request);

    // Parse HTTP request
    if (parse_http_request(request, method, host, path, &port) != 0) {
        send_error_response(client_socket, 400, "Bad Request - Invalid HTTP format");
        return;
    }

    printf("Parsed: Method=%s, Host=%s, Port=%d, Path=%s\n", method, host, port, path);

    // Connect to the target server
    int server_socket = connect_to_server(host, port);
    if (server_socket < 0) {
        send_error_response(client_socket, 502, "Bad Gateway - Cannot connect to server");
        return;
    }

    // Forward the request to the server
    if (send(server_socket, request, bytes_received, 0) < 0) {
        perror("Failed to send request to server");
        close(server_socket);
        send_error_response(client_socket, 502, "Bad Gateway - Failed to forward request");
        return;
    }

    // Relay data between client and server
    fd_set read_fds;
    char buffer[BUFFER_SIZE];
    int max_fd = (client_socket > server_socket) ? client_socket : server_socket;

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(client_socket, &read_fds);
        FD_SET(server_socket, &read_fds);

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0) {
            perror("Select failed");
            break;
        }

        // Data from server to client
        if (FD_ISSET(server_socket, &read_fds)) {
            ssize_t bytes = recv(server_socket, buffer, sizeof(buffer), 0);
            if (bytes <= 0) break;
            
            if (send(client_socket, buffer, bytes, 0) < 0) {
                perror("Failed to send data to client");
                break;
            }
        }

        // Data from client to server
        if (FD_ISSET(client_socket, &read_fds)) {
            ssize_t bytes = recv(client_socket, buffer, sizeof(buffer), 0);
            if (bytes <= 0) break;
            
            if (send(server_socket, buffer, bytes, 0) < 0) {
                perror("Failed to send data to server");
                break;
            }
        }
    }

    close(server_socket);
    printf("Connection closed\n");
}

int parse_http_request(char *request, char *method, char *host, char *path, int *port) {
    char *line = strtok(request, "\r\n");
    if (!line) return -1;

    // Parse request line: METHOD URL HTTP/1.x
    char url[1024];
    if (sscanf(line, "%15s %1023s", method, url) != 2) {
        return -1;
    }

    // Initialize default values
    strcpy(path, "/");
    *port = 80;
    host[0] = '\0';

    // Parse URL
    if (strncmp(url, "http://", 7) == 0) {
        // Full URL format: http://host:port/path
        char *url_start = url + 7;
        char *path_start = strchr(url_start, '/');
        
        if (path_start) {
            strcpy(path, path_start);
            *path_start = '\0';
        }
        
        char *port_start = strchr(url_start, ':');
        if (port_start) {
            *port = atoi(port_start + 1);
            *port_start = '\0';
        }
        
        strcpy(host, url_start);
    } else {
        // Relative URL - need to find Host header
        strcpy(path, url);
        
        // Look for Host header
        char *request_copy = strdup(request);
        char *line = strtok(request_copy, "\r\n");
        
        while ((line = strtok(NULL, "\r\n")) != NULL) {
            if (strncasecmp(line, "Host:", 5) == 0) {
                char *host_value = line + 5;
                while (*host_value == ' ') host_value++; // Skip spaces
                
                char *port_start = strchr(host_value, ':');
                if (port_start) {
                    *port = atoi(port_start + 1);
                    *port_start = '\0';
                }
                
                strcpy(host, host_value);
                break;
            }
        }
        
        free(request_copy);
    }

    if (strlen(host) == 0) {
        return -1; // No host found
    }

    return 0;
}

int connect_to_server(char *host, int port) {
    struct hostent *server_info = gethostbyname(host);
    if (!server_info) {
        fprintf(stderr, "Failed to resolve host: %s\n", host);
        return -1;
    }

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Failed to create server socket");
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    memcpy(&server_addr.sin_addr.s_addr, server_info->h_addr, server_info->h_length);

    if (connect(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Failed to connect to server");
        close(server_socket);
        return -1;
    }

    return server_socket;
}

void send_error_response(int socket, int error_code, char *message) {
    char response[512];
    snprintf(response, sizeof(response),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<html><body><h1>%d %s</h1></body></html>",
        error_code, message, strlen(message) + 38, error_code, message);
    
    send(socket, response, strlen(response), 0);
}

void sigchld_handler(int sig) {
    // Clean up zombie child processes
    while (waitpid(-1, NULL, WNOHANG) > 0);
}
