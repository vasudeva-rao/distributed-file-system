/*
 * S1 Server Implementation for Distributed File System
 * 
 * This server acts as the main coordinator in a distributed file system with the following roles:
 * - Handles direct storage and management of .c files
 * - Coordinates with S2 server for .pdf files
 * - Coordinates with S3 server for .txt files
 * - Coordinates with S4 server for .zip files
 * - Manages client connections and command routing
 * - Implements file operations: upload, download, remove, tar creation, and directory listing
 */

#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <pwd.h>
#include <time.h>
#include <ctype.h>
#include <stdarg.h>
/* Define constants */
#define MAX_BUFFER 1024
#define MAX_PATH 256
#define MAX_FILENAME 128
#define MAX_CMD 64
#define MIN(a,b) ((a) < (b) ? (a) : (b))

/* Port numbers */
#define S1_PORT 5600
#define S2_PORT 5601
#define S3_PORT 5602
#define S4_PORT 5603

/* Server connections */
#define NUM_SERVERS 3
static int server_sockets[NUM_SERVERS] = {-1, -1, -1};

/* Command types */
#define CMD_UPLOADF "uploadf"
#define CMD_DOWNLF "downlf"
#define CMD_REMOVEF "removef"
#define CMD_DOWNLTAR "downltar"
#define CMD_DISPFNAMES "dispfnames"

/* File types */
#define FILE_C ".c"
#define FILE_PDF ".pdf"
#define FILE_TXT ".txt"
#define FILE_ZIP ".zip"

/* Error codes */
#define SUCCESS 0
#define ERR_SOCKET -1
#define ERR_BIND -3
#define ERR_LISTEN -4
#define ERR_ACCEPT -5
#define ERR_FILE_NOT_FOUND -6
#define ERR_INVALID_CMD -7
#define ERR_TRANSFER -9

/* Structure for command */
struct command {
    char cmd_type[MAX_CMD];
    char arg1[MAX_PATH];
    char arg2[MAX_PATH];
};

/* Global variables */
static int server_fd = -1;

/* Function declarations */
int init_server(int port);
int connect_to_server(int port);
int accept_connection(int server_fd);
int send_file(int sock_fd, const char *filepath);
int receive_file(int sock_fd, const char *filepath);
int parse_command(char *cmd_str, struct command *cmd);
void process_client(int client_fd);
int create_directory(const char *path);
void log_error(const char *msg);
int validate_path(const char *path);
void cleanup(int signum);
int connect_to_servers(void);
int get_server_for_file(const char *filename);
void forward_command(int server_sock, const struct command *cmd);
char* expand_path(const char* path, char* expanded, size_t size);
void create_server_directories(void);
char* map_s1_path(const char* path, char* mapped, size_t size);
int forward_remove_command(int client_fd, const char *filepath, int server_num);
char* map_path_to_server(const char* path, char* mapped, size_t size, int server_num);
int create_c_files_tar(const char *tar_path);
int request_server_tar(int server_num, const char *filetype, const char *temp_path);
int send_command(int sock_fd, const char *format, ...);
int receive_response(int sock_fd, char *buffer, size_t size);

/* Main function */
int main() {
    struct sigaction sa = {.sa_handler = cleanup, .sa_flags = 0};
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    /* Create server directories */
    create_server_directories();
    
    /* Initialize server */
    if ((server_fd = init_server(S1_PORT)) < 0) {
        log_error("Failed to initialize server");
        return 1;
    }
    
    /* Connect to other servers */
    if (connect_to_servers() != SUCCESS) {
        log_error("Failed to connect to all servers");
        cleanup(SIGTERM);
        return 1;
    }
    
    printf("Main Server (S1) started on port %d\n", S1_PORT);
    
    /* Main server loop */
    while (1) {
        int client_fd = accept_connection(server_fd);
        if (client_fd < 0) {
            log_error("Failed to accept connection");
            continue;
        }
        
        /* Fork child process to handle client */
        pid_t pid = fork();
        if (pid < 0) {
            log_error("Failed to fork process");
            close(client_fd);
        } else if (pid == 0) {  /* Child process */
            close(server_fd);
            process_client(client_fd);
            exit(0);
        } else {  /* Parent process */
            close(client_fd);
        }
    }
    
    return 0;
}

/* Connect to all other servers
 * Connects to all other servers in the distributed file system
 * @return: SUCCESS on success, ERR_SOCKET on failure
 */
int connect_to_servers() {
    int ports[] = {S2_PORT, S3_PORT, S4_PORT};
    
    /* Try to connect multiple times with delays */
    for (int attempt = 0; attempt < 5; attempt++) {
        for (int i = 0; i < NUM_SERVERS; i++) {
            if (server_sockets[i] == -1) {
                server_sockets[i] = connect_to_server(ports[i]);
                if (server_sockets[i] >= 0) {
                    printf("Debug: Connected to server on port %d (socket %d)\n", 
                           ports[i], server_sockets[i]);
                }
            }
        }
        
        /* Check if all servers are connected */
        int all_connected = 1;
        for (int i = 0; i < NUM_SERVERS; i++) {
            if (server_sockets[i] == -1) {
                all_connected = 0;
                break;
            }
        }
        
        if (all_connected) {
            printf("Debug: Successfully connected to all servers\n");
            return SUCCESS;
        }
        
        /* Wait before retrying */
        sleep(1);
    }
    
    return ERR_SOCKET;
}

/* Connect to a specific server
 * Connects to a specific server in the distributed file system
 * @param port: port number of the server to connect to
 * @return: socket file descriptor on success, ERR_SOCKET on failure
 */
int connect_to_server(int port) {
    int sock_fd;
    struct sockaddr_in server_addr;
    
    /* Create socket */
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        return ERR_SOCKET;
    }
    
    /* Configure server address */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");  /* Connect to localhost */
    server_addr.sin_port = htons(port);
    
    /* Connect to server */
    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(sock_fd);
        return ERR_SOCKET;
    }
    
    return sock_fd;
}

/* Initialize server
 * Initializes the server socket
 * @param port: port number to listen on
 * @return: server file descriptor on success, ERR_SOCKET on failure
 */
int init_server(int port) {
    int server_fd;
    struct sockaddr_in server_addr;
    
    /* Create socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        return ERR_SOCKET;
    }
    
    /* Set socket options */
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close(server_fd);
        return ERR_SOCKET;
    }
    
    /* Configure server address */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;  /* Bind to all network interfaces */
    server_addr.sin_port = htons(port);
    
    /* Bind socket */
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(server_fd);
        return ERR_BIND;
    }
    
    /* Listen for connections */
    if (listen(server_fd, 10) < 0) {
        close(server_fd);
        return ERR_LISTEN;
    }
    
    return server_fd;
}

/* Get appropriate server for file type
 * Determines which server should handle a given file type
 * @param filename: name of the file to check
 * @return: index of the appropriate server, -1 if not handled by S1
 */
int get_server_for_file(const char *filename) {
    if (strstr(filename, FILE_C)) {
        return -1;  /* S1 handles C files directly */
    } else if (strstr(filename, FILE_PDF)) {
        return 0;  /* S2 */
    } else if (strstr(filename, FILE_TXT)) {
        return 1;  /* S3 */
    } else if (strstr(filename, FILE_ZIP)) {
        return 2;  /* S4 */
    }
    return -1;
}

