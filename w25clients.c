/*
 * Distributed File System Client Implementation
 * This client provides an interface to interact with a distributed file system
 * consisting of multiple servers (S1-S4) that handle different file types:
 * - S1 Server: Manages .c files and coordinates with other servers
 * - S2 Server: Handles .pdf files
 * - S3 Server: Manages .txt files
 * - S4 Server: Handles .zip files
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>  // For struct timeval
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>

/* Define constants for the project */
#define MAX_BUFFER 1024
#define MAX_PATH 256
#define MAX_FILENAME 128
#define MAX_CMD 64
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define S1_PORT 5600

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
#define ERR_CONNECT -2
#define ERR_FILE_NOT_FOUND -6
#define ERR_INVALID_CMD -7
#define ERR_TRANSFER -9

/* Structure for command */
struct command {
    char cmd_type[MAX_CMD];
    char arg1[MAX_PATH];
    char arg2[MAX_PATH];
};

/* Function declarations */
int connect_to_main_server(const char *ip);
void disconnect_from_server(int sock_fd);
int handle_uploadf(int sock_fd, const char *filename, const char *dest_path);
int handle_downlf(int sock_fd, const char *filepath);
int handle_removef(int sock_fd, const char *filepath);
int handle_downltar(int sock_fd, const char *filetype);
int handle_dispfnames(int sock_fd, const char *pathname);
int validate_file_exists(const char *filename);
int validate_file_type(const char *filename);
int validate_s1_path(const char *path);
void print_usage(void);
void print_error(const char *msg);
int parse_command(char *cmd_str, struct command *cmd);
int send_command(int sock_fd, const char *format, ...);
int receive_response(int sock_fd, char *buffer, size_t size);
int send_file_data(int sock_fd, FILE *file, long file_size);
int receive_file_data(int sock_fd, FILE *file, long file_size);
int receive_file(int sock_fd, const char *filepath);
int send_file(int sock_fd, const char *filepath);

/* Main function */
int main() {
    int sock_fd;
    char input[MAX_BUFFER];
    struct command cmd;
    
    /* Connect to main server (S1) */
    sock_fd = connect_to_main_server("localhost");
    if (sock_fd < 0) {
        print_error("Failed to connect to server");
        return 1;
    }
    
    printf("Connected to server. Type 'help' for available commands.\n");
    
    /* Main client loop */
    while (1) {
        printf("w25clients$ ");
        fflush(stdout);
        
        /* Get user input */
        if (!fgets(input, MAX_BUFFER, stdin)) {
            break;
        }
        
        /* Remove newline */
        input[strcspn(input, "\n")] = 0;
        
        /* Check for exit command */
        if (strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0) {
            break;
        }
        
        /* Check for help command */
        if (strcmp(input, "help") == 0) {
            print_usage();
            continue;
        }
        
        /* Parse and validate command */
        if (parse_command(input, &cmd) != SUCCESS) {
            printf("Invalid command format. Type 'help' for usage.\n");
            continue;
        }
        
        /* Process commands */
        int result = SUCCESS;
        if (strcmp(cmd.cmd_type, CMD_UPLOADF) == 0) {
            if (!cmd.arg1[0] || !cmd.arg2[0]) {
                printf("Usage: uploadf filename destination_path\n");
                continue;
            }
            result = handle_uploadf(sock_fd, cmd.arg1, cmd.arg2);
            
        } else if (strcmp(cmd.cmd_type, CMD_DOWNLF) == 0) {
            if (!cmd.arg1[0]) {
                printf("Usage: downlf filename\n");
                continue;
            }
            result = handle_downlf(sock_fd, cmd.arg1);
            
        } else if (strcmp(cmd.cmd_type, CMD_REMOVEF) == 0) {
            if (!cmd.arg1[0]) {
                printf("Usage: removef filename\n");
                continue;
            }
            result = handle_removef(sock_fd, cmd.arg1);
            
        } else if (strcmp(cmd.cmd_type, CMD_DOWNLTAR) == 0) {
            if (!cmd.arg1[0]) {
                printf("Usage: downltar filetype\n");
                continue;
            }
            result = handle_downltar(sock_fd, cmd.arg1);
            
        } else if (strcmp(cmd.cmd_type, CMD_DISPFNAMES) == 0) {
            if (!cmd.arg1[0]) {
                printf("Usage: dispfnames pathname\n");
                continue;
            }
            result = handle_dispfnames(sock_fd, cmd.arg1);
            
        } else {
            printf("Unknown command. Type 'help' for usage.\n");
            continue;
        }
        
        if (result != SUCCESS) {
            printf("Command failed with error code: %d\n", result);
        }
    }
    
    /* Cleanup */
    disconnect_from_server(sock_fd);
    printf("Disconnected from server.\n");
    
    return 0;
}

/* Connect to main server (S1)
 * Establishes a TCP connection to the main server
 * @param ip: IP address of the server (unused, always connects to localhost)
 * @return: socket file descriptor on success, error code on failure
 */
int connect_to_main_server(const char *ip) {
    int sock_fd;
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(S1_PORT),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK)
    };
    
    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        return ERR_SOCKET;
    }
    // Connect to server
    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) { 
        close(sock_fd);
        return ERR_CONNECT;
    }
    
    return sock_fd;
}

/* Disconnect from server
 * Closes the socket connection to the server
 * @param sock_fd: socket file descriptor to close
 */
void disconnect_from_server(int sock_fd) {
    if (sock_fd >= 0) {
        close(sock_fd);
    }
}

/* Handle file upload
 * Uploads a file to the server
 * @param sock_fd: socket file descriptor to the server
 * @param filename: path to the file to upload
 * @param dest_path: destination path on the server
 * @return: SUCCESS on success, error code on failure
 */
int handle_uploadf(int sock_fd, const char *filename, const char *dest_path) {
    char buffer[MAX_BUFFER];
    FILE *file;
    
    /* Validate file exists and type */
    if (!validate_file_exists(filename)) {
        print_error("File does not exist");
        return ERR_FILE_NOT_FOUND;
    }
    /* Validate file type */
    if (!validate_file_type(filename)) {
        print_error("Invalid file type (must be .c, .pdf, .txt, or .zip)");
        return ERR_INVALID_CMD;
    }
    
    /* Validate destination path */
    if (!validate_s1_path(dest_path)) {
        print_error("Destination path must start with ~/S1");
        return ERR_INVALID_CMD;
    }
    
    /* Ensure dest_path ends with a / if it's just ~/S1 */
    char final_path[MAX_PATH];
    strncpy(final_path, dest_path, sizeof(final_path) - 2);  // Leave room for potential /
    final_path[sizeof(final_path) - 2] = '\0';
    
    if (strcmp(final_path, "~/S1") == 0) {
        strcat(final_path, "/");
    }
    
    /* Open file first to ensure we can read it */
    if (!(file = fopen(filename, "rb"))) {
        print_error("Failed to open file for reading");
        return ERR_FILE_NOT_FOUND;
    }
    
    /* Get file size */
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);
    
    if (file_size <= 0) {
        print_error("File is empty");
        fclose(file);
        return ERR_FILE_NOT_FOUND;
    }
    
    printf("Debug: File size: %ld bytes\n", file_size);
    
    /* Send initial command to server */
    if (send_command(sock_fd, "%s %s %s", CMD_UPLOADF, filename, final_path) != SUCCESS) {
        fclose(file);
        return ERR_TRANSFER;
    }
    
    /* Wait for initial server acknowledgment */
    memset(buffer, 0, sizeof(buffer));
    ssize_t response_len = recv(sock_fd, buffer, sizeof(buffer) - 1, 0);
    if (response_len <= 0) {
        print_error("Failed to receive server response");
        fclose(file);
        return ERR_TRANSFER;
    }
    buffer[response_len] = '\0';
    
    if (strcmp(buffer, "OK") != 0) {
        print_error(buffer);
        fclose(file);
        return ERR_TRANSFER;
    }
    
    printf("Debug: Server acknowledged command, sending file size\n");
    
    /* Send file size as text */
    char size_str[32];
    snprintf(size_str, sizeof(size_str), "%ld", file_size);
    if (send(sock_fd, size_str, strlen(size_str), 0) <= 0) {
        print_error("Failed to send file size");
        fclose(file);
        return ERR_TRANSFER;
    }
    
    /* Wait for size acknowledgment */
    memset(buffer, 0, sizeof(buffer));
    response_len = recv(sock_fd, buffer, sizeof(buffer) - 1, 0);
    if (response_len <= 0 || strcmp(buffer, "OK") != 0) {
        print_error("Failed to receive size acknowledgment");
        fclose(file);
        return ERR_TRANSFER;
    }
    
    printf("Debug: Server acknowledged file size, sending contents\n");
    
    /* Send file contents */
    size_t bytes_read;
    long total_sent = 0;
    time_t last_progress = time(NULL);
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        ssize_t bytes_sent = send(sock_fd, buffer, bytes_read, 0);
        if (bytes_sent < 0) {
            print_error("Failed to send file data");
            fclose(file);
            return ERR_TRANSFER;
        }
        total_sent += bytes_sent;
        
        /* Show progress every second */
        time_t now = time(NULL);
        if (now != last_progress) {
            fprintf(stderr, "\rSending: %ld/%ld bytes (%.1f%%)", 
                    total_sent, file_size, 
                    (float)total_sent * 100 / file_size);
            fflush(stderr);
            last_progress = now;
        }
    }
    
    if (total_sent > 0) {
        fprintf(stderr, "\n");  // New line after progress
    }
    
    printf("Debug: Successfully sent %ld bytes\n", total_sent);
    fclose(file);
    
    /* Wait for final server response */
    memset(buffer, 0, sizeof(buffer));
    response_len = recv(sock_fd, buffer, sizeof(buffer) - 1, 0);
    if (response_len <= 0) {
        print_error("Failed to receive final server response");
        return ERR_TRANSFER;
    }
    buffer[response_len] = '\0';
    
    if (strstr(buffer, "success") != NULL) {
        printf("%s\n", buffer);
        return SUCCESS;
    } else {
        print_error(buffer);
        return ERR_TRANSFER;
    }
}

/* Handle file download
 * Downloads a file from the server
 * @param sock_fd: socket file descriptor to the server
 * @param filepath: path to the file to download
 * @return: SUCCESS on success, error code on failure
 */
int handle_downlf(int sock_fd, const char *filepath) {
    char buffer[MAX_BUFFER];
    FILE *file = NULL;
    
    /* Validate path starts with ~/S1 */
    if (!filepath || strncmp(filepath, "~/S1", 4) != 0) {
        fprintf(stderr, "Error: Path must start with ~/S1\n");
        return ERR_INVALID_CMD;
    }

    /* Get file extension */
    char *dot = strrchr(filepath, '.');
    if (!dot) {
        fprintf(stderr, "Error: File must have an extension (.c, .pdf, .txt, or .zip)\n");
        return ERR_INVALID_CMD;
    }

    /* Convert extension to lowercase for validation */
    char ext[5];
    strncpy(ext, dot, sizeof(ext) - 1);
    ext[sizeof(ext) - 1] = '\0';
    for (char *p = ext; *p; p++) {
        *p = tolower(*p);
    }

    /* Validate file extension */
    if (strcmp(ext, ".c") != 0 && strcmp(ext, ".pdf") != 0 && 
        strcmp(ext, ".txt") != 0 && strcmp(ext, ".zip") != 0) {
        fprintf(stderr, "Error: Unsupported file type. Only .c, .pdf, .txt, and .zip files are supported\n");
        return ERR_INVALID_CMD;
    }

    /* Send command to server */
    if (send_command(sock_fd, "%s %s", CMD_DOWNLF, filepath) != SUCCESS) {
        fprintf(stderr, "Error sending command to server\n");
        return ERR_TRANSFER;
    }

    /* Get the local filename (just the basename) */
    const char *basename = strrchr(filepath, '/');
    basename = basename ? basename + 1 : filepath;
    
    /* Create local filename in current directory */
    char local_filename[MAX_PATH];
    snprintf(local_filename, sizeof(local_filename), "./%s", basename);

    /* Receive initial response */
    memset(buffer, 0, sizeof(buffer));
    ssize_t bytes_received = recv(sock_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        fprintf(stderr, "Error: Failed to receive server response\n");
        return ERR_TRANSFER;
    }
    buffer[bytes_received] = '\0';

    /* Check for error message */
    if (strncmp(buffer, "Error:", 6) == 0) {
        fprintf(stderr, "%s\n", buffer);
        return ERR_FILE_NOT_FOUND;
    }

    /* Parse file size from response */
    long file_size;
    if (sscanf(buffer, "%ld", &file_size) != 1) {
        fprintf(stderr, "Error: Invalid file size received\n");
        return ERR_TRANSFER;
    }

    /* Check if file exists on server */
    if (file_size <= 0) {
        fprintf(stderr, "Error: File not found on server\n");
        return ERR_FILE_NOT_FOUND;
    }

    /* Send acknowledgment */
    if (send(sock_fd, "OK", 2, 0) <= 0) {
        fprintf(stderr, "Error: Failed to send acknowledgment\n");
        return ERR_TRANSFER;
    }

    /* Only create the file if we know it exists on the server */
    file = fopen(local_filename, "wb");
    if (!file) {
        fprintf(stderr, "Error creating local file\n");
        return ERR_FILE_NOT_FOUND;
    }

    /* Receive file contents with progress indication */
    long total_received = 0;
    time_t last_progress = time(NULL);
    
    while (total_received < file_size) {
        bytes_received = recv(sock_fd, buffer, 
            MIN(sizeof(buffer), file_size - total_received), 0);
        
        if (bytes_received <= 0) {
            fprintf(stderr, "\nError receiving file data\n");
            fclose(file);
            remove(local_filename);  // Delete the partial file
            return ERR_TRANSFER;
        }
        
        if (fwrite(buffer, 1, bytes_received, file) != (size_t)bytes_received) {
            fprintf(stderr, "\nError writing to file\n");
            fclose(file);
            remove(local_filename);  // Delete the partial file
            return ERR_TRANSFER;
        }
        
        total_received += bytes_received;

        /* Show progress every second */
        time_t now = time(NULL);
        if (now != last_progress) {
            fprintf(stderr, "\rDownloading: %ld/%ld bytes (%.1f%%)", 
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
    printf("File downloaded successfully as %s\n", local_filename);
    return SUCCESS;
}

/* Validate file extension
 * Checks if the given filename has a supported extension
 * @param filename: name of the file to check
 * @return: 1 if extension is valid, 0 otherwise
 */
int is_valid_extension(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return 0;
    
    return (strcmp(ext, ".c") == 0 ||
            strcmp(ext, ".pdf") == 0 ||
            strcmp(ext, ".txt") == 0 ||
            strcmp(ext, ".zip") == 0);
}

/* Function to handle removef command
 * Removes a file from the server
 * @param sock_fd: socket file descriptor to the server
 * @param path: path to the file to remove
 * @return: SUCCESS on success, error code on failure
 */
int handle_removef(int sock_fd, const char *path) {
    /* Validate path starts with ~/S1 */
    if (strncmp(path, "~/S1", 4) != 0) {
        printf("Error: Path must start with ~/S1\n");
        return -1;
    }

    /* Check if a file is specified (path contains a dot) */
    if (!strrchr(path, '.')) {
        printf("Error: Must specify a file with .c, .pdf, .txt, or .zip extension\n");
        return -1;
    }

    /* Validate file extension */
    if (!is_valid_extension(path)) {
        printf("Error: Only .c, .pdf, .txt, and .zip files are allowed\n");
        return -1;
    }

    char command[MAX_BUFFER];
    snprintf(command, sizeof(command), "removef %s", path);
    
    if (send(sock_fd, command, strlen(command), 0) < 0) {
        printf("Error: Failed to send command to server\n");
        return -1;
    }

    char response[MAX_BUFFER];
    memset(response, 0, sizeof(response));
    ssize_t bytes_received = recv(sock_fd, response, sizeof(response) - 1, 0);
    if (bytes_received <= 0) {
        printf("Error: No response from server\n");
        return -1;
    }
    response[bytes_received] = '\0';

    /* Check for success message */
    if (strstr(response, "success")) {
        printf("File removed successfully: %s\n", path);
        return 0;
    } 
    /* Check for file not found */
    else if (strstr(response, "not found") || strstr(response, "not exist")) {
        printf("Error: File does not exist: %s\n", path);
        return -1;
    }
    /* Other errors */
    else {
        printf("Error: Failed to remove file: %s\n", path);
        return -1;
    }
}

/* Validate tar file type
 * Checks if the given file type is supported
 * @param filetype: file type to check
 * @return: 1 if file type is valid, 0 otherwise
 */
int is_valid_tar_type(const char *filetype) {
    return (strcmp(filetype, ".c") == 0 ||
            strcmp(filetype, ".pdf") == 0 ||
            strcmp(filetype, ".txt") == 0);
}

/* Handle tar download
 * Downloads a tar archive of files of a given type
 * @param sock_fd: socket file descriptor to the server
 * @param filetype: file type to download
 * @return: SUCCESS on success, error code on failure
 */
int handle_downltar(int sock_fd, const char *filetype) {
    // Validate file type
    if (!filetype || !is_valid_tar_type(filetype)) {
        printf("Error: Invalid file type. Must be .c, .pdf, or .txt\n");
        return -1;
    }

    char command[MAX_BUFFER];
    snprintf(command, sizeof(command), "downltar %s", filetype);
    
    if (send(sock_fd, command, strlen(command), 0) < 0) {
        printf("Error: Failed to send command to server\n");
        return -1;
    }

    // Determine output filename based on file type
    const char *output_name;
    if (strcmp(filetype, ".c") == 0) {
        output_name = "cfiles.tar";
    } else if (strcmp(filetype, ".pdf") == 0) {
        output_name = "pdf.tar";
    } else {
        output_name = "text.tar";
    }

    // Receive file size
    char size_str[32];
    ssize_t bytes_received = recv(sock_fd, size_str, sizeof(size_str) - 1, 0);
    if (bytes_received <= 0) {
        printf("Error: Failed to receive tar file size\n");
        return -1;
    }
    size_str[bytes_received] = '\0';

    // Check for error message
    if (strncmp(size_str, "Error:", 6) == 0) {
        printf("%s\n", size_str);
        return -1;
    }

    // Parse file size
    long file_size;
    if (sscanf(size_str, "%ld", &file_size) != 1) {
        printf("Error: Invalid file size received\n");
        return -1;
    }

    // Send acknowledgment
    if (send(sock_fd, "OK", 2, 0) < 0) {
        printf("Error: Failed to send acknowledgment\n");
        return -1;
    }

    // Create output file
    FILE *file = fopen(output_name, "wb");
    if (!file) {
        printf("Error: Failed to create tar file\n");
        return -1;
    }

    // Receive file data
    char buffer[MAX_BUFFER];
    long total_received = 0;
    while (total_received < file_size) {
        bytes_received = recv(sock_fd, buffer, 
            MIN(sizeof(buffer), file_size - total_received), 0);
        
        if (bytes_received <= 0) {
            printf("Error: Failed to receive tar file data\n");
            fclose(file);
            remove(output_name);
            return -1;
        }

        if (fwrite(buffer, 1, bytes_received, file) != bytes_received) {
            printf("Error: Failed to write tar file\n");
            fclose(file);
            remove(output_name);
            return -1;
        }

        total_received += bytes_received;
    }

    fclose(file);
    printf("Successfully downloaded %s (%ld bytes)\n", output_name, file_size);
    return 0;
}

/* Handle display filenames
 * Displays the filenames in a given directory
 * @param sock_fd: socket file descriptor to the server
 * @param pathname: path to the directory to display
 * @return: SUCCESS on success, error code on failure
 */
int handle_dispfnames(int sock_fd, const char *pathname) {
    char buffer[MAX_BUFFER];
    
    /* Validate path starts with ~/S1 */
    if (!validate_s1_path(pathname)) {
        print_error("Path must start with ~/S1");
        return ERR_INVALID_CMD;
    }
    
    /* Send command to server */
    if (send_command(sock_fd, "%s %s", CMD_DISPFNAMES, pathname) != SUCCESS) {
        return ERR_TRANSFER;
    }
    
    /* Receive and display response */
    if (receive_response(sock_fd, buffer, sizeof(buffer)) == SUCCESS) {
        if (strncmp(buffer, "Error:", 6) == 0) {
            print_error(buffer + 7); // Skip "Error: " prefix
            return ERR_TRANSFER;
        }
        
        printf("\nFiles in %s:\n", pathname);
        if (strlen(buffer) > 0) {
            printf("%s\n", buffer);
        } else {
            printf("No files found.\n");
        }
        return SUCCESS;
    }
    
    return ERR_TRANSFER;
}

/* Send command to server
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
    
    if (send(sock_fd, buffer, strlen(buffer), 0) < 0) {
        print_error("Failed to send command to server");
        return ERR_TRANSFER;
    }
    
    return SUCCESS;
}

/* Receive response from server
 * Receives a response from the server
 * @param sock_fd: socket file descriptor
 * @param buffer: buffer to store the response
 * @param size: size of the buffer
 * @return: SUCCESS on success, ERR_TRANSFER on failure
 */
int receive_response(int sock_fd, char *buffer, size_t size) {
    memset(buffer, 0, size);
    ssize_t bytes_read = recv(sock_fd, buffer, size - 1, 0);
    
    if (bytes_read <= 0) {
        print_error("Failed to receive server response");
        return ERR_TRANSFER;
    }
    
    buffer[bytes_read] = '\0';
    return SUCCESS;
}

/* Send file data to server
 * Sends the contents of a file to the server
 * @param sock_fd: socket file descriptor
 * @param file: file to send
 * @param file_size: size of the file
 * @return: SUCCESS on success, ERR_TRANSFER on failure
 */
int send_file_data(int sock_fd, FILE *file, long file_size) {
    char buffer[MAX_BUFFER];
    size_t bytes_read;
    long total_sent = 0;
    
    while (total_sent < file_size && 
           (bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (send(sock_fd, buffer, bytes_read, 0) < 0) {
            print_error("Failed to send file contents");
            return ERR_TRANSFER;
        }
        total_sent += bytes_read;
    }
    
    return SUCCESS;
}

/* Receive file data from server
 * Receives the contents of a file from the server
 * @param sock_fd: socket file descriptor
 * @param file: file to store the received data
 * @param file_size: size of the file
 * @return: SUCCESS on success, ERR_TRANSFER on failure
 */
int receive_file_data(int sock_fd, FILE *file, long file_size) {
    char buffer[MAX_BUFFER];
    long total_received = 0;
    
    while (total_received < file_size) {
        ssize_t bytes_received = recv(sock_fd, buffer, 
            MIN(sizeof(buffer), file_size - total_received), 0);
        
        if (bytes_received <= 0) {
            print_error("Failed to receive file data");
            return ERR_TRANSFER;
        }
        
        if (fwrite(buffer, 1, bytes_received, file) != (size_t)bytes_received) {
            print_error("Failed to write file data");
            return ERR_TRANSFER;
        }
        
        total_received += bytes_received;
    }
    
    return SUCCESS;
}

/* Validate file exists
 * Checks if the given file exists
 * @param filename: path to the file to check
 * @return: 1 if file exists, 0 otherwise
 */
int validate_file_exists(const char *filename) {
    struct stat st;
    return stat(filename, &st) == 0;
}

/* Validate file type
 * Checks if the given file has a supported extension
 * @param filename: path to the file to check
 * @return: 1 if file type is valid, 0 otherwise
 */
int validate_file_type(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return 0;
    
    return (strcmp(ext, FILE_C) == 0 ||
            strcmp(ext, FILE_PDF) == 0 ||
            strcmp(ext, FILE_TXT) == 0 ||
            strcmp(ext, FILE_ZIP) == 0);
}

/* Validate S1 path
 * Checks if the given path starts with ~/S1
 * @param path: path to check
 * @return: 1 if path is valid, 0 otherwise
 */
int validate_s1_path(const char *path) {
    if (!path) return 0;
    
    // Must start with ~/S1
    if (strncmp(path, "~/S1", 4) != 0) return 0;
    
    // If it's exactly "~/S1", that's valid
    if (path[4] == '\0') return 1;
    
    // If there's more, it must continue with a /
    if (path[4] != '/') return 0;
    
    // Path is valid
    return 1;
}

/* Print error message
 * Prints an error message to the standard error stream
 * @param msg: error message to print
 */
void print_error(const char *msg) {
    fprintf(stderr, "Error: %s\n", msg);
}

/* Print usage
 * Prints the usage information for the program
 */
void print_usage(void) {
    printf("\nAvailable commands:\n");
    printf("  uploadf <filename> <dest_path>  - Upload a file to the server\n");
    printf("  downlf <filepath>               - Download a file from the server\n");
    printf("  removef <filepath>              - Remove a file from the server\n");
    printf("  downltar <filetype>             - Download tar of files by type (.c/.pdf/.txt/.zip)\n");
    printf("  dispfnames <pathname>           - Display filenames in directory\n");
    printf("  help                            - Show this help message\n");
    printf("  exit/quit                       - Exit the program\n\n");
    printf("Notes:\n");
    printf("- All paths must start with ~/S1\n");
    printf("- Supported file types: .c, .pdf, .txt, .zip\n\n");
}

/* Parse command string
 * Parses the command string into a command structure
 * @param cmd_str: command string to parse
 * @param cmd: command structure to store the parsed command
 * @return: SUCCESS on success, ERR_INVALID_CMD on failure
 */
int parse_command(char *cmd_str, struct command *cmd) {
    char *token = strtok(cmd_str, " ");
    if (!token) return ERR_INVALID_CMD;
    
    strncpy(cmd->cmd_type, token, MAX_CMD - 1); // Copy the command type
    cmd->arg1[0] = cmd->arg2[0] = '\0'; // Initialize the arguments
    
    if ((token = strtok(NULL, " "))) strncpy(cmd->arg1, token, MAX_PATH - 1);
    if ((token = strtok(NULL, " "))) strncpy(cmd->arg2, token, MAX_PATH - 1);
    
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
        return ERR_FILE_NOT_FOUND;
    }
    
    /* Get file size */
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);
    
    printf("Debug: Sending file size: %ld bytes\n", file_size);
    
    /* Send file size as text */
    char size_str[32];
    snprintf(size_str, sizeof(size_str), "%ld", file_size);
    if (send(sock_fd, size_str, strlen(size_str), 0) <= 0) {
        printf("Debug: Failed to send file size\n");
        fclose(file);
        return ERR_TRANSFER;
    }
    
    /* Wait for server acknowledgment */
    char ack[3] = {0};
    if (recv(sock_fd, ack, 2, 0) <= 0 || strncmp(ack, "OK", 2) != 0) {
        printf("Debug: Failed to receive server acknowledgment\n");
        fclose(file);
        return ERR_TRANSFER;
    }
    
    /* Send file contents */
    size_t bytes_read;
    long total_sent = 0;
    time_t last_progress = time(NULL);
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        ssize_t bytes_sent = send(sock_fd, buffer, bytes_read, 0);
        if (bytes_sent < 0) {
            printf("Debug: Failed to send file data\n");
            fclose(file);
            return ERR_TRANSFER;
        }
        total_sent += bytes_sent;
        
        /* Show progress every second */
        time_t now = time(NULL);
        if (now != last_progress) {
            fprintf(stderr, "\rSending: %ld/%ld bytes (%.1f%%)", 
                    total_sent, file_size, 
                    (float)total_sent * 100 / file_size);
            fflush(stderr);
            last_progress = now;
        }
    }
    
    if (total_sent > 0) {
        fprintf(stderr, "\n");  // New line after progress
    }
    
    printf("Debug: Successfully sent %ld bytes\n", total_sent);
    fclose(file);
    return SUCCESS;
} 