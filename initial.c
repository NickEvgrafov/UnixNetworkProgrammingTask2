#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static const char* messageToClient = "Hi there\n";

int main(int argc, char** argv)
{
    if (argc >= 2)
    {
        int cmpRes = strcmp(argv[1], "--help");
        if (cmpRes == 0)
        {
            printf("usage: initial [serverPort]\n");
            exit(EXIT_SUCCESS);
        }    
    }

    uint16_t port = 6666;
    if (argc >= 2)
        port = (uint16_t)atoi(argv[1]);
    printf("server port = %d\n", port);
    
    int masterSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (masterSocket == -1)
    {
        fprintf(stderr, "socket() : %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    int enable = 1;
    int setOptRes = setsockopt(masterSocket, SOL_SOCKET, SO_REUSEADDR,
                               &enable, sizeof(int));
    if (setOptRes == -1)
    {
        fprintf(stderr, "setsockopt() : %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in inaddr;
    bzero(&inaddr, sizeof(inaddr));
    inaddr.sin_family = AF_INET;
    inaddr.sin_port = htons(port);
    inaddr.sin_addr.s_addr = INADDR_ANY;
    int binded = bind(masterSocket, (const struct sockaddr *)&inaddr,
                      sizeof(inaddr));
    if (binded == -1)
    {
        fprintf(stderr, "bind() : %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    int listened = listen(masterSocket, SOMAXCONN);
    if (listened == -1)
    {
        fprintf(stderr, "listen() : %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    printf("ready to accept client connections...\n");
    while (1)
    {
        struct sockaddr_in clientInAddr;
        socklen_t clientInAddrLen = sizeof(clientInAddr);
        bzero(&clientInAddr, sizeof(struct sockaddr_in));
        printf("waiting for client...\n");
        int slaveSocket = accept(masterSocket, (struct sockaddr *)(&clientInAddr),
                                 &clientInAddrLen);
        if (slaveSocket == -1)
        {
            if (slaveSocket == EINTR)
            {
                printf("signal occured... continue accepting...");
                continue;
            }
            else
            {
                fprintf(stderr, "accept() : %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }
        }

        char buffer[INET_ADDRSTRLEN];
        const char* clientIpStr = inet_ntop(AF_INET, &clientInAddr.sin_addr, buffer, INET_ADDRSTRLEN);
        int clientPort = (int)clientInAddr.sin_port;
        if (clientIpStr == NULL)
        {
            fprintf(stderr, "inet_ntop() : %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        printf("accepted request from %s:%d\n", clientIpStr, clientPort);

        int sndCount = 0;
        int messageSize = strlen(messageToClient);
        for (; sndCount < 5; ++sndCount)
        {
            printf("sending packet number %d to %s:%d\n", sndCount + 1,
                   clientIpStr, clientPort);
            int sent = sendto(slaveSocket, messageToClient, messageSize, MSG_NOSIGNAL,
                              (struct sockaddr *)(&clientInAddr), sizeof(clientInAddr));
            if (sent == -1)
            {
                if (errno = EPIPE)
                {
                    printf("outgoing connection closed: %s:%d\n", clientIpStr, clientPort);
                    break;
                }
                else
                {
                    fprintf(stderr, "sendto() : %s\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }
            }

            sleep(1);
        }

        printf("closing connection: %s:%d\n", clientIpStr, clientPort);
        close(slaveSocket);
    }

    close(masterSocket);
    exit(EXIT_SUCCESS);
}