/* Forward command to appropriate server
 * Forwards a command to the appropriate server based on the command type
 * @param server_sock: socket file descriptor of the server to forward to
 * @param cmd: pointer to the command structure
 */
void forward_command(int server_sock, const struct command *cmd) {
    char buffer[MAX_BUFFER];
    
    /* Format command string */
    snprintf(buffer, MAX_BUFFER, "%s %s %s", 
        cmd->cmd_type, cmd->arg1, cmd->arg2);
    
    printf("Debug: Forwarding command to server: '%s'\n", buffer);
    
    /* Send command */
    ssize_t sent = send(server_sock, buffer, strlen(buffer), 0);
    if (sent < 0) {
        printf("Debug: Failed to forward command (errno: %d)\n", errno);
        return;
    }
    printf("Debug: Successfully forwarded command (%zd bytes)\n", sent);

    /* For download commands, we don't wait for initial response */
    if (strcmp(cmd->cmd_type, CMD_DOWNLF) == 0 || 
        strcmp(cmd->cmd_type, CMD_DOWNLTAR) == 0) {
        return;
    }

    /* For other commands, wait for initial response */
    memset(buffer, 0, MAX_BUFFER);
    ssize_t bytes_read = recv(server_sock, buffer, MAX_BUFFER - 1, 0);
    if (bytes_read <= 0) {
        printf("Debug: Failed to receive initial response from server\n");
        return;
    }
    buffer[bytes_read] = '\0';
    printf("Debug: Initial server response: '%s'\n", buffer);
}

/* Function to validate and map S1 path
 * Validates the path and maps it to the S1 path
 * @param path: path to validate and map
 * @param mapped: buffer to store the mapped path
 * @param size: size of the mapped buffer
 * @return: mapped path on success, NULL on failure
 */
char* map_s1_path(const char* path, char* mapped, size_t size) {
    printf("Debug: Mapping path: %s\n", path);
    
    // First check if path starts with ~/S1
    if (strncmp(path, "~/S1", 4) != 0) {
        printf("Debug: Path does not start with ~/S1\n");
        return NULL;
    }

    // Get home directory
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) {
            home = pw->pw_dir;
        }
    }
    if (!home) {
        printf("Debug: Could not determine home directory\n");
        return NULL;
    }

    // Keep S1 in the path (unlike other servers that replace it)
    const char *remainder = path + 1; // Skip past "~"
    snprintf(mapped, size, "%s%s", home, remainder);
    printf("Debug: Mapped to: %s\n", mapped);
    return mapped;
}

/* Process client requests
 * Processes client requests and handles them
 * @param client_fd: socket file descriptor
 */
