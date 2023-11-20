#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <stdbool.h>
#include <time.h>
#include <arpa/inet.h> 
#include <netdb.h>

#define DEFAULT_PORT 69
#define MAX_BUFFER_SIZE 516
#define TIMEOUT_SECONDS 5

void print_message(const struct sockaddr_in *src_addr, const struct sockaddr_in *dst_addr, int opcode, int block_id, char* e_msg) {
    char src_ip[INET_ADDRSTRLEN];
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
            fprintf(stderr, "ERROR %s:%d:%d \"%s\"\n", src_ip, src_port, dst_port, e_msg+4);
            break;
        default:
            fprintf(stderr, "Unknown opcode %d\n", opcode);
    }
}

int set_data_block(char* buffer, int block_number, const char* filename, size_t len) {
    printf("filename is %s\n", filename);

    FILE *file = fopen(filename, "r+b");  // Try to open the file for reading and writing

    if (file == NULL) {
        // The file does not exist, create a new file
        file = fopen(filename, "w+b");  // Create a new file in write mode

        if (file == NULL) {
            printf("Error creating file");
            return 1;
        }
    }

    size_t block_size = 512;
    size_t position = (block_number - 1) * block_size;
    printf("postition is %ld\n", position);

    // Move the file pointer to the appropriate position
    fseek(file, position, SEEK_SET);
    printf("stlen: %ld\n",strlen(buffer + 4));
    size_t bytesWritten = fwrite(buffer + 4, 1, len-4, file);

    if (bytesWritten != len-4) {
        printf("Error writing data to file");
    } else {
        printf("Data block %d written to file\n", block_number);
    }

    fclose(file);
    return 0;
}

