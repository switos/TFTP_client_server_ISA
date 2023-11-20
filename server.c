#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <arpa/inet.h> 
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdbool.h>

#define DEFAULT_PORT 69
#define MAX_BUFFER_SIZE 516
#define TIMEOUT_SECONDS 3

void print_message(const struct sockaddr_in *src_addr, const struct sockaddr_in *dst_addr, int block_id, char* buffer, const char *opts) {
    char src_ip[INET_ADDRSTRLEN];

    inet_ntop(AF_INET, &(src_addr->sin_addr), src_ip, INET_ADDRSTRLEN);

    // Get the source and destination ports
    int src_port = ntohs(src_addr->sin_port);
    int dst_port = (dst_addr != NULL) ? ntohs(dst_addr->sin_port) : -1;

    unsigned short opcode = ntohs(*(unsigned short *)buffer);
    char *filename = buffer + 2;  // Skip opcode
    char *mode = filename + strlen(filename) + 1;  // Skip filename

    switch (opcode) {
        case 1:  // RRQ
        case 2:  // WRQ
            fprintf(stderr, "%s %s:%d \"%s\" %s\n", (opcode == 1) ? "RRQ" : "WRQ", src_ip, src_port, filename, mode);
            break;
        case 3:  // DATA
            fprintf(stderr, "DATA %s:%d:%d %d\n", src_ip, src_port, dst_port, block_id);
            break;
        case 4:  // ACK
            fprintf(stderr, "ACK %s:%d %d\n", src_ip, src_port, block_id);
            break;
        case 5:  // ERROR
            fprintf(stderr, "ERROR %s:%d:%d \"%s\"\n", src_ip, src_port, dst_port, buffer+4);
            break;
    }
}

void send_error(int sockfd, struct sockaddr_in client_addr, unsigned short error_code, const char *error_msg) {
    char buffer[MAX_BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    // Set error opcode
    *(unsigned short *)buffer = htons(5);
    // Set error code
    *(unsigned short *)(buffer + 2) = htons(error_code);
    // Set error message
    strcpy(buffer + 4, error_msg);

    // Send error packet
    sendto(sockfd, buffer, strlen(error_msg) + 5, 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
}

int get_data_block(char* buffer, int block_number, char* filename) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror("Error opening file");
        return -1;
    }

    // Seek to the appropriate position based on block_number
    fseek(file, (block_number - 1) * 512, SEEK_SET);

    // Read data from the file
    size_t bytesRead = fread(buffer + 4, 1, 512, file);

    fclose(file);

    if (bytesRead == 0) {
        // No more data to read, return 0 to signal end of file
        return 0;
    }

    // Set opcode for data (3)
    *(unsigned short *)buffer = htons(3);

    // Set block number
    *(unsigned short *)(buffer + 2) = htons(block_number);

    // Return the total size of the data block (header + data)
    return bytesRead + 4;
}

int recive_ack(int sockfd, struct sockaddr_in client_addr, struct sockaddr_in server_addr, int block_number, int* cnt, int timeout){
    socklen_t client_len = sizeof(client_addr);
    char ack_buffer[MAX_BUFFER_SIZE];
    ssize_t ack_size;
    time_t start_time = time(NULL);
    time_t current_time;
    while (1) {
        // Check if the timeout has occurred
        current_time = time(NULL);
        if (current_time - start_time >= timeout) {
            printf("Timeout occurred. No ACK received.\n");
            (*cnt)++;
            return -1;  // Timeout occurred
        }

        // Receive ACK packet (non-blocking call)
        ack_size = recvfrom(sockfd, ack_buffer, sizeof(ack_buffer), MSG_DONTWAIT,
                            (struct sockaddr *)&client_addr, &client_len);

        if (ack_size > 0) {
            unsigned short opcode = ntohs(*(unsigned short *)ack_buffer);
            print_message(&client_addr, &server_addr, block_number, ack_buffer, "opts");
            if (opcode == 4) {
                // Check if ACK packet is correct
                unsigned short received_block_number = ntohs(*(unsigned short *)(ack_buffer + 2));
                if (received_block_number == block_number) {
                    // ACK is correct, return 0
                    (*cnt) = 0;
                    return 0;
                } else if (opcode == 5) {
                    printf("Error package has arrived");
                    return 5;
                }
            } 
        }

        // Sleep for a short duration before checking again
        usleep(100000);  // 100,000 microseconds (100 milliseconds)
    }
}