void process_client(int client_fd) {
    char buffer[MAX_BUFFER];
    struct command cmd;
    char mapped_path[MAX_PATH];
    
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_read = recv(client_fd, buffer, MAX_BUFFER - 1, 0);
        if (bytes_read <= 0) break;
        
        buffer[bytes_read] = '\0';
        printf("Debug: Received command: '%s'\n", buffer);
        
        if (parse_command(buffer, &cmd) != SUCCESS) {
            send(client_fd, "Error: Invalid command format", 28, 0);
            continue;
        }
        
        /* Handle dispfnames command first */
        if (strcmp(cmd.cmd_type, CMD_DISPFNAMES) == 0) {
            /* Map and validate path */
            if (strncmp(cmd.arg1, "~/S1", 4) != 0) {
                send(client_fd, "Error: Path must start with ~/S1", 31, 0);
                continue;
            }

            /* Get list of files from all servers */
            char result[MAX_BUFFER * 4] = "";  // Larger buffer for all servers
            char temp_buffer[MAX_BUFFER];
            FILE *fp;
            int found_files = 0;
            
            /* Get C files from S1 */
            if (!map_s1_path(cmd.arg1, mapped_path, sizeof(mapped_path))) {
                send(client_fd, "Error: Invalid path mapping", 26, 0);
                continue;
            }

            /* Check if directory exists */
            struct stat st;
            if (stat(mapped_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
                send(client_fd, "Error: Directory not found", 24, 0);
                continue;
            }

            /* List C files */
            snprintf(temp_buffer, sizeof(temp_buffer), 
                "find %s -maxdepth 1 -type f -name '*.c' -printf '%%f\\n' 2>/dev/null | sort",
                mapped_path);
            
            fp = popen(temp_buffer, "r");
            if (fp) {
                size_t bytes = fread(temp_buffer, 1, sizeof(temp_buffer) - 1, fp);
                pclose(fp);
                if (bytes > 0) {
                    temp_buffer[bytes] = '\0';
                    strcat(result, "C files:\n");
                    strncat(result, temp_buffer, sizeof(result) - strlen(result) - 1);
                    found_files = 1;
                }
            }
            
            /* Forward request to S2 for PDF files */
            if (server_sockets[0] >= 0) {
                char s2_path[MAX_PATH];
                strncpy(s2_path, cmd.arg1, sizeof(s2_path));
                char *s1_pos = strstr(s2_path, "S1");
                if (s1_pos) {
                    s1_pos[1] = '2';  // Replace S1 with S2
                }
                
                printf("Debug: Sending dispfnames request to S2 for path: %s\n", s2_path);
                if (send_command(server_sockets[0], "%s %s", CMD_DISPFNAMES, s2_path) == SUCCESS) {
                    memset(temp_buffer, 0, sizeof(temp_buffer));
                    if (receive_response(server_sockets[0], temp_buffer, sizeof(temp_buffer)) == SUCCESS) {
                        printf("Debug: Received response from S2: '%s'\n", temp_buffer);
                        if (strncmp(temp_buffer, "Error:", 6) != 0) {
                            if (strlen(temp_buffer) > 0 && strcmp(temp_buffer, "No files found") != 0) {
                                if (found_files) strcat(result, "\n");
                                strcat(result, "PDF files:\n");
                                strncat(result, temp_buffer, sizeof(result) - strlen(result) - 1);
                                found_files = 1;
                            }
                        }
                    }
                }
            }
            
            /* Forward request to S3 for TXT files */
            if (server_sockets[1] >= 0) {
                char s3_path[MAX_PATH];
                strncpy(s3_path, cmd.arg1, sizeof(s3_path));
                char *s1_pos = strstr(s3_path, "S1");
                if (s1_pos) {
                    s1_pos[1] = '3';  // Replace S1 with S3
                }
                
                printf("Debug: Sending dispfnames request to S3 for path: %s\n", s3_path);
                if (send_command(server_sockets[1], "%s %s", CMD_DISPFNAMES, s3_path) == SUCCESS) {
                    memset(temp_buffer, 0, sizeof(temp_buffer));
                    if (receive_response(server_sockets[1], temp_buffer, sizeof(temp_buffer)) == SUCCESS) {
                        printf("Debug: Received response from S3: '%s'\n", temp_buffer);
                        if (strncmp(temp_buffer, "Error:", 6) != 0) {
                            if (strlen(temp_buffer) > 0 && strcmp(temp_buffer, "No files found") != 0) {
                                if (found_files) strcat(result, "\n");
                                strcat(result, "Text files:\n");
                                strncat(result, temp_buffer, sizeof(result) - strlen(result) - 1);
                                found_files = 1;
                            }
                        }
                    }
                }
            }
            
            /* Forward request to S4 for ZIP files */
            if (server_sockets[2] >= 0) {
                char s4_path[MAX_PATH];
                strncpy(s4_path, cmd.arg1, sizeof(s4_path));
                char *s1_pos = strstr(s4_path, "S1");
                if (s1_pos) {
                    s1_pos[1] = '4';  // Replace S1 with S4
                }
                
                printf("Debug: Sending dispfnames request to S4 for path: %s\n", s4_path);
                if (send_command(server_sockets[2], "%s %s", CMD_DISPFNAMES, s4_path) == SUCCESS) {
                    memset(temp_buffer, 0, sizeof(temp_buffer));
                    if (receive_response(server_sockets[2], temp_buffer, sizeof(temp_buffer)) == SUCCESS) {
                        printf("Debug: Received response from S4: '%s'\n", temp_buffer);
                        if (strncmp(temp_buffer, "Error:", 6) != 0) {
                            if (strlen(temp_buffer) > 0 && strcmp(temp_buffer, "No files found") != 0) {
                                if (found_files) strcat(result, "\n");
                                strcat(result, "ZIP files:\n");
                                strncat(result, temp_buffer, sizeof(result) - strlen(result) - 1);
                                found_files = 1;
                            }
                        }
                    }
                }
            }
            
            /* Send combined results */
            if (strlen(result) > 0) {
                send(client_fd, result, strlen(result), 0);
            } else {
                send(client_fd, "No files found", 13, 0);
            }
            continue;
        }

        // Handle remove command
        if (strcmp(cmd.cmd_type, CMD_REMOVEF) == 0) {
            char *filepath = cmd.arg1;
            printf("Debug: Processing remove command for: %s\n", filepath);
            
            // Validate path starts with ~/S1
            if (strncmp(filepath, "~/S1", 4) != 0) {
                send(client_fd, "Error: Path must start with ~/S1", 31, 0);
                continue;
            }
            
            // Get file extension
            const char *ext = strrchr(filepath, '.');
            if (!ext) {
                send(client_fd, "Error: Must specify a file with extension", 39, 0);
                continue;
            }

            // Handle .c files locally
            if (strcmp(ext, ".c") == 0) {
                if (!map_s1_path(filepath, mapped_path, sizeof(mapped_path))) {
                    send(client_fd, "Error: Invalid path mapping", 26, 0);
                    continue;
                }

                printf("Debug: Removing .c file: %s\n", mapped_path);
                if (access(mapped_path, F_OK) != 0) {
                    send(client_fd, "Error: File does not exist", 25, 0);
                    continue;
                }

                if (remove(mapped_path) == 0) {
                    send(client_fd, "success", 7, 0);
                } else {
                    printf("Debug: Failed to remove file (errno: %d)\n", errno);
                    send(client_fd, "Error: Failed to remove file", 27, 0);
                }
                continue;
            }
            
            // Forward to appropriate server based on extension
            if (strcmp(ext, ".pdf") == 0) {
                forward_remove_command(client_fd, filepath, 2);
            } else if (strcmp(ext, ".txt") == 0) {
                forward_remove_command(client_fd, filepath, 3);
            } else if (strcmp(ext, ".zip") == 0) {
                forward_remove_command(client_fd, filepath, 4);
            } else {
                send(client_fd, "Error: Invalid file type", 23, 0);
            }
            continue;
        }
        
        /* For upload command, check the source file type */
        if (strcmp(cmd.cmd_type, CMD_UPLOADF) == 0) {
            int server_idx = get_server_for_file(cmd.arg1);
            
            /* Handle C files directly in S1 */
            if (strstr(cmd.arg1, FILE_C)) {
                /* Map and validate destination path */
                char dest_dir[MAX_PATH];
                
                if (strcmp(cmd.arg2, "~/S1") == 0 || strcmp(cmd.arg2, "~/S1/") == 0) {
                    // If the path is just ~/S1, use it as is
                    if (!map_s1_path(cmd.arg2, dest_dir, sizeof(dest_dir))) {
                        send(client_fd, "Error: Path must start with ~/S1", 31, 0);
                        continue;
                    }
                } else {
                    // For paths with subdirectories
                    if (!map_s1_path(cmd.arg2, dest_dir, sizeof(dest_dir))) {
                        send(client_fd, "Error: Path must start with ~/S1", 31, 0);
                        continue;
                    }
                }
                
                printf("Debug: Original path: %s\n", cmd.arg2);
                printf("Debug: Mapped directory: %s\n", dest_dir);
                
                /* Create directory structure */
                if (create_directory(dest_dir) != 0 && errno != EEXIST) {
                    printf("Debug: Failed to create directory: %s (errno: %d)\n", dest_dir, errno);
                    send(client_fd, "Error: Failed to create directory", 31, 0);
                    continue;
                }
                
                /* Construct the full file path */
                char full_path[MAX_PATH];
                const char *filename = strrchr(cmd.arg1, '/');
                if (filename) {
                    filename++; // Skip the slash
                } else {
                    filename = cmd.arg1;
                }
                
                snprintf(full_path, sizeof(full_path), "%s/%s", dest_dir, filename);
                printf("Debug: Full file path: %s\n", full_path);
                
                /* Create temporary file */
                char temp_path[MAX_PATH];
                snprintf(temp_path, sizeof(temp_path), "/tmp/temp_upload_XXXXXX");
                int temp_fd = mkstemp(temp_path);
                if (temp_fd == -1) {
                    printf("Debug: Failed to create temp file\n");
                    send(client_fd, "Error: Failed to create temporary file", 36, 0);
                    continue;
                }
                close(temp_fd);
                
                /* Send success response to client to start file transfer */
                printf("Debug: Sending initial OK to client\n");
                send(client_fd, "OK", 2, 0);
                
                /* Receive file into temporary location */
                int result = receive_file(client_fd, temp_path);
                printf("Debug: receive_file result: %d\n", result);
                
                if (result == SUCCESS) {
                    /* Move file to final destination */
                    if (rename(temp_path, full_path) == 0) {
                        printf("Debug: Successfully moved file to: %s\n", full_path);
                        send(client_fd, "File uploaded successfully", 24, 0);
                    } else {
                        printf("Debug: Rename failed (errno: %d), trying copy method\n", errno);
                        
                        /* Try copy method if rename fails */
                        FILE *src = fopen(temp_path, "rb");
                        FILE *dst = fopen(full_path, "wb");
                        
                        if (!src || !dst) {
                            printf("Debug: Failed to open files for copy (errno: %d)\n", errno);
                            send(client_fd, "Error: Failed to save file", 25, 0);
                            if (src) fclose(src);
                            if (dst) fclose(dst);
                            remove(temp_path);
                        } else {
                            /* Copy the file contents */
                            char buf[8192];
                            size_t bytes_read;
                            int copy_success = 1;
                            
                            while ((bytes_read = fread(buf, 1, sizeof(buf), src)) > 0) {
                                if (fwrite(buf, 1, bytes_read, dst) != bytes_read) {
                                    copy_success = 0;
                                    break;
                                }
                            }
                            
                            /* Clean up */
                            fclose(src);
                            fclose(dst);
                            remove(temp_path);
                            
                            if (copy_success) {
                                printf("Debug: Successfully copied file to: %s\n", full_path);
                                send(client_fd, "File uploaded successfully", 24, 0);
                            } else {
                                printf("Debug: Failed to copy file contents\n");
                                send(client_fd, "Error: Failed to save file", 25, 0);
                                remove(full_path);  // Clean up partial file
                            }
                        }
                    }
                } else {
                    printf("Debug: Failed to receive file (error: %d)\n", result);
                    remove(temp_path);
                    if (result == ERR_FILE_NOT_FOUND) {
                        send(client_fd, "Error: Failed to create destination file", 38, 0);
                    } else {
                        send(client_fd, "Error: Failed to upload file", 27, 0);
                    }
                }
                continue;
            }
            
            /* Forward to appropriate server if not a C file */
            if (server_idx >= 0 && server_sockets[server_idx] >= 0) {
                /* Modify destination path for the appropriate server */
                char modified_path[MAX_PATH];
                char server_prefix[8];
                int target_server = server_idx + 2;  // S2, S3, or S4
                
                printf("Debug: Processing file for S%d\n", target_server);
                
                /* Replace ~/S1 with ~/S2, ~/S3, or ~/S4 */
                if (strncmp(cmd.arg2, "~/S1", 4) == 0) {
                    /* If it's just ~/S1, append a slash */
                    if (strlen(cmd.arg2) == 4) {
                        snprintf(modified_path, sizeof(modified_path), "~/S%d/", target_server);
                    } else {
                        /* Replace S1 with appropriate server number */
                        snprintf(modified_path, sizeof(modified_path), "~/S%d%s", 
                                target_server, cmd.arg2 + 4);
                    }
                    strncpy(cmd.arg2, modified_path, MAX_PATH - 1);
                    cmd.arg2[MAX_PATH - 1] = '\0';
                    printf("Debug: Modified destination path to: %s\n", cmd.arg2);
                }
                
                /* Forward command to appropriate server */
                forward_command(server_sockets[server_idx], &cmd);
                
                /* Create temporary file to store uploaded content */
                char temp_file[] = "/tmp/temp_upload_XXXXXX";
                int temp_fd = mkstemp(temp_file);
                if (temp_fd < 0) {
                    send(client_fd, "Failed to create temporary file for transfer", 42, 0);
                    continue;
                }
                close(temp_fd);
                
                /* Send success response to client to start file transfer */
                send(client_fd, "OK", 2, 0);
                
                /* Receive file from client */
                printf("Debug: Receiving file from client into temp file: %s\n", temp_file);
                if (receive_file(client_fd, temp_file) == SUCCESS) {
                    printf("Debug: Successfully received file from client, forwarding to S%d\n", target_server);
                    
                    /* Forward file to appropriate server */
                    FILE *temp = fopen(temp_file, "rb");
                    if (!temp) {
                        printf("Debug: Failed to open temp file for reading\n");
                        remove(temp_file);
                        send(client_fd, "Error: Failed to process file", 28, 0);
                        continue;
                    }
                    
                    /* Get file size */
                    fseek(temp, 0, SEEK_END);
                    long file_size = ftell(temp);
                    rewind(temp);
                    
                    /* Send file size to server */
                    char size_str[32];
                    snprintf(size_str, sizeof(size_str), "%ld", file_size);
                    if (send(server_sockets[server_idx], size_str, strlen(size_str), 0) <= 0) {
                        printf("Debug: Failed to send file size to server\n");
                        fclose(temp);
                        remove(temp_file);
                        send(client_fd, "Error: Failed to forward file size", 33, 0);
                        continue;
                    }
                    
                    /* Wait for server acknowledgment with timeout */
                    char response[MAX_BUFFER];
                    memset(response, 0, sizeof(response));
                    
                    /* Set socket timeout */
                    struct timeval tv;
                    tv.tv_sec = 5;  // 5 seconds timeout
                    tv.tv_usec = 0;
                    setsockopt(server_sockets[server_idx], SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
                    
                    /* Receive response */
                    ssize_t resp_len = recv(server_sockets[server_idx], response, sizeof(response) - 1, 0);
                    if (resp_len <= 0) {
                        printf("Debug: Failed to receive server response (timeout or error)\n");
                        fclose(temp);
                        remove(temp_file);
                        send(client_fd, "Error: Server not responding", 27, 0);
                        continue;
                    }
                    response[resp_len] = '\0';
                    printf("Debug: Server response: '%s'\n", response);
                    
                    /* Check for error response */
                    if (strncmp(response, "Error:", 6) == 0) {
                        printf("Debug: Server returned error: %s\n", response);
                        fclose(temp);
                        remove(temp_file);
                        send(client_fd, response, resp_len, 0);
                        continue;
                    }
                    
                    /* Verify OK response */
                    if (strcmp(response, "OK") != 0) {
                        printf("Debug: Unexpected server response: %s\n", response);
                        fclose(temp);
                        remove(temp_file);
                        send(client_fd, "Error: Unexpected server response", 32, 0);
                        continue;
                    }
                    
                    printf("Debug: Server acknowledged file size, sending contents\n");
                    
                    /* Send file contents */
                    char buffer[MAX_BUFFER];
                    size_t bytes_read;
                    long total_sent = 0;
                    
                    while ((bytes_read = fread(buffer, 1, sizeof(buffer), temp)) > 0) {
                        ssize_t bytes_sent = send(server_sockets[server_idx], buffer, bytes_read, 0);
                        if (bytes_sent <= 0) {
                            printf("Debug: Failed to send file data to server\n");
                            fclose(temp);
                            remove(temp_file);
                            send(client_fd, "Error: Failed to forward file data", 33, 0);
                            continue;
                        }
                        total_sent += bytes_sent;
                        printf("Debug: Forwarded %ld of %ld bytes to server\n", total_sent, file_size);
                    }
                    
                    fclose(temp);
                    remove(temp_file);
                    
                    /* Wait for final server response */
                    memset(response, 0, sizeof(response));
                    resp_len = recv(server_sockets[server_idx], response, sizeof(response) - 1, 0);
                    
                    if (resp_len > 0) {
                        response[resp_len] = '\0';
                        printf("Debug: Final server response: '%s'\n", response);
                        if (strstr(response, "success") != NULL || strcmp(response, "OK") == 0) {
                            send(client_fd, "File uploaded successfully", 24, 0);
                        } else {
                            send(client_fd, response, resp_len, 0);
                        }
                    } else {
                        printf("Debug: Failed to get final server response\n");
                        send(client_fd, "Error: No response from server", 29, 0);
                    }
                } else {
                    remove(temp_file);
                    send(client_fd, "Error: Failed to receive file from client", 39, 0);
                }
                continue;
            }
            
            send(client_fd, "Invalid file type or server not available for handling this file type", 67, 0);
            continue;
        }
        
        /* For other commands, check the path and file type */
        int server_idx = get_server_for_file(cmd.arg1);
        
        /* Handle C files directly in S1 */
        if (server_idx == -1 && strstr(cmd.arg1, FILE_C)) {
            if (strcmp(cmd.cmd_type, CMD_DOWNLF) == 0) {
                // Get file extension
                char *dot = strrchr(cmd.arg1, '.');
                if (!dot) {
                    send(client_fd, "Error: Invalid file format", 25, 0);
                    continue;
                }

                // Convert extension to lowercase for comparison
                char ext[5];
                strncpy(ext, dot, sizeof(ext) - 1);
                ext[sizeof(ext) - 1] = '\0';
                for (char *p = ext; *p; p++) {
                    *p = tolower(*p);
                }

                int target_server = -1;
                char mapped_path[MAX_PATH];
                const char *remainder = cmd.arg1 + 4;  // Skip "~/S1"

                // Map to appropriate server based on extension
                if (strcmp(ext, ".c") == 0) {
                    // Handle .c files locally (S1)
                    target_server = 0;
                    snprintf(mapped_path, sizeof(mapped_path), "%s/S1%s", getenv("HOME") ? getenv("HOME") : ".", remainder);
                    
                    if (access(mapped_path, F_OK) == -1) {
                        printf("Debug: File not found in S1: %s\n", mapped_path);
                        send(client_fd, "Error: File not found", 21, 0);
                        continue;
                    }
                    
                    int result = send_file(client_fd, mapped_path);
                    if (result != SUCCESS) {
                        printf("Debug: Failed to send file: %s (error: %d)\n", mapped_path, result);
                    }
                    continue;
                } else if (strcmp(ext, ".pdf") == 0) {
                    // Route to S2 for PDF files
                    target_server = 0;  // Index 0 in server_sockets array is S2
                    // Preserve the directory structure when mapping to S2
                    const char *path_after_s1 = strstr(cmd.arg1, "S1");
                    if (path_after_s1) {
                        path_after_s1 += 2;  // Skip "S1"
                        snprintf(mapped_path, sizeof(mapped_path), "~/S2%s", path_after_s1);
                    } else {
                        // Fallback if path structure is unexpected
                        snprintf(mapped_path, sizeof(mapped_path), "~/S2%s", remainder);
                    }
                    printf("Debug: PDF file request, routing to S2. Original path: %s, Mapped path: %s\n", 
                           cmd.arg1, mapped_path);
                } else if (strcmp(ext, ".txt") == 0) {
                    // Route to S3 for text files
                    target_server = 1;  // Index 1 in server_sockets array is S3
                    snprintf(mapped_path, sizeof(mapped_path), "~/S3%s", remainder);
                    printf("Debug: TXT file request, routing to S3. Path: %s\n", mapped_path);
                } else if (strcmp(ext, ".zip") == 0) {
                    // Route to S4 for zip files
                    target_server = 2;  // Index 2 in server_sockets array is S4
                    snprintf(mapped_path, sizeof(mapped_path), "~/S4%s", remainder);
                    printf("Debug: ZIP file request, routing to S4. Path: %s\n", mapped_path);
                } else {
                    send(client_fd, "Error: Unsupported file type", 27, 0);
                    continue;
                }

                printf("Debug: File type: %s, Target server: %d\n", ext, target_server);
                printf("Debug: Original path: %s\n", cmd.arg1);
                printf("Debug: Mapped path: %s\n", mapped_path);

                if (target_server == 0 && strcmp(ext, ".c") == 0) {
                    // Handle locally in S1 for .c files
                    if (access(mapped_path, F_OK) == -1) {
                        printf("Debug: File not found in S1: %s\n", mapped_path);
                        send(client_fd, "Error: File not found", 20, 0);
                        continue;
                    }
                    int result = send_file(client_fd, mapped_path);
                    if (result != SUCCESS) {
                        printf("Debug: Failed to send file: %s (error: %d)\n", mapped_path, result);
                    }
                } else {
                    // Forward request to appropriate server
                    int server_fd = server_sockets[target_server];
                    if (server_fd < 0) {
                        printf("Debug: Server not connected. Target: %d, FD: %d\n", target_server, server_fd);
                        send(client_fd, "Error: Server not connected", 27, 0);
                        continue;
                    }

                    // Create and send command to target server
                    char server_cmd[MAX_BUFFER];
                    snprintf(server_cmd, sizeof(server_cmd), "%s %s", CMD_DOWNLF, mapped_path);
                    printf("Debug: Sending command to server: '%s'\n", server_cmd);

                    if (send(server_fd, server_cmd, strlen(server_cmd), 0) <= 0) {
                        printf("Debug: Failed to send command to server\n");
                        send(client_fd, "Error: Failed to forward request to server", 39, 0);
                        continue;
                    }

                    // Forward the file from server to client using temporary file
                    char temp_path[MAX_PATH];
                    snprintf(temp_path, sizeof(temp_path), "/tmp/temp_download_XXXXXX");
                    int temp_fd = mkstemp(temp_path);
                    if (temp_fd == -1) {
                        printf("Debug: Failed to create temp file\n");
                        long error = -1;
                        send(client_fd, &error, sizeof(error), 0);
                        continue;
                    }
                    close(temp_fd);

                    // Get file size from server
                    char size_buffer[32];
                    memset(size_buffer, 0, sizeof(size_buffer));
                    ssize_t size_received = recv(server_fd, size_buffer, sizeof(size_buffer) - 1, 0);
                    if (size_received <= 0) {
                        printf("Debug: Failed to receive file size from server\n");
                        unlink(temp_path);
                        send(client_fd, "Error: Failed to get file size from server", 41, 0);
                        continue;
                    }
                    size_buffer[size_received] = '\0';

                    // Check for error message
                    if (strncmp(size_buffer, "Error:", 6) == 0) {
                        printf("Debug: Server returned error: %s\n", size_buffer);
                        unlink(temp_path);
                        send(client_fd, size_buffer, strlen(size_buffer), 0);
                        continue;
                    }

                    // Parse file size
                    long file_size;
                    if (sscanf(size_buffer, "%ld", &file_size) != 1 || file_size <= 0) {
                        printf("Debug: Invalid file size received: %s\n", size_buffer);
                        unlink(temp_path);
                        send(client_fd, "Error: Invalid file size received", 31, 0);
                        continue;
                    }

                    // Forward file size to client
                    if (send(client_fd, size_buffer, strlen(size_buffer), 0) <= 0) {
                        printf("Debug: Failed to send file size to client\n");
                        unlink(temp_path);
                        continue;
                    }

                    // Wait for client acknowledgment
                    char ack[3];
                    if (recv(client_fd, ack, 2, 0) <= 0 || strncmp(ack, "OK", 2) != 0) {
                        printf("Debug: Failed to receive client acknowledgment\n");
                        unlink(temp_path);
                        continue;
                    }

                    // Send acknowledgment to server
                    if (send(server_fd, "OK", 2, 0) <= 0) {
                        printf("Debug: Failed to send acknowledgment to server\n");
                        unlink(temp_path);
                        send(client_fd, "Error: Failed to acknowledge server", 33, 0);
                        continue;
                    }

                    // Receive file from server and forward to client
                    char transfer_buffer[MAX_BUFFER];
                    long total_received = 0;

                    while (total_received < file_size) {
                        ssize_t bytes = recv(server_fd, transfer_buffer, 
                            MIN(sizeof(transfer_buffer), file_size - total_received), 0);
                        
                        if (bytes <= 0) {
                            printf("Debug: Failed to receive file data from server\n");
                            break;
                        }

                        ssize_t bytes_sent = send(client_fd, transfer_buffer, bytes, 0);
                        if (bytes_sent != bytes) {
                            printf("Debug: Failed to forward data to client\n");
                            break;
                        }

                        total_received += bytes;
                        printf("Debug: Forwarded %ld of %ld bytes\n", total_received, file_size);
                    }

                    if (total_received != file_size) {
                        printf("Debug: Incomplete file transfer\n");
                        send(client_fd, "Error: Incomplete file transfer", 29, 0);
                    }

                    unlink(temp_path);
                }
            } else if (strcmp(cmd.cmd_type, CMD_DOWNLTAR) == 0) {
                const char *filetype = cmd.arg1;
                char temp_path[MAX_PATH];
                int result;
                
                // Validate file type
                if (!filetype || (strcmp(filetype, ".c") != 0 && 
                    strcmp(filetype, ".pdf") != 0 && 
                    strcmp(filetype, ".txt") != 0)) {
                    send(client_fd, "Error: Invalid file type. Must be .c, .pdf, or .txt", 48, 0);
                    continue;
                }
                
                // Create temporary file path
                snprintf(temp_path, sizeof(temp_path), "/tmp/tar_XXXXXX");
                int temp_fd = mkstemp(temp_path);
                if (temp_fd < 0) {
                    send(client_fd, "Error: Failed to create temporary file", 36, 0);
                    continue;
                }
                close(temp_fd);
                
                // Handle based on file type
                if (strcmp(filetype, ".c") == 0) {
                    result = create_c_files_tar(temp_path);
                } else if (strcmp(filetype, ".pdf") == 0) {
                    result = request_server_tar(2, filetype, temp_path);
                } else { // .txt
                    result = request_server_tar(3, filetype, temp_path);
                }
                
                if (result != SUCCESS) {
                    send(client_fd, "Error: Failed to create tar file", 31, 0);
                    remove(temp_path);
                    continue;
                }
                
                // Send the tar file
                if (send_file(client_fd, temp_path) != SUCCESS) {
                    printf("Debug: Failed to send tar file to client\n");
                }
                
                // Clean up
                remove(temp_path);
            } else {
                send(client_fd, "Invalid file type or server not available", 41, 0);
            }
            continue;
        }
        
        /* Forward to appropriate server if not a C file */
        if (server_idx >= 0 && server_sockets[server_idx] >= 0) {
            if (strcmp(cmd.cmd_type, CMD_DOWNLF) == 0 || 
                strcmp(cmd.cmd_type, CMD_DOWNLTAR) == 0) {
                /* For download commands, send command and receive file directly */
                char temp_file[] = "/tmp/temp_download_XXXXXX";
                int temp_fd = mkstemp(temp_file);
                if (temp_fd == -1) {
                    printf("Debug: Failed to create temp file\n");
                    long error = -1;
                    send(client_fd, &error, sizeof(error), 0);
                    continue;
                }
                close(temp_fd);

                /* Send command to server */
                char server_cmd[MAX_BUFFER];
                snprintf(server_cmd, sizeof(server_cmd), "%s %s", cmd.cmd_type, cmd.arg1);
                printf("Debug: Sending command to server: '%s'\n", server_cmd);

                if (send(server_sockets[server_idx], server_cmd, strlen(server_cmd), 0) <= 0) {
                    printf("Debug: Failed to send command to server\n");
                    unlink(temp_file);
                    long error = -1;
                    send(client_fd, &error, sizeof(error), 0);
                    continue;
                }

                /* Receive file from server */
                int result = receive_file(server_sockets[server_idx], temp_file);
                if (result != SUCCESS) {
                    printf("Debug: Failed to receive file from server\n");
                    unlink(temp_file);
                    long error = -1;
                    send(client_fd, &error, sizeof(error), 0);
                    continue;
                }

                /* Send file to client */
                result = send_file(client_fd, temp_file);
                unlink(temp_file);
                if (result != SUCCESS) {
                    printf("Debug: Failed to send file to client\n");
                    long error = -1;
                    send(client_fd, &error, sizeof(error), 0);
                }
            } else {
                /* For other commands, use forward_command */
                forward_command(server_sockets[server_idx], &cmd);
                
                /* Forward the response */
                char buffer[MAX_BUFFER];
                memset(buffer, 0, MAX_BUFFER);
                ssize_t bytes_read = recv(server_sockets[server_idx], buffer, MAX_BUFFER - 1, 0);
                if (bytes_read > 0) {
                    send(client_fd, buffer, bytes_read, 0);
                }
            }
            continue;
        }
        
        send(client_fd, "Invalid file type or server not available", 41, 0);
    }
}

/* Accept client connection
 * Accepts a client connection on the server socket
 * @param server_fd: server file descriptor
 * @return: client file descriptor on success, ERR_ACCEPT on failure
 */
int accept_connection(int server_fd) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    return accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
}