int receive_data(int sockfd, struct sockaddr_in *server_addr, struct sockaddr_in local_addr, const char* filename, int block_number, const char* localpath, int* cnt) {
    socklen_t server_len = sizeof(*server_addr);
    char buffer[MAX_BUFFER_SIZE];
    ssize_t bytes_received;
    time_t start_time = time(NULL);
    time_t current_time;

    while(1) {
        // Receive data packet
        current_time = time(NULL);
        if (current_time - start_time >= TIMEOUT_SECONDS) {
            printf("Timeout occurred. No DATA received.\n");
            (*cnt)++; 
            return 2;  // Timeout occurred
        }

        bytes_received = recvfrom(sockfd, buffer, sizeof(buffer), MSG_DONTWAIT, (struct sockaddr *)server_addr, &server_len);
        
        //printf("bytes recived: %ld\n", bytes_received);
        if (bytes_received > 0) {
            unsigned short opcode = ntohs(*(unsigned short *)buffer);
            print_message(server_addr, &local_addr, opcode, block_number, buffer);
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
            // printf("Received Data:\n");
            // for (size_t i = 0; i < bytes_received; ++i) {
            //     printf("%c", buffer[i]);
            // }
            // printf("\n");
            set_data_block(buffer, block_number, localpath, bytes_received);
            printf("data_processed\n"); 
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

int recive_ack(int sockfd, struct sockaddr_in *server_addr, struct sockaddr_in local_addr, int block_number, int* cnt){

    socklen_t server_len = sizeof(*server_addr);
    char ack_buffer[MAX_BUFFER_SIZE];
    ssize_t ack_size;
    time_t start_time = time(NULL);
    time_t current_time;
    while (1) {
        // Check if the timeout has occurred
        current_time = time(NULL);
        if (current_time - start_time >= TIMEOUT_SECONDS) {
            printf("Timeout occurred. No ACK received.\n");
            (*cnt)++;
            return -1;  // Timeout occurred
        }

        // Receive ACK packet (non-blocking call)
        ack_size = recvfrom(sockfd, ack_buffer, sizeof(ack_buffer), MSG_DONTWAIT,
                            (struct sockaddr *)server_addr, &server_len);

        if (ack_size > 0) {
            // Check if the received packet is an ACK
            unsigned short opcode = ntohs(*(unsigned short *)ack_buffer);
            print_message(server_addr, &local_addr, opcode, block_number, ack_buffer);
            if (opcode == 4) {
                // Check if ACK packet is correct
                unsigned short received_block_number = ntohs(*(unsigned short *)(ack_buffer + 2));
                if (received_block_number == block_number) {
                    printf("Block number is correct = %d\n", block_number);
                    // ACK is correct, return 0
                    (*cnt) = 0;
                    return 0;
                } else {
                    // Incorrect ACK, continue waiting for the correct one
                    printf("Block number is not correct = %d %d\n", block_number, received_block_number);
                }
            } else if (opcode == 5) {
                    printf("Error package has arrived\n");
                    return 5;
            } else {
                // The received packet is not an ACK, continue waiting
                printf("Received packet is not an ACK (opcode = %d)\n", opcode);
            }
        }

        // Sleep for a short duration before checking again
        usleep(100000);  // 100,000 microseconds (100 milliseconds)
    }
}

int send_rrq(int sockfd, struct sockaddr_in server_addr, const char *filename, const char* localpath) {
    
    char buffer[MAX_BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    // Set opcode (RRQ)
    *(unsigned short *)buffer = htons(1);
    // Set filename
    strcpy(buffer + 2, filename);
    // Set mode (octet)
    strcpy(buffer + 2 + strlen(filename) + 1, "octet");

    // Send RRQ packet
    sendto(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));

    // Get local address in order to get dynamicly assigned port number
    struct sockaddr_in local_addr;
    socklen_t local_len = sizeof(local_addr);
    getsockname(sockfd, (struct sockaddr*)&local_addr, &local_len);
    int bn = 1;
    char ack_packet[4] = {0, 4, 0, 1};
    int cnt = 0;
    while(1) {
        if( cnt > 3 ) {
            printf("Timeout has occurred, can not recive DATA from server, terminate commuication\n");
            return 1;
        }
        int received_data = receive_data(sockfd, &server_addr, local_addr, filename, bn, localpath, &cnt);
        if (received_data == 0){ // okay, send next
            ack_packet[2] = (bn >> 8) & 0xFF;
            ack_packet[3] = bn & 0xFF;
            sendto(sockfd, ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
            bn++;
        } else if (received_data == 1) { // last packet recieved
            ack_packet[2] = (bn >> 8) & 0xFF;
            ack_packet[3] = bn & 0xFF;
            sendto(sockfd, ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
            return 0;
        } else if (received_data == 2){ // error in data reciving stect
            sendto(sockfd, ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
        } else if (received_data == 2 && bn == 1) { // error int rrq sending step
            sendto(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
        } else if ( received_data == 5 ) { //error package 
            return received_data;
        }
    }
}

int get_data_block(char* buffer, int block_number) {
    // Read data from stdin
    size_t bytesRead = fread(buffer + 4, 1, 512, stdin);

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

int send_wrq(int sockfd, struct sockaddr_in server_addr, const char *filename) {
    char buffer[MAX_BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    // Set WRQ opcode
    *(unsigned short *)buffer = htons(2);
    // Set filename
    strcpy(buffer + 2, filename);
    // Set mode (octet)
    strcpy(buffer + 2 + strlen(filename) + 1, "octet");

    // Send WRQ packet
    sendto(sockfd, buffer, 2 + strlen(filename) + 1 + strlen("octet") + 1, 0,
           (struct sockaddr *)&server_addr, sizeof(server_addr));

    // Getting local address in order to get dynamicly assigned port number
    struct sockaddr_in local_addr;
    socklen_t local_len = sizeof(local_addr);
    getsockname(sockfd, (struct sockaddr*)&local_addr, &local_len);
    int result;
    int cnt = 0;
    while( (result = recive_ack(sockfd, &server_addr, local_addr, 0, &cnt)) != 0 ){
        if( cnt > 3 ) {
            printf("Timeout has occurred, cannot recive ACK from server, terminate communication\n");
            return 1;
        }
        if (result == 5)
            return result;
        sendto(sockfd, buffer, 2 + strlen(filename) + 1 + strlen("octet") + 1, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
    }

    int bn = 1;
    int data_size = get_data_block(buffer, bn);
    while(data_size) {
        sendto(sockfd, buffer, data_size, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
        while(recive_ack(sockfd, &server_addr, local_addr, bn, &cnt)!=0){
            if( cnt > 3 ) {
                printf("Timeout has occurred, cannot recive ACK from server\n");
                return 1;
            }
           sendto(sockfd, buffer, data_size, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
        }
        bn++;
        data_size = get_data_block(buffer, bn);
    }

    return 0;

}

int comunicate(int port, char* address, bool flag, char* filename, char* localpath) {
    int sockfd;
    struct sockaddr_in server_addr;

    // Create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        printf("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    // Convert IP address from string to network byte order
    if (inet_pton(AF_INET, address, &(server_addr.sin_addr)) <= 0) {
        // If inet_pton fails, it might not be a valid IPv4 address

        // Resolve domain name to IP address
        struct hostent *host_info = gethostbyname(address);
        if (host_info == NULL) {
            printf("Error converting or resolving address");
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        // Copy the resolved IP address to the sockaddr_in structure
        memcpy(&(server_addr.sin_addr), host_info->h_addr, host_info->h_length);
    }

    int result;

    if (flag) {
        // Write request
        result = send_wrq(sockfd, server_addr, filename);
    } else {
        // Read request
        result = send_rrq(sockfd, server_addr, localpath, filename);
    }

    // Close the socket
    close(sockfd);

    return result; 
}

int main(int argc, char *argv[]) {
    int opt;
    char *hostname = NULL;
    int port = -1;
    char *filepath = NULL;
    char *dest_filepath = NULL;

    while ((opt = getopt(argc, argv, "h:p:f:t:")) != -1) {
        switch (opt) {
            case 'h':
                hostname = optarg;
                break;
            case 'p':
                port = atoi(optarg);
                if (port <= 0 || port > 65535) {
                    printf("Invalid port number: %s\n", optarg);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'f':
                filepath = optarg;
                break;
            case 't':
                dest_filepath = optarg;
                break;
            default:
                printf("Usage: %s -h hostname [-p port] [-f filepath] -t dest_filepath\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // Check if required arguments are provided
    if (hostname == NULL || dest_filepath == NULL) {
        printf("Missing required arguments. Please provide -h and -t.\n");
        exit(EXIT_FAILURE);
    }

    // Check for extra arguments after options
    if (optind < argc) {
        printf("Unexpected arguments:");
        while (optind < argc) {
            printf(" %s", argv[optind++]);
        }
        printf("\n");
        exit(EXIT_FAILURE);
    }

    bool wrqflag;
    if (filepath == NULL) {
        wrqflag = true;
    } else {
        wrqflag = false;
        if (access(dest_filepath, F_OK) == 0) {
        printf("Error, file already exists: %s\n", dest_filepath);
        exit(EXIT_FAILURE);
        }
    }

    int result = comunicate(port, hostname, wrqflag, dest_filepath, filepath);

    return result;
}