int handle_rrq(int sockfd, struct sockaddr_in client_addr, char *filename, bool mode) {

    char buffer[MAX_BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    struct sockaddr_in server_addr;
    socklen_t server_len = sizeof(server_addr);

    getsockname(sockfd, (struct sockaddr *)&server_addr, &server_len);
    
    int bn = 1;
    int data_size = get_data_block(buffer, bn, filename);
    int cnt = 0;
    int result;
    
    while(data_size) {
        sendto(sockfd, buffer, data_size, 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
        while((result = recive_ack(sockfd, client_addr, server_addr, bn, &cnt, TIMEOUT_SECONDS))!=0){
            if( cnt > 3 ) {
                printf("Timeout has occurred, cannot recive ACK from server, terminate communication\n");
                return 1;
            }
            if (result == 5)
                return result;
            sendto(sockfd, buffer, data_size, 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
        }
        bn++;
        data_size = get_data_block(buffer, bn, filename);
    }
    return 0;
}

int set_data_block(int sockfd, struct sockaddr_in client_addr, char* buffer, int block_number, const char* filename, size_t len) {


    FILE *file = fopen(filename, "r+b");  // Try to open the file for reading and writing

    if (file == NULL) {
        // The file does not exist, create a new file
        file = fopen(filename, "w+b");  // Create a new file in write mode

        if (file == NULL) {
            send_error(sockfd, client_addr, 2, "Access violation");
            return 1;
        }
    }

    size_t block_size = 512;
    size_t position = (block_number - 1) * block_size;
  

    // Move the file pointer to the appropriate position
    fseek(file, position, SEEK_SET);

    fwrite(buffer + 4, 1, len-4, file);



    fclose(file);
    return 0;
}

int receive_data(int sockfd, struct sockaddr_in client_addr, struct sockaddr_in server_addr, const char* filepath, int block_number, int* cnt, int timeout, bool mode) {
    socklen_t clinet_len = sizeof(client_addr);
    char buffer[MAX_BUFFER_SIZE];
    ssize_t bytes_received;
    time_t start_time = time(NULL);
    time_t current_time;

    while(1) {
        // Receive data packet
        current_time = time(NULL);
        if (current_time - start_time >= timeout) {
            printf("Timeout occurred. No DATA received.\n");
            (*cnt)++; 
            return 2;  // Timeout occurred
        }

        bytes_received = recvfrom(sockfd, buffer, sizeof(buffer), MSG_DONTWAIT, (struct sockaddr *)&client_addr, &clinet_len);
        
        if (bytes_received > 0) {
            unsigned short opcode = ntohs(*(unsigned short *)buffer);
            print_message(&client_addr, &server_addr, block_number, buffer, "opts");
            if (opcode != 3) {
                if (opcode == 5) {
                    printf("Error package has arrived\n");
                    return 5;
                }
                printf("Received packet is not a data packet (opcode != 3)\n");
                return 2;
            }
            
            
            // Check block number
            unsigned short received_block_number = ntohs(*(unsigned short *)(buffer + 2));
            if (received_block_number != block_number) {
                printf("Received unexpected block number: %d\n", received_block_number);
                return 2;
            }
            set_data_block(sockfd, client_addr, buffer, block_number, filepath, bytes_received);
            (*cnt) = 0;
            if(bytes_received < MAX_BUFFER_SIZE) {
                return 1;
            } else {
                return 0;
            }
        }
        usleep(100000);
    }
}

int handle_wrq(int sockfd, struct sockaddr_in client_addr, char *filename, bool mode) {
    // Send acknowledgment for the initial request
    char ack_packet[4] = {0, 4, 0, 0};
    sendto(sockfd, ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)&client_addr, sizeof(client_addr));

    struct sockaddr_in local_addr;
    socklen_t local_len = sizeof(local_addr);
    getsockname(sockfd, (struct sockaddr*)&local_addr, &local_len);
    
    int bn = 1;
    int cnt = 0;
    int received_data;

    while(1) {
        if( cnt > 3 ) {
            printf("Timeout has occurred, can not recive DATA from server, terminate communication\n");
            return 1;
        }
        received_data = receive_data(sockfd, client_addr, local_addr, filename, bn, &cnt, TIMEOUT_SECONDS, mode);
        if (received_data == 0){ // okay, send next
            ack_packet[2] = (bn >> 8) & 0xFF;
            ack_packet[3] = bn & 0xFF;
            sendto(sockfd, ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
            bn++;
        } else if (received_data == 1) { // last packet recieved
            ack_packet[2] = (bn >> 8) & 0xFF;
            ack_packet[3] = bn & 0xFF;
            sendto(sockfd, ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
            return 0;
        } else if (received_data == 2){ // error in data reciving step
            sendto(sockfd, ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
        } 
    }

}

int handle_queries(int child_sockfd, struct sockaddr_in client_addr, char buffer[512], const char* dir) {

    struct sockaddr_in server_addr;
    socklen_t server_len = sizeof(server_addr);

    getsockname(child_sockfd, (struct sockaddr *)&server_addr, &server_len);

    unsigned short opcode = ntohs(*(unsigned short *)buffer);
    char *filename = buffer + 2;  // Skip opcode
    char *mode = filename + strlen(filename) + 1;  // Skip filename

    print_message(&client_addr, &server_addr, 0, buffer, "opts");

    bool mode_flag;

    if (strcmp(mode, "netascii") == 0) {
        send_error(child_sockfd, client_addr, 0, "Unsupported mode\n");
        return 1;
    } else if (strcmp(mode, "octet") == 0) {
        mode_flag = false;
    } else { 
        send_error(child_sockfd, client_addr, 0, "Unsupported mode\n");
        return 1;
    }

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", dir, filename);


    switch (opcode) {
        case 1:  // RRQ
            if (access(filepath, F_OK) == -1) {
                printf("Error, file do not exist: %s\n", filepath);
                send_error(child_sockfd, client_addr, 2, "File do not exists");
                return 1;
            }
            handle_rrq(child_sockfd, client_addr, filepath, mode_flag);
            break;
        case 2:  // WRQ
            if (access(filepath, F_OK) == 0) {
                printf("Error, file exists: %s\n", filepath);
                send_error(child_sockfd, client_addr, 6, "File already exists");
                return 1;
            }
            handle_wrq(child_sockfd, client_addr, filepath, mode_flag);
            break;
        default:
            send_error(child_sockfd, client_addr, 0,"Unsupported package");
            return 1;
            break;
    }
    return 0;
}

int bind_child_socket(int *child_sockfd, int parent_sockfd) {
    *child_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (*child_sockfd < 0) {
        perror("Error creating socket in child process");
        return -1;
    }

   // Bind child socket to an available port
    struct sockaddr_in child_addr;
    memset(&child_addr, 0, sizeof(child_addr));
    child_addr.sin_family = AF_INET;
    child_addr.sin_addr.s_addr = INADDR_ANY;
    child_addr.sin_port = 0;  // Set port to 0 for dynamic assignment

    // Bind child socket to the child address
    if (bind(*child_sockfd, (struct sockaddr *)&child_addr, sizeof(child_addr)) < 0) {
        perror("Error binding socket in child process");
        close(*child_sockfd);
        return -1;
    }



    // Close the parent socket in the child process
    close(parent_sockfd);

    return 0;
}

int run_server(int port, const char* dir) {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[MAX_BUFFER_SIZE];

    // Create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // Bind socket to server address
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error binding socket");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    while (1) {
        // Receive TFTP request
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_received = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &client_len);
        if (bytes_received < 0) {
            printf("Error receiving data\n");
            continue;
        }

        pid_t child_pid = fork();

        if (child_pid < 0) {
            printf("Error forking process\n");
            continue;
        } else if (child_pid == 0) {
            
            // This is the child process
            int child_sockfd;
            if (bind_child_socket(&child_sockfd, sockfd) == 0) {
                // Handle TFTP request based on opcode
                handle_queries(child_sockfd, client_addr, buffer, dir);
                // Exit the child process
                close(child_sockfd);
                exit(EXIT_SUCCESS);
            } else {
                // Handle error in creating and binding the child socket
               printf("Error in binding new socket\n");
               exit(EXIT_FAILURE);
            }
        }
    }

    // Close the socket (never reached in this example)
    close(sockfd);

    return 0;
}

int main(int argc, char *argv[]) {
    int opt;
    int port = DEFAULT_PORT;
    char *root_dirpath = NULL;

    while ((opt = getopt(argc, argv, "p:")) != -1) {
        switch (opt) {
            case 'p':
                port = atoi(optarg);
                break;
            default:
                fprintf(stderr, "Usage: %s [-p port] root_dirpath\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // Check if the root directory path is provided
    if (optind >= argc) {
        fprintf(stderr, "Missing root directory path. Please provide the root_dirpath.\n");
        exit(EXIT_FAILURE);
    } else {
        root_dirpath = argv[optind];
    }


    const char* root_dirpath_const = root_dirpath;

    DIR *dir = opendir(root_dirpath);
    if (dir == NULL) {
        if (mkdir(root_dirpath, 0777) != 0) {
            printf("Error creating directory\n");
            return -1; // Error creating directory
        }
    }
    run_server(port, root_dirpath_const);
    return 0;
}