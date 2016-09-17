#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/wait.h>
#include <signal.h>
#include <assert.h>

static const char* messageToClient = "Hi there\n";

int create_tcp_socket()
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        fprintf(stderr, "socket() : %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    return sockfd;
}

void set_reuse_addr_opt(int sockfd)
{
    int enable = 1;
    int setOptRes = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
                               &enable, sizeof(int));
    if (setOptRes == -1)
    {
        fprintf(stderr, "setsockopt() : %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void bind_server_socket(int sockfd, uint16_t port)
{
    struct sockaddr_in inaddr;
    bzero(&inaddr, sizeof(inaddr));
    inaddr.sin_family = AF_INET;
    inaddr.sin_port = htons(port);
    inaddr.sin_addr.s_addr = INADDR_ANY;
    int binded = bind(sockfd, (const struct sockaddr *)&inaddr,
                      sizeof(inaddr));
    if (binded == -1)
    {
        fprintf(stderr, "bind() : %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void listen_tcp_socket(int sockfd)
{
    int listened = listen(sockfd, SOMAXCONN);
    if (listened == -1)
    {
        fprintf(stderr, "listen() : %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

static int childrenStarted = 0;
static int childrenFinished = 0;

void sig_chld(int signo)
{
    pid_t pid = -1;
    int stat = 0;

    while((pid = waitpid(-1, &stat, WNOHANG)) > 0)
    {
        printf("child %d terminated\n", (int)pid);
        ++childrenFinished;
    }
    return;
}

typedef void (*sighandler_t)(int);
void set_signal_handler(int sigNumber, const char* sigPresentation,
                        sighandler_t handler)
{
    struct sigaction sigact;
    bzero(&sigact, sizeof(struct sigaction));
    sigact.sa_handler = handler;
    int sigActionSet = sigaction(sigNumber, &sigact, NULL);
    if (sigActionSet == -1)
    {
        fprintf(stderr, "sigaction(%s) : %s\n", sigPresentation, strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void set_sigchld_handler()
{
    set_signal_handler(SIGCHLD, "SIGCHLD", sig_chld);
}

static int needToFinish = 0;
void sig_int()
{
    needToFinish = 1;
    printf("process %d has received SIGINT signal\n", (int)getpid());
}

void set_sigint_handler()
{
    set_signal_handler(SIGINT, "SIGINT", sig_int);
}

int main(int argc, char** argv)
{
    if (argc >= 2)
    {
        int cmpRes = strcmp(argv[1], "--help");
        if (cmpRes == 0)
        {
            printf("usage: perrequest [serverPort]\n");
            exit(EXIT_SUCCESS);
        }
    }

    uint16_t port = 6666;
    if (argc >= 2)
        port = (uint16_t)atoi(argv[1]);
    printf("server port = %d\n", port);

    set_sigchld_handler();
    set_sigint_handler();

    int masterSocket = create_tcp_socket();
    set_reuse_addr_opt(masterSocket);
    bind_server_socket(masterSocket, port);
    listen_tcp_socket(masterSocket);

    pid_t mainPid = getpid();
    pid_t myPid = mainPid;

    printf("main server process: pid = %d\n", (int)(myPid));
    
    while (1)
    {
        struct sockaddr_in clientInAddr;
        socklen_t clientInAddrLen = sizeof(clientInAddr);
        bzero(&clientInAddr, sizeof(struct sockaddr_in));
        printf("%d: waiting for client...\n", (int)myPid);
        int slaveSocket = accept(masterSocket, (struct sockaddr *)(&clientInAddr),
                                 &clientInAddrLen);
        if (slaveSocket == -1)
        {
            if (errno == EINTR)
            {
                if (needToFinish)
                {
                    printf("%d: stop working\n", (int)myPid);
                    break;
                }
                else
                {
                    printf("%d: signal occured... continue accepting...\n", (int)myPid);
                    continue;
                }
            }
            else
            {
                fprintf(stderr, "%d: accept() : %s\n", (int)myPid, strerror(errno));
                exit(EXIT_FAILURE);
            }
        }

        char buffer[INET_ADDRSTRLEN];
        const char* clientIpStr = inet_ntop(AF_INET, &clientInAddr.sin_addr, buffer, INET_ADDRSTRLEN);
        int clientPort = (int)clientInAddr.sin_port;
        if (clientIpStr == NULL)
        {
            fprintf(stderr, "%d: inet_ntop() : %s\n", (int)myPid, strerror(errno));
            exit(EXIT_FAILURE);
        }
        printf("%d: accepted request from %s:%d\n", (int)myPid, clientIpStr, clientPort);

        pid_t pid = fork();
        if (pid == -1)
        {
            fprintf(stderr, "fork() : %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        else if (pid == 0)
        { // this is a child process
            myPid = getpid();
            printf("additional server process: pid = %d\n", (int)(myPid));
            close(masterSocket);

            int sndCount = 0;
            int messageSize = strlen(messageToClient);
            for (; sndCount < 5; ++sndCount)
            {
                printf("%d: sending packet number %d to %s:%d\n",
                       (int)myPid,sndCount + 1, clientIpStr, clientPort);
                int sent = sendto(slaveSocket, messageToClient, messageSize, MSG_NOSIGNAL,
                                  (struct sockaddr *)(&clientInAddr), sizeof(clientInAddr));
                if (sent == -1)
                {
                    if (errno = EPIPE)
                    {
                        printf("%d: outgoing connection closed: %s:%d\n",
                               (int)myPid, clientIpStr, clientPort);
                        break;
                    }
                    else
                    {
                        fprintf(stderr, "%d: sendto() : %s\n", (int)myPid, strerror(errno));
                        exit(EXIT_FAILURE);
                    }
                }

                if (needToFinish)
                {
                    printf("%d: stop working\n", (int)myPid);
                    break;
                }

                sleep(1);
            }

            close(slaveSocket);
            exit(EXIT_SUCCESS);
        }
        else
        { // this is the main process
            close(slaveSocket);
            ++childrenStarted;
        }
    }

    // this is the main process
    assert(mainPid == myPid);

    int seconds = 0;
    while (childrenFinished < childrenStarted
        && seconds < 60)
    {
        pid_t pid = -1;
        int stat = 0;

        while((pid = waitpid(-1, &stat, WNOHANG)) > 0)
        {
            printf("child %d terminated\n", (int)pid);
            ++childrenFinished;
        }

        if (childrenFinished < childrenStarted)
            sleep(1);
    }
    
    close(masterSocket);
    exit(EXIT_SUCCESS);
}
