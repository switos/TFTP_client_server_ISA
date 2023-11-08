#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <arpa/inet.h> 

#define DEFAULT_PORT 69
#define MAX_BUFFER_SIZE 516

int send_rrq(int sockfd, struct sockaddr_in server_addr, const char *filename) {
    printf("i send an rrq request\n");
    return 0;
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