/* Send file to socket
 * Sends a file to the server
 * @param sock_fd: socket file descriptor
 * @param filepath: path of file to send (must start with ~/S1)
 * @return: SUCCESS on success, ERR_TRANSFER on failure
 */
int send_file(int sock_fd, const char *filepath) {
    char buffer[MAX_BUFFER];
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        printf("Debug: Failed to open file: %s\n", filepath);
        char error_msg[MAX_BUFFER];
        snprintf(error_msg, sizeof(error_msg), "Error: File not found");
        send(sock_fd, error_msg, strlen(error_msg), 0);
        return ERR_FILE_NOT_FOUND;
    }
    
    /* Get file size */
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);
    
    /* Send file size as text */
    char size_str[32];
    snprintf(size_str, sizeof(size_str), "%ld", file_size);
    if (send(sock_fd, size_str, strlen(size_str), 0) < 0) {
        printf("Debug: Failed to send file size\n");
        fclose(file);
        return ERR_TRANSFER;
    }
    
    /* Wait for client acknowledgment */
    char ack[3] = {0};
    if (recv(sock_fd, ack, 2, 0) <= 0 || strncmp(ack, "OK", 2) != 0) {
        printf("Debug: Failed to receive client acknowledgment\n");
        fclose(file);
        return ERR_TRANSFER;
    }
    
    /* Send file contents */
    size_t bytes_read;
    long total_sent = 0;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        ssize_t bytes_sent = send(sock_fd, buffer, bytes_read, 0);
        if (bytes_sent < 0) {
            printf("Debug: Failed to send file data\n");
            fclose(file);
            return ERR_TRANSFER;
        }
        total_sent += bytes_sent;
    }
    
    printf("Debug: Successfully sent %ld bytes\n", total_sent);
    fclose(file);
    return SUCCESS;
}

