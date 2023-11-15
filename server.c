#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h> 

#define DEFAULT_PORT 69
#define MAX_BUFFER_SIZE 516


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


void handle_rrq(int sockfd, struct sockaddr_in client_addr, char *filename) {
    printf("I have the read request\n");
}

void handle_wrq(int sockfd, struct sockaddr_in client_addr, char *filename) {
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
        printf("Received Data:\n");
        for (size_t i = 0; i < bytes_received; ++i) {
            printf("%02X ", (unsigned char)buffer[i]);
        }
        printf("\n");

        // Write data to the file
        fwrite(buffer + 4, 1, bytes_received - 4, file);


        // Send acknowledgment
        ack_packet[2] = (block_number >> 8) & 0xFF;
        ack_packet[3] = block_number & 0xFF;
        sendto(sockfd, ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
        block_number++;
    } while (bytes_received == 516);

    //Close the file
    fclose(file);
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
    int i = 1;
    while (i != 0) {
        // Receive TFTP request
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_received = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &client_len);
        if (bytes_received < 0) {
            perror("Error receiving data");
            continue;
        }

        // Parse TFTP request
        unsigned short opcode = ntohs(*(unsigned short *)buffer);
        char *filename = buffer + 2;  // Skip opcode
        char *mode = filename + strlen(filename) + 1;  // Skip filename

        // Handle TFTP request based on opcode
        switch (opcode) {
            case 1:  // RRQ
                handle_rrq(sockfd, client_addr, filename);
                break;
            case 2:  // WRQ
                printf("wrq parsed\n");
                printf("filename is %s\n",filename);
                handle_wrq(sockfd, client_addr, filename);
                break;
            default:
                fprintf(stderr, "Unsupported TFTP opcode: %d\n", opcode);
                break;
        }
        i--;
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
