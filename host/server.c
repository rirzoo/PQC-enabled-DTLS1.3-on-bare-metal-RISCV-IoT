#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVER_PORT 1234
#define BUFFER_SIZE 1024

int main(void) {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    char buffer[BUFFER_SIZE];
    ssize_t recv_len;

    /* Create UDP socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    /* Zero out the server address structure */
    memset(&server_addr, 0, sizeof(server_addr));

    /* Configure server address */
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("192.168.1.100");  /* Listen on 192.168.1.100 */
    server_addr.sin_port = htons(SERVER_PORT);        /* Port 1234 */

    /* Bind the socket to the address and port */
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("UDP server listening on port %d\n", SERVER_PORT);

    client_len = sizeof(client_addr);

    while (1) {
        /* Receive data from a client */
        recv_len = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
                            (struct sockaddr *)&client_addr, &client_len);
        if (recv_len < 0) {
            perror("recvfrom failed");
            continue;
        }

        /* First send greeting message to the client */
        const char *greeting = "Hi I am the linux server";
        if (sendto(sockfd, greeting, strlen(greeting), 0,
                   (struct sockaddr *)&client_addr, client_len) < 0) {
            perror("sendto greeting failed");
        }

        buffer[recv_len] = '\0'; /* Null-terminate received data */

        /* Now print the incoming message from client */
        printf("Received %zd bytes from %s:%d: %s\n",
               recv_len,
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port),
               buffer);
    }

    close(sockfd);
    return 0;
}