/* Receive file from socket
 * Receives a file from the server and writes it to a file
 * @param sock_fd: socket file descriptor
 * @param filepath: path of file to receive (must start with ~/S1)
 * @return: SUCCESS on success, ERR_TRANSFER on failure
 */
int receive_file(int sock_fd, const char *filepath) {
    char buffer[MAX_BUFFER];
    FILE *file = NULL;
    
    printf("Debug: Starting file receive for: %s\n", filepath);
    
    /* Receive file size as text */
    memset(buffer, 0, sizeof(buffer));
    ssize_t bytes_received = recv(sock_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        printf("Debug: Failed to receive file size\n");
        return ERR_TRANSFER;
    }
    buffer[bytes_received] = '\0';
    
    /* Check for error message */
    if (strncmp(buffer, "Error:", 6) == 0) {
        printf("Debug: Server returned error: %s\n", buffer);
        return ERR_FILE_NOT_FOUND;
    }
    
    /* Parse file size */
    long file_size;
    if (sscanf(buffer, "%ld", &file_size) != 1 || file_size <= 0) {
        printf("Debug: Invalid file size received: %s\n", buffer);
        return ERR_TRANSFER;
    }
    printf("Debug: Received file size: %ld bytes\n", file_size);
    
    /* Create the file */
    file = fopen(filepath, "wb");
    if (!file) {
        printf("Debug: Failed to create file: %s (errno: %d)\n", filepath, errno);
        return ERR_FILE_NOT_FOUND;
    }
    
    /* Send acknowledgment */
    if (send(sock_fd, "OK", 2, 0) <= 0) {
        printf("Debug: Failed to send acknowledgment\n");
        fclose(file);
        remove(filepath);
        return ERR_TRANSFER;
    }
    
    /* Receive file contents */
    long total_received = 0;
    time_t last_progress = time(NULL);
    
    while (total_received < file_size) {
        bytes_received = recv(sock_fd, buffer, 
            MIN(sizeof(buffer), file_size - total_received), 0);
        
        if (bytes_received <= 0) {
            printf("Debug: Failed to receive file contents (received %zd bytes)\n", bytes_received);
            fclose(file);
            remove(filepath);  // Delete the partial file
            return ERR_TRANSFER;
        }
        
        size_t bytes_written = fwrite(buffer, 1, bytes_received, file);
        if (bytes_written != bytes_received) {
            printf("Debug: Failed to write to file (wrote %zu of %zd bytes)\n", 
                   bytes_written, bytes_received);
            fclose(file);
            remove(filepath);  // Delete the partial file
            return ERR_TRANSFER;
        }
        
        total_received += bytes_received;
        
        /* Show progress every second */
        time_t now = time(NULL);
        if (now != last_progress) {
            fprintf(stderr, "\rReceiving: %ld/%ld bytes (%.1f%%)", 
                    total_received, file_size, 
                    (float)total_received * 100 / file_size);
            fflush(stderr);
            last_progress = now;
        }
    }
    
    if (total_received > 0) {
        fprintf(stderr, "\n");  // New line after progress
    }
    
    fclose(file);
    printf("Debug: File transfer completed successfully\n");
    return SUCCESS;
}

