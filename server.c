#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <arpa/inet.h> 

#define DEFAULT_PORT 69
#define MAX_BUFFER_SIZE 516
#define TIMEOUT_SECONDS 3


void print_message(const struct sockaddr_in *src_addr, const struct sockaddr_in *dst_addr, int opcode, int block_id) {
    char src_ip[INET_ADDRSTRLEN];
    char dst_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(src_addr->sin_addr), src_ip, INET_ADDRSTRLEN);
    
    // Get the source and destination ports
    int src_port = ntohs(src_addr->sin_port);
    int dst_port = (dst_addr != NULL) ? ntohs(dst_addr->sin_port) : -1;

    switch (opcode) {
        case 3:
            fprintf(stderr, "DATA %s:%d:%d %d\n", src_ip, src_port, dst_port, block_id);
            break;
        case 4:
            fprintf(stderr, "ACK %s:%d %d\n", src_ip, src_port, block_id);
            break;
        case 5:
            fprintf(stderr, "ERROR %s:%d:%d %d \"%s\"\n", src_ip, src_port, dst_port, block_id, "Error Message");
            break;
        default:
            fprintf(stderr, "Unknown opcode %d\n", opcode);
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

int recive_ack(int sockfd, struct sockaddr_in client_addr, struct sockaddr_in server_addr, int block_number){
    socklen_t client_len = sizeof(client_addr);
    char ack_buffer[MAX_BUFFER_SIZE];
    ssize_t ack_size;
    time_t start_time = time(NULL);
    time_t current_time;
    while (1) {
        // Check if the timeout has occurred
        current_time = time(NULL);
        if (current_time - start_time >= TIMEOUT_SECONDS) {
            printf("Timeout occurred. No ACK received.\n");
            return -1;  // Timeout occurred
        }

        // Receive ACK packet (non-blocking call)
        ack_size = recvfrom(sockfd, ack_buffer, sizeof(ack_buffer), MSG_DONTWAIT,
                            (struct sockaddr *)&client_addr, &client_len);

        if (ack_size > 0) {
            unsigned short opcode = ntohs(*(unsigned short *)ack_buffer);
            print_message(&client_addr, &server_addr, opcode, block_number);
            if (opcode == 4) {
                // Check if ACK packet is correct
                unsigned short received_block_number = ntohs(*(unsigned short *)(ack_buffer + 2));
                if (received_block_number == block_number) {
                    printf("Block number is correct = %d\n", block_number);
                    // ACK is correct, return 0
                    //(*cnt) == 0;
                    return 0;
                } else if (opcode == 5) {
                    printf("Error package has arrived");
                    return 5;
                } else {
                    // Incorrect ACK, continue waiting for the correct one
                    printf("Block number is not correct = %d %d\n", block_number, received_block_number);
                }
            } else {
                // The received packet is not an ACK, continue waiting
                printf("Received packet is not an ACK (opcode = %d)\n", opcode);
            }
        }

        // Sleep for a short duration before checking again
        usleep(100000);  // 100,000 microseconds (100 milliseconds)
    }
}

void handle_rrq(int sockfd, struct sockaddr_in client_addr, char *filename) {

    char buffer[MAX_BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    struct sockaddr_in server_addr;
    socklen_t server_len = sizeof(server_addr);

    getsockname(sockfd, (struct sockaddr *)&server_addr, &server_len);
    
    int bn = 1;
    int data_size = get_data_block(buffer, bn, filename);

    printf("data sizr is %d\n",data_size);
    while(data_size) {
        sendto(sockfd, buffer, data_size, 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
        while(recive_ack(sockfd, client_addr, server_addr, bn)!=0){
           printf("Again\n");
           sendto(sockfd, buffer, data_size, 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
        }
        bn++;
        data_size = get_data_block(buffer, bn, filename);
    }
    return;
}

void handle_wrq(int sockfd, struct sockaddr_in client_addr, char *filename) {
    
    struct sockaddr_in server_addr;
    socklen_t server_len = sizeof(server_addr);

    getsockname(sockfd, (struct sockaddr *)&server_addr, &server_len);

    FILE *file = fopen(filename, "wb");
    if (file == NULL) {
        send_error(sockfd, client_addr, 2, "Access violation");
        return;
    }

    // Send acknowledgment for the initial request
    char ack_packet[4] = {0, 4, 0, 0};
    sendto(sockfd, ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
    printf("ack is sent\n");
    // Receive data and write to the file
    unsigned short block_number = 1;
    char buffer[MAX_BUFFER_SIZE];
    size_t bytes_received;

    do {
        printf("i am here\n");
        // Receive data packet
        bytes_received = recvfrom(sockfd, buffer, sizeof(buffer), 0, NULL, NULL);
        if (bytes_received < 0) {
            perror("Error receiving data");
            break;
        }
        
        // Check opcode
        unsigned short opcode = ntohs(*(unsigned short *)buffer);
        if (opcode != 3) {
            printf("Illegal TFTP operation opcode is != 3\n");
            send_error(sockfd, client_addr, 4, "Illegal TFTP operation");
            break;
        }

        // Check block number
        unsigned short received_block_number = ntohs(*(unsigned short *)(buffer + 2));
        if (received_block_number != block_number) {
            printf("Illegal TFTP operation opcode is block numbers are different\n");
            send_error(sockfd, client_addr, 4, "Illegal TFTP operation");
            break;
        }
        // printf("Received Data:\n");
        // for (size_t i = 0; i < bytes_received; ++i) {
        //     printf("%02X ", (unsigned char)buffer[i]);
        // }
        // printf("\n");

        // Write data to the file
        fwrite(buffer + 4, 1, bytes_received - 4, file);


        // Send acknowledgment
        ack_packet[2] = (block_number >> 8) & 0xFF;
        ack_packet[3] = block_number & 0xFF;
        sendto(sockfd, ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
        block_number++;
    } while (bytes_received == MAX_BUFFER_SIZE);

    //Close the file
    fclose(file);
    return;
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

    // Get the dynamically assigned port
    socklen_t child_len = sizeof(child_addr);
    if (getsockname(*child_sockfd, (struct sockaddr *)&child_addr, &child_len) == 0) {
        printf("Child process dynamically assigned port: %d\n", ntohs(child_addr.sin_port));
    } else {
        perror("Error getting dynamically assigned port");
    }

    // Close the parent socket in the child process
    close(parent_sockfd);

    return 0;
}

int run_server(int port) {
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
            perror("Error receiving data");
            continue;
        }

        pid_t child_pid = fork();

        if (child_pid < 0) {
            perror("Error forking process");
            continue;
        } else if (child_pid == 0) {
            
            // This is the child process
            int child_sockfd;
            if (bind_child_socket(&child_sockfd, sockfd) == 0) {

                // Handle the TFTP request using child_sockfd
                // Parse TFTP request
                unsigned short opcode = ntohs(*(unsigned short *)buffer);
                char *filename = buffer + 2;  // Skip opcode
                char *mode = filename + strlen(filename) + 1;  // Skip filename

                // Handle TFTP request based on opcode
                switch (opcode) {
                    case 1:  // RRQ
                        printf("rrq parsed\n");
                        handle_rrq(child_sockfd, client_addr, filename);
                        break;
                    case 2:  // WRQ
                        printf("wrq parsed\n");
                        printf("filename is %s\n",filename);
                        handle_wrq(child_sockfd, client_addr, filename);
                        break;
                    default:
                        fprintf(stderr, "Unsupported TFTP opcode: %d\n", opcode);
                        exit(EXIT_FAILURE);
                        break;
                }
                // Exit the child process
                exit(EXIT_SUCCESS);
            } else {
                // Handle error in creating and binding the child socket
               printf("Error in binding new socket\n");
               exit(EXIT_FAILURE);
            }
        } else {
            // This is the parent process
            // Close the client socket in the parent process
            printf("Child process spawned\n");
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

    // Print parsed arguments
    printf("Port: %d\n", port);
    printf("Root Directory Path: %s\n", root_dirpath);

    run_server(port);

    return 0;
}
