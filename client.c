#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>
#include <arpa/inet.h> 

#define DEFAULT_PORT 69
#define MAX_BUFFER_SIZE 516
#define TIMEOUT_SECONDS 3

int send_rrq(int sockfd, struct sockaddr_in server_addr, const char *filename) {
    printf("i send an rrq request\n");
    return 0;
}

int recive_ack(int sockfd, struct sockaddr_in server_addr, int block_number){
 
    socklen_t server_len = sizeof(server_addr);
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
                            (struct sockaddr *)&server_addr, &server_len);

        if (ack_size > 0) {
            // Check if ACK packet is correct
            unsigned short received_block_number = ntohs(*(unsigned short *)(ack_buffer+2));
            if (received_block_number == block_number) {
                printf("Block number is correct = %d\n", block_number);
                // ACK is correct, return 0
                return 0;
            } else {
                // Incorrect ACK, continue waiting for the correct one
                printf("Block number is not correct = %d %d\n", block_number, received_block_number);
            }
        }

        // Sleep for a short duration before checking again
        usleep(100000);  // 100,000 microseconds (100 milliseconds)
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
    while( (recive_ack(sockfd, server_addr, 0)) != 0 ){
        sendto(sockfd, buffer, 2 + strlen(filename) + 1 + strlen("octet") + 1, 0,
           (struct sockaddr *)&server_addr, sizeof(server_addr));
    }
    int bn = 1;
    int data_size = get_data_block(buffer, bn);
    printf("data sizr is %d\n",data_size);
    while(data_size) {
        sendto(sockfd, buffer, data_size, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
        while(recive_ack(sockfd, server_addr,bn)!=0){
           printf("Again\n");
           sendto(sockfd, buffer, data_size, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
        }
        bn++;
        data_size = get_data_block(buffer, bn);
    }

}
void receive_data(int sockfd, struct sockaddr_in server_addr, const char *filename) {
    printf("I receive data\n");
    return;
}

int comunicate(int port, char* address, bool flag) {
   int sockfd;
    struct sockaddr_in server_addr;
    socklen_t server_len = sizeof(server_addr);

    // Create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    // Convert IP address from string to network byte order
    if (inet_pton(AF_INET, address, &(server_addr.sin_addr)) <= 0) {
        perror("Error converting IP address with localhost");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (!(flag)) {
        // Read request
        send_rrq(sockfd, server_addr, "./file_to_read");
        receive_data(sockfd, server_addr, "./file_to_receive");
    } else {
        // Write request
        send_wrq(sockfd, server_addr, "./file_to_write");

    }

    // Close the socket
    close(sockfd);

    return 0; 
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
                break;
            case 'f':
                filepath = optarg;
                break;
            case 't':
                dest_filepath = optarg;
                break;
            default:
                fprintf(stderr, "Usage: %s -h hostname [-p port] [-f filepath] -t dest_filepath\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // Check if required arguments are provided
    if (hostname == NULL || dest_filepath == NULL) {
        fprintf(stderr, "Missing required arguments. Please provide -h and -t.\n");
        exit(EXIT_FAILURE);
    }

    // Print parsed arguments
    printf("Hostname: %s\n", hostname);
    printf("Port: %d\n", (port != -1) ? port : DEFAULT_PORT); // Assuming DEFAULT_PORT is a constant
    printf("Filepath: %s\n", (filepath != NULL) ? filepath : "stdin");
    printf("Dest Filepath: %s\n", dest_filepath);

    bool flag = true;
    comunicate(port, hostname, flag);

    return 0;
}