/* Parse command string
 * Parses the command string and stores the command type and arguments
 * @param cmd_str: command string to parse
 * @param cmd: struct to store the parsed command
 * @return: SUCCESS on success, ERR_INVALID_CMD on failure
 */
int parse_command(char *cmd_str, struct command *cmd) {
    char *token;
    
    /* Get command type */
    token = strtok(cmd_str, " ");
    if (!token) return ERR_INVALID_CMD;
    strncpy(cmd->cmd_type, token, MAX_CMD - 1);
    
    /* Get first argument */
    token = strtok(NULL, " ");
    if (token) {
        strncpy(cmd->arg1, token, MAX_PATH - 1);
    } else {
        cmd->arg1[0] = '\0';
    }
    
    /* Get second argument */
    token = strtok(NULL, " ");
    if (token) {
        strncpy(cmd->arg2, token, MAX_PATH - 1);
    } else {
        cmd->arg2[0] = '\0';
    }
    
    return SUCCESS;
}

/* Create directory recursively
 * Creates a directory recursively
 * @param path: path of the directory to create
 * @return: 0 on success, -1 on failure
 */
int create_directory(const char *path) {
    char tmp[MAX_PATH];
    char *p = NULL;
    size_t len;
    
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }
    
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    
    return mkdir(tmp, 0755);
}

/* Error logging
 * Logs an error message to the standard error stream
 * @param msg: error message to log
 */
void log_error(const char *msg) {
    fprintf(stderr, "Error: %s (%s)\n", msg, strerror(errno));
}

/* Path validation
 * Validates the path to ensure it is valid and prevents directory traversal
 * @param path: path to validate
 * @return: 1 if valid, 0 if invalid
 */
int validate_path(const char *path) {
    if (!path || strlen(path) == 0) return 0;
    if (strstr(path, "..")) return 0;  /* Prevent directory traversal */
    return 1;
}

/* Signal handler for cleanup
 * Cleans up the server when a signal is received
 * @param signum: signal number
 */
void cleanup(int signum) {
    /* Close all server connections */
    for (int i = 0; i < NUM_SERVERS; i++) {
        if (server_sockets[i] >= 0) {
            close(server_sockets[i]);
        }
    }
    
    if (server_fd != -1) close(server_fd);
    exit(0);
}

/* Function to expand ~ to home directory
 * Expands the ~ to the user's home directory
 * @param path: path to expand
 * @param expanded: buffer to store the expanded path
 * @param size: size of the expanded buffer
 * @return: expanded path on success, NULL on failure
 */
char* expand_path(const char* path, char* expanded, size_t size) {
    if (path[0] == '~') {
        const char *home;
        if (path[1] == '/' || path[1] == '\0') {
            // ~/ or just ~
            home = getenv("HOME");
            if (!home) {
                struct passwd *pw = getpwuid(getuid());
                if (pw) {
                    home = pw->pw_dir;
                }
            }
            if (home) {
                snprintf(expanded, size, "%s%s", home, path + 1);
                return expanded;
            }
        }
    }
    strncpy(expanded, path, size);
    expanded[size - 1] = '\0';
    return expanded;
}

/* Create server directories
 * Creates the server directories in the user's home directory
 */
void create_server_directories(void) {
    char path[MAX_PATH];
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
    if (!home) return;

    /* Create server directories */
    snprintf(path, sizeof(path), "%s/S1", home);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/S2", home);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/S3", home);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/S4", home);
    mkdir(path, 0755);
}

int forward_remove_command(int client_fd, const char *filepath, int server_num) {
    int server_idx = server_num - 2;  // Convert server number to array index
    if (server_idx < 0 || server_idx >= NUM_SERVERS) {
        printf("Debug: Invalid server number %d\n", server_num);
        send(client_fd, "Error: Invalid server number", 27, 0);
        return ERR_TRANSFER;
    }
    
    int server_fd = server_sockets[server_idx];
    if (server_fd < 0) {
        printf("Debug: Server %d not connected (socket %d)\n", server_num, server_fd);
        send(client_fd, "Error: Server not available", 26, 0);
        return ERR_TRANSFER;
    }
    
    char mapped_path[MAX_PATH];
    char command[MAX_BUFFER];
    char response[MAX_BUFFER];
    
    // Map the path for the appropriate server
    if (!map_path_to_server(filepath, mapped_path, sizeof(mapped_path), server_num)) {
        printf("Debug: Failed to map path %s for server %d\n", filepath, server_num);
        send(client_fd, "Error: Invalid path mapping", 26, 0);
        return ERR_TRANSFER;
    }
    
    // Send remove command to server
    snprintf(command, sizeof(command), "removef %s", mapped_path);
    printf("Debug: Forwarding remove command to server %d (socket %d): '%s'\n", 
           server_num, server_fd, command);
    
    if (send(server_fd, command, strlen(command), 0) < 0) {
        printf("Debug: Failed to send command to server %d (errno: %d)\n", server_num, errno);
        send(client_fd, "Error: Failed to send command to server", 37, 0);
        return ERR_TRANSFER;
    }
    
    // Set timeout for response
    struct timeval tv;
    tv.tv_sec = 5;  // 5 second timeout
    tv.tv_usec = 0;
    setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    
    // Get response from server
    memset(response, 0, sizeof(response));
    ssize_t bytes_received = recv(server_fd, response, sizeof(response) - 1, 0);
    
    if (bytes_received <= 0) {
        printf("Debug: No response from server %d (errno: %d)\n", server_num, errno);
        send(client_fd, "Error: Server timeout or no response", 34, 0);
        return ERR_TRANSFER;
    }
    
    response[bytes_received] = '\0';
    printf("Debug: Server %d response: '%s'\n", server_num, response);
    
    // Forward response to client
    if (strstr(response, "success")) {
        printf("Debug: Sending success response to client\n");
        send(client_fd, "success", 7, 0);
    } else if (strstr(response, "not found") || strstr(response, "not exist")) {
        printf("Debug: File not found on server %d\n", server_num);
        send(client_fd, "Error: File does not exist", 25, 0);
    } else {
        printf("Debug: Server %d failed to remove file\n", server_num);
        send(client_fd, "Error: Failed to remove file", 27, 0);
    }
    
    return SUCCESS;
}

/* Function to map S1 path to appropriate server path
 * Maps the S1 path to the appropriate server path
 * @param path: path to map
 * @param mapped: buffer to store the mapped path
 * @param size: size of the mapped buffer
 * @param server_num: server number to map to
 */
char* map_path_to_server(const char* path, char* mapped, size_t size, int server_num) {
    if (!path || !mapped || size == 0 || server_num < 2 || server_num > 4) {
        return NULL;
    }
    
    // Check if path starts with ~/S1
    if (strncmp(path, "~/S1", 4) != 0) {
        return NULL;
    }
    
    // Get home directory
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
    if (!home) return NULL;
    
    // Skip past "~/S1"
    const char *remainder = path + 4;
    
    // Create the new path with appropriate server number
    if (snprintf(mapped, size, "~/S%d%s", server_num, remainder) >= (int)size) {
        return NULL;  // Path too long
    }
    
    return mapped;
}

/* Function to create tar of .c files
 * Creates a tar archive of all .c files in the S1 directory
 * @param tar_path: path to save the tar archive
 * @return: 0 on success, -1 on failure
 */
int create_c_files_tar(const char *tar_path) {
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());  // Get the user's home directory
        if (pw) home = pw->pw_dir;  // Set home to the user's home directory
    }
    if (!home) return ERR_TRANSFER;

    char cmd[MAX_BUFFER];
    snprintf(cmd, sizeof(cmd), 
        "cd %s && find S1 -name '*.c' -print0 | tar czf %s --null -T -", 
        home, tar_path);
    
    return system(cmd);
}

/* Function to request tar from other servers
 * Requests a tar archive from another server
 * @param server_num: server number to request from
 * @param filetype: type of file to request
 * @param temp_path: path to save the tar archive
 * @return: 0 on success, -1 on failure
 */
int request_server_tar(int server_num, const char *filetype, const char *temp_path) {
    int server_idx = server_num - 2;
    if (server_idx < 0 || server_idx >= NUM_SERVERS) {
        printf("Debug: Invalid server number %d\n", server_num);
        return ERR_TRANSFER;
    }
    
    int server_fd = server_sockets[server_idx];
    if (server_fd < 0) {
        printf("Debug: Server %d not connected\n", server_num);
        return ERR_TRANSFER;
    }
    
    // Send downltar command
    char command[MAX_BUFFER];
    snprintf(command, sizeof(command), "downltar %s", filetype);
    printf("Debug: Sending command to server %d: %s\n", server_num, command);
    
    if (send(server_fd, command, strlen(command), 0) < 0) {
        printf("Debug: Failed to send command to server %d\n", server_num);
        return ERR_TRANSFER;
    }
    
    // Receive and save the tar file
    FILE *file = fopen(temp_path, "wb");
    if (!file) {
        printf("Debug: Failed to create temporary file: %s\n", temp_path);
        return ERR_TRANSFER;
    }
    
    // Get file size
    char size_str[32];
    ssize_t bytes_received = recv(server_fd, size_str, sizeof(size_str) - 1, 0);
    if (bytes_received <= 0) {
        printf("Debug: Failed to receive file size from server %d\n", server_num);
        fclose(file);
        remove(temp_path);
        return ERR_TRANSFER;
    }
    size_str[bytes_received] = '\0';
    
    // Parse file size
    long file_size;
    if (sscanf(size_str, "%ld", &file_size) != 1) {
        printf("Debug: Invalid file size received: %s\n", size_str);
        fclose(file);
        remove(temp_path);
        return ERR_TRANSFER;
    }
    
    // Send acknowledgment
    if (send(server_fd, "OK", 2, 0) < 0) {
        printf("Debug: Failed to send acknowledgment to server %d\n", server_num);
        fclose(file);
        remove(temp_path);
        return ERR_TRANSFER;
    }
    
    // Receive file data
    char buffer[MAX_BUFFER];
    long total_received = 0;
    while (total_received < file_size) {
        bytes_received = recv(server_fd, buffer, 
            MIN(sizeof(buffer), file_size - total_received), 0);
        
        if (bytes_received <= 0) {
            printf("Debug: Failed to receive file data from server %d\n", server_num);
            fclose(file);
            remove(temp_path);
            return ERR_TRANSFER;
        }
        
        if (fwrite(buffer, 1, bytes_received, file) != bytes_received) {
            printf("Debug: Failed to write to temporary file\n");
            fclose(file);
            remove(temp_path);
            return ERR_TRANSFER;
        }
        
        total_received += bytes_received;
    }
    
    fclose(file);
    return SUCCESS;
}

/* Send formatted command to server
 * Sends a formatted command to the server
 * @param sock_fd: socket file descriptor
 * @param format: format string for the command
 * @param ...: variable arguments for the command
 * @return: SUCCESS on success, ERR_TRANSFER on failure
 */
int send_command(int sock_fd, const char *format, ...) {
    char buffer[MAX_BUFFER];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    printf("Debug: Sending command: '%s'\n", buffer);
    ssize_t sent = send(sock_fd, buffer, strlen(buffer), 0);
    if (sent <= 0) {
        printf("Debug: Failed to send command (errno: %d)\n", errno);
        return ERR_TRANSFER;
    }
    
    printf("Debug: Successfully sent %zd bytes\n", sent);
    return SUCCESS;
}

/* Receive response from server
 * Receives a response from the server and stores it in a buffer
 * @param sock_fd: socket file descriptor
 * @param buffer: buffer to store the response
 * @param size: size of the buffer
 * @return: SUCCESS on success, ERR_TRANSFER on failure
 */
int receive_response(int sock_fd, char *buffer, size_t size) {
    memset(buffer, 0, size);
    
    /* Set socket timeout */
    struct timeval tv;
    tv.tv_sec = 5;  // 5 seconds timeout
    tv.tv_usec = 0;
    setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    
    /* Receive response with timeout */
    ssize_t bytes_received = recv(sock_fd, buffer, size - 1, 0);
    if (bytes_received <= 0) {
        printf("Debug: Failed to receive response (errno: %d)\n", errno);
        return ERR_TRANSFER;
    }
    
    buffer[bytes_received] = '\0';
    printf("Debug: Received response: '%s'\n", buffer);
    return SUCCESS;
} 