#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(int argc, char** argv)
{
    if (argc >= 2)
    {
        int cmpRes = strcmp(argv[1], "--help");
        if (cmpRes == 0)
        {
            printf("usage: client [serverIP] [serverPort] [clientIP] [clientPort]\n");
            exit(EXIT_SUCCESS);
        }    
    }

    char serverIP[32] = "127.0.0.1";
    if (argc >= 2)
        strncpy(serverIP, argv[1], sizeof(serverIP));
    printf("server ip = %s\n", serverIP);

    uint16_t serverPort = 6666;
    if (argc >= 3)
        serverPort = (uint16_t)atoi(argv[2]);
    printf("serverPort = %d\n", (int)serverPort);

    char clientIP[32] = "0.0.0.0";
    if (argc >= 4)
        strncpy(clientIP, argv[3], 32);
    int isUniversalClientIP = 0;
    if (strcmp(clientIP, "0.0.0.0") == 0)
        isUniversalClientIP = 1;
    
    uint16_t clientPort = 0;
    if(argc >= 5)
        clientPort = atoi(argv[4]);

    if (strlen(clientIP) != 0)
        printf("clientIP = %s\n", clientIP);
    else
        printf("clientIP = auto\n");

    if (clientPort != 0)
        printf("clientPort = %d\n", clientPort);
    else
        printf("clientPort = auto\n");

    printf("preparing to connect...\n");
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1)
    {
        fprintf(stderr, "socket() : %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (!isUniversalClientIP || clientPort != 0)
    {
        printf("binding client to %s:%d\n", clientIP, (int)clientPort);

        int enable = 1;
        int setOptRes = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                                   &enable, sizeof(int));
        if (setOptRes == -1)
        {
            fprintf(stderr, "setsockopt() : %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in sinAddr;
        bzero(&sinAddr, sizeof(struct sockaddr_in));
        sinAddr.sin_family = AF_INET;
        sinAddr.sin_port = htons(clientPort);
        if (!isUniversalClientIP)
        {
            int converted = inet_pton(AF_INET, clientIP, &sinAddr.sin_addr.s_addr);
            if (converted != 1)
            {
                fprintf(stderr, "inet_pton() : cannot convert clientIP\n");
                exit(EXIT_FAILURE);
            }
        }
        else
        {
            sinAddr.sin_addr.s_addr = INADDR_ANY;
        }
        int bindedClient = bind(sock, (const struct sockaddr *)&sinAddr, sizeof(sinAddr));
        if (bindedClient == -1)
        {
            fprintf(stderr, "bind() : %s\n", strerror(errno));
            exit(EXIT_FAILURE);            
        }
    }

    printf("connecting to server...\n");
    struct sockaddr_in serverAddr;
    bzero(&serverAddr, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort);
    int convertedServIP = inet_pton(AF_INET, serverIP, &serverAddr.sin_addr.s_addr);
    if (convertedServIP != 1)
    {
        fprintf(stderr, "inet_pton() : cannot convert serverIP\n");
        exit(EXIT_FAILURE);
    }
    
    int connected = connect(sock, (const struct sockaddr *)&serverAddr, sizeof(serverAddr));
    if (connected == -1)
    {
        fprintf(stderr, "connect() : %s\n", strerror(errno));
        exit(EXIT_FAILURE);            
    }

    char recvbuffer[1024];

    while (1)
    {
        int bytesReceived = recv(sock, recvbuffer, 1023, 0);
        if (bytesReceived == -1)
        {
            fprintf(stderr, "recv() : %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        else if (bytesReceived == 0)
        {
            printf("connection closed\n");
            break;
        }
        else
        {
            recvbuffer[bytesReceived] = '\0';
            printf("%s", recvbuffer);
        }
    }
    
    close(sock);
    exit(EXIT_SUCCESS);        
}
