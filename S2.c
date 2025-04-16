/*
 * S2 Server Implementation
 * Handles PDF file operations in the distributed file system
 * This server is responsible for:
 * - Storing and managing PDF files
 * - Processing upload/download requests
 * - Creating tar archives of PDF files
 * - Maintaining directory structure
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <pwd.h>

/* Define constants */
#define MAX_BUFFER 1024
#define MAX_PATH 256
#define MAX_FILENAME 128
#define MAX_CMD 64

/* Helper macros */
#define MIN(a,b) ((a) < (b) ? (a) : (b))

/* Port numbers */
#define S2_PORT 5601

/* Command types */
#define CMD_UPLOADF "uploadf"
#define CMD_DOWNLF "downlf"
#define CMD_REMOVEF "removef"
#define CMD_DOWNLTAR "downltar"
#define CMD_DISPFNAMES "dispfnames"

/* File types */
#define FILE_PDF ".pdf"

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
int accept_connection(int server_fd);
int send_file(int sock_fd, const char *filepath);
int receive_file(int sock_fd, const char *filepath);
int parse_command(char *cmd_str, struct command *cmd);
void process_client(int client_fd);
void cleanup(int signum);
char* map_s1_to_s2_path(const char* path, char* mapped, size_t size);

/* Main function */
int main() {
    struct sigaction sa = {.sa_handler = cleanup, .sa_flags = 0}; // Initialize signal handler
    sigemptyset(&sa.sa_mask); // Empty the signal mask
    sigaction(SIGINT, &sa, NULL); // Register SIGINT handler
    sigaction(SIGTERM, &sa, NULL); // Register SIGTERM handler
    
    if ((server_fd = init_server(S2_PORT)) < 0) {
        fprintf(stderr, "Failed to initialize server\n");
        return 1;
    }
    
    printf("PDF Server (S2) started on port %d\n", S2_PORT);
    
    while (1) {
        int client_fd = accept_connection(server_fd);
        if (client_fd < 0) continue;
        
        pid_t pid = fork();
        if (pid < 0) {
            close(client_fd);
        } else if (pid == 0) {
            close(server_fd);
            process_client(client_fd);
            exit(0);
        } else {
            close(client_fd);
        }
    }
    return 0;
}

/* Initialize server
 * Initializes the server
 * @param port: port number to listen on
 * @return: server file descriptor on success, ERR_SOCKET on failure
 */
int init_server(int port) {
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET, // Set the address family to IPv4
        .sin_addr.s_addr = INADDR_ANY, // Accept connections on all network interfaces
        .sin_port = htons(port) // Convert port number to network byte order
    };
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return ERR_SOCKET;
    
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0 ||
        bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0 ||
        listen(server_fd, 10) < 0) {
        close(server_fd);
        return ERR_SOCKET;
    }
    
    return server_fd;
}

/* Accept client connection
 * Accepts a client connection
 * @param server_fd: server file descriptor
 * @return: client file descriptor on success, ERR_ACCEPT on failure
 */
int accept_connection(int server_fd) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    return accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
}

/* Process client requests
 * Processes client requests
 * @param client_fd: socket file descriptor
 */
void process_client(int client_fd) {
    char buffer[MAX_BUFFER];
    struct command cmd;
    char mapped_path[MAX_PATH];
    
    while (1) {
        memset(buffer, 0, MAX_BUFFER);
        ssize_t bytes_read = recv(client_fd, buffer, MAX_BUFFER - 1, 0);
        if (bytes_read <= 0) break;
        
        if (parse_command(buffer, &cmd) != SUCCESS) {
            send(client_fd, "Invalid command format", 21, 0);
            continue;
        }
        
        if (strcmp(cmd.cmd_type, CMD_UPLOADF) == 0) {
            if (!map_s1_to_s2_path(cmd.arg2, mapped_path, sizeof(mapped_path))) {
                send(client_fd, "Error: Invalid path", 18, 0);
                continue;
            }
            
            // Extract the filename from the source path
            const char *src_filename = strrchr(cmd.arg1, '/');
            src_filename = src_filename ? src_filename + 1 : cmd.arg1;
            
            // Create directory path
            char dir_path[MAX_PATH];
            strncpy(dir_path, mapped_path, sizeof(dir_path) - 1);
            dir_path[sizeof(dir_path) - 1] = '\0';
            
            // If mapped_path doesn't end with the source filename, append it
            const char *dest_filename = strrchr(mapped_path, '/');
            dest_filename = dest_filename ? dest_filename + 1 : mapped_path;
            
            if (strcmp(dest_filename, src_filename) != 0) {
                // This is a directory path, append the filename
                size_t path_len = strlen(mapped_path);
                if (path_len > 0) {
                    // Ensure path ends with /
                    if (mapped_path[path_len - 1] != '/') {
                        if (path_len < sizeof(mapped_path) - 1) {
                            mapped_path[path_len] = '/';
                            mapped_path[path_len + 1] = '\0';
                            path_len++;
                        }
                    }
                    // Append filename
                    if (snprintf(mapped_path + path_len, sizeof(mapped_path) - path_len, "%s", src_filename) >= (int)(sizeof(mapped_path) - path_len)) {
                        send(client_fd, "Error: Path too long", 19, 0);
                        continue;
                    }
                }
                
                // Update dir_path to be the directory portion
                strncpy(dir_path, mapped_path, sizeof(dir_path) - 1);
                dir_path[sizeof(dir_path) - 1] = '\0';
                char *last_slash = strrchr(dir_path, '/');
                if (last_slash) {
                    *last_slash = '\0';
                }
            }
            
            // Create all parent directories
            char *p = dir_path;
            while (*p) {
                if (*p == '/') {
                    *p = '\0';
                    if (strlen(dir_path) > 0) {
                        mkdir(dir_path, 0755);
                    }
                    *p = '/';
                }
                p++;
            }
            if (strlen(dir_path) > 0) {
                mkdir(dir_path, 0755);
            }
            
            printf("Debug: Creating file at: %s\n", mapped_path);
            
            send(client_fd, "OK", 2, 0);
            
            int result = receive_file(client_fd, mapped_path);
            printf("Debug: receive_file result: %d\n", result);
            
            if (result == SUCCESS) {
                send(client_fd, "File uploaded successfully", 24, 0);
            } else {
                if (result == ERR_FILE_NOT_FOUND) {
                    send(client_fd, "Error: Failed to create destination file", 38, 0);
                } else {
                    send(client_fd, "Error: Failed to upload file", 27, 0);
                }
            }
            
        } else if (strcmp(cmd.cmd_type, CMD_DOWNLF) == 0) {
            if (!map_s1_to_s2_path(cmd.arg1, mapped_path, sizeof(mapped_path))) {
                send(client_fd, "Error: Invalid path", 18, 0);
                continue;
            }
            
            if (send_file(client_fd, mapped_path) != SUCCESS) {
                long error = -1;
                send(client_fd, &error, sizeof(error), 0);
            }
            
        } else if (strcmp(cmd.cmd_type, CMD_REMOVEF) == 0) {
            if (!map_s1_to_s2_path(cmd.arg1, mapped_path, sizeof(mapped_path))) {
                send(client_fd, "Error: Invalid path", 18, 0);
                continue;
            }
            
            // Check if file exists first
            if (access(mapped_path, F_OK) != 0) {
                send(client_fd, "Error: File does not exist", 25, 0);
                continue;
            }
            
            if (remove(mapped_path) == 0) {
                send(client_fd, "success", 7, 0);  // Changed to match S1's response
            } else {
                send(client_fd, "Error: Failed to remove file", 27, 0);
            }
            
        } else if (strcmp(cmd.cmd_type, CMD_DOWNLTAR) == 0) {
            const char *filetype = cmd.arg1;
            
            // Validate file type
            if (strcmp(filetype, ".pdf") != 0) {
                send(client_fd, "Error: S2 only handles .pdf files", 31, 0);
                continue;
            }
            
            // Create temporary tar file
            char temp_path[MAX_PATH];
            snprintf(temp_path, sizeof(temp_path), "/tmp/pdf_XXXXXX");
            int temp_fd = mkstemp(temp_path);
            if (temp_fd < 0) {
                send(client_fd, "Error: Failed to create temporary file", 36, 0);
                continue;
            }
            close(temp_fd);
            
            // Get home directory
            const char *home = getenv("HOME");
            if (!home) {
                struct passwd *pw = getpwuid(getuid());
                if (pw) home = pw->pw_dir;
            }
            if (!home) {
                send(client_fd, "Error: Failed to get home directory", 34, 0);
                remove(temp_path);
                continue;
            }
            
            // Create tar command
            char cmd[MAX_BUFFER];
            snprintf(cmd, sizeof(cmd), 
                "cd %s && find S2 -name '*.pdf' -print0 | tar czf %s --null -T -",
                home, temp_path);
            
            // Execute tar command
            if (system(cmd) != 0) {
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
            
        } else if (strcmp(cmd.cmd_type, CMD_DISPFNAMES) == 0) {
            if (!map_s1_to_s2_path(cmd.arg1, mapped_path, sizeof(mapped_path))) {
                send(client_fd, "Error: Invalid path", 18, 0);
                continue;
            }

            // Check if directory exists
            struct stat st;
            if (stat(mapped_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
                printf("Debug: Directory does not exist or is not a directory: %s\n", mapped_path);
                send(client_fd, "Error: Directory not found", 24, 0);
                continue;
            }

            char result[MAX_BUFFER] = "";
            char find_cmd[MAX_BUFFER];
            
            snprintf(find_cmd, sizeof(find_cmd), 
                "find %s -type f -name '*.pdf' -printf '%%P\\n' 2>/dev/null | sort",
                mapped_path);
            
            printf("Debug: Executing command: %s\n", find_cmd);
            
            FILE *fp = popen(find_cmd, "r");
            if (!fp) {
                printf("Debug: Failed to execute find command\n");
                send(client_fd, "Error: Failed to list files", 25, 0);
                continue;
            }

            size_t bytes = fread(result, 1, sizeof(result) - 1, fp);
            int status = pclose(fp);
            
            if (status != 0) {
                printf("Debug: Find command failed with status %d\n", status);
                send(client_fd, "Error: Failed to list files", 25, 0);
                continue;
            }

            result[bytes] = '\0';
            
            if (bytes == 0) {
                // No files found - send empty string
                send(client_fd, "", 0, 0);
                printf("Debug: No .pdf files found in %s\n", mapped_path);
            } else {
                send(client_fd, result, strlen(result), 0);
                printf("Debug: Found and sent list of .pdf files\n");
            }
        }
    }
}

/* Send file to socket
 * Sends a file to the server
 * @param sock_fd: socket file descriptor
 * @param filepath: path to the file to send
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
        printf("Debug: Sent %ld of %ld bytes\n", total_sent, file_size);
    }
    
    printf("Debug: Successfully sent %ld bytes\n", total_sent);
    fclose(file);
    return SUCCESS;
}

/* Receive file from socket
 * Receives a file from the server
 * @param sock_fd: socket file descriptor
 * @param filepath: path to the file to receive
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
        printf("Debug: Received %ld of %ld bytes\n", total_received, file_size);
    }
    
    fclose(file);
    printf("Debug: File transfer completed successfully\n");
    return SUCCESS;
}

/* Parse command string
 * Parses a command string
 * @param cmd_str: command string to parse
 * @param cmd: command structure to store the parsed command
 * @return: SUCCESS on success, ERR_INVALID_CMD on failure
 */
int parse_command(char *cmd_str, struct command *cmd) {
    char *token = strtok(cmd_str, " ");
    if (!token) return ERR_INVALID_CMD;
    
    strncpy(cmd->cmd_type, token, MAX_CMD - 1);
    cmd->arg1[0] = cmd->arg2[0] = '\0';
    
    if ((token = strtok(NULL, " "))) strncpy(cmd->arg1, token, MAX_PATH - 1);
    if ((token = strtok(NULL, " "))) strncpy(cmd->arg2, token, MAX_PATH - 1);
    
    return SUCCESS;
}

/* Signal handler for cleanup
 * Handles cleanup when a signal is received
 * @param signum: signal number
 */
void cleanup(int signum) {
    if (server_fd != -1) close(server_fd);
    exit(0);
}

/* Function to validate and map S1 path to S2 path
 * Validates and maps an S1 path to an S2 path
 * @param path: input path to validate and map
 * @param mapped: output buffer to store the mapped path
 * @param size: size of the output buffer
 * @return: pointer to the mapped path, or NULL on failure
 */
char* map_s1_to_s2_path(const char* path, char* mapped, size_t size) {
    if (!path || !mapped || size == 0) return NULL;
    
    // Check if path starts with ~/S1 or ~/S2
    if (strncmp(path, "~/S1", 4) != 0 && strncmp(path, "~/S2", 4) != 0) {
        return NULL;
    }
    
    // Get home directory
    const char *home = getenv("HOME"); // Get the home directory from the environment
    if (!home) { // If home directory is not set, get it from the current user
        struct passwd *pw = getpwuid(getuid()); // Get the current user's password entry
        if (pw) home = pw->pw_dir; // Set home to the user's home directory
    }
    if (!home) return NULL; // If home directory is still not set, return NULL
    
    // Skip past "~/S1" or "~/S2"
    const char *remainder = path + 4;
    
    // Create the new path with S2
    if (snprintf(mapped, size, "%s/S2%s", home, remainder) >= (int)size) {
        return NULL;  // Path too long
    }
    
    return mapped;
} 