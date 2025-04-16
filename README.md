# Distributed File System

A distributed file system implementation that handles different file types across multiple servers. The system consists of four servers (S1, S2, S3, and S4), each specialized in handling specific file types.

## Architecture

- **S1 (Main Server)**: Handles `.c` files and coordinates with other servers
- **S2**: Handles `.pdf` files
- **S3**: Handles `.txt` files
- **S4**: Handles `.zip` files
- **Client**: Provides user interface to interact with the distributed file system

## Features

- File upload and download across distributed servers
- Automatic file type routing to appropriate servers
- Directory listing across all servers
- File removal from any server
- TAR archive creation for specific file types
- Robust error handling and debugging
- Path validation and security checks

## Commands

1. **Upload File** (`uploadf`):
   ```
   uploadf <source_file> <destination_path>
   ```
   Uploads a file to the appropriate server based on its extension.

2. **Download File** (`downlf`):
   ```
   downlf <file_path>
   ```
   Downloads a file from the appropriate server.

3. **Remove File** (`removef`):
   ```
   removef <file_path>
   ```
   Removes a file from the appropriate server.

4. **Download TAR** (`downltar`):
   ```
   downltar <file_type>
   ```
   Creates and downloads a TAR archive of all files of the specified type.

5. **Display File Names** (`dispfnames`):
   ```
   dispfnames <directory_path>
   ```
   Lists all files in the specified directory across all servers.

## Path Format

- All paths must start with `~/S1/`
- The system automatically maps paths to the appropriate server
- Example: `~/S1/folder/file.txt` will be mapped to `~/S3/folder/file.txt` for text files

## Building and Running

1. Compile all servers:
   ```bash
   gcc -o S1 S1.c
   gcc -o S2 S2.c
   gcc -o S3 S3.c
   gcc -o S4 S4.c
   ```

2. Start the servers in order:
   ```bash
   ./S4
   ./S3
   ./S2
   ./S1
   ```

## Directory Structure

The system creates the following directory structure in the user's home directory:
```
~/
├── S1/    # For .c files
├── S2/    # For .pdf files
├── S3/    # For .txt files
└── S4/    # For .zip files
```

## Error Handling

- Invalid path format
- File not found
- Server not available
- Permission denied
- Invalid file types
- Network communication errors

## Debug Messages

The system includes comprehensive debug messages that can help in:
- Tracking file operations
- Monitoring server communications
- Diagnosing connection issues
- Verifying path mappings
- Troubleshooting file transfers

## Implementation Details

- Uses TCP sockets for inter-server communication
- Implements file transfer protocol with size prefixing
- Supports concurrent client connections through forking
- Includes timeout mechanisms for network operations
- Uses temporary files for secure file transfers
- Implements proper cleanup on program termination

## Security Features

- Path validation to prevent directory traversal
- File permission checks
- Secure temporary file handling
- Input validation for all commands
- Error handling for malformed requests

## Network Ports

- S1: 5600
- S2: 5601
- S3: 5602
- S4: 5603

## Requirements

- Linux/Unix operating system
- GCC compiler
- Standard C libraries
- Network connectivity between servers
- Write permissions in home directory

## Client Implementation

### Building the Client
```bash
gcc -o dfs_client client.c
```

### Client Usage
```bash
./dfs_client <command> [arguments]
```

### Client Features
- Direct connection to S1 (main server)
- Command-line interface for all file operations
- Automatic file type detection
- Progress indicators for file transfers
- Error reporting and status messages
- Session management

### Example Client Commands
```bash
# Upload a C file
./dfs_client uploadf ~/mycode.c ~/S1/projects/

# Download a PDF file
./dfs_client downlf ~/S1/documents/report.pdf

# List files in a directory
./dfs_client dispfnames ~/S1/projects/

# Remove a text file
./dfs_client removef ~/S1/notes/todo.txt

# Create and download a tar of all PDF files
./dfs_client downltar .pdf
```

### Client Error Handling
- Connection failure recovery
- Timeout handling
- Invalid command detection
- File access error reporting
- Server availability checking

### Client Configuration
- Default connection to localhost:5600 (S1)
- Configurable timeout settings
- Adjustable buffer sizes for file transfers
- Debug mode for verbose output 
