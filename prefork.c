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

static const char* messageToClient = "Hi there\n";

/**
 *  while (needed)
 *  {
 *       int sock = accept(mastersocket, ...)
 *       recv(sock)...send(sock)
 *       close(sock)
 *  }
 */

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

static pid_t children[100];
static size_t childCount = 0;
void sig_chld(int signo)
{
    pid_t pid = -1;
    int stat = 0;

    while((pid = waitpid(-1, &stat, WNOHANG)) > 0)
    {
        printf("child %d terminated\n", (int)pid);
        int i = 0;
        for (; i < childCount; ++i)
        {
            if (children[i] == pid)
            {
                children[i] = 0;
                break;
            }
        }
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

void unset_sigchld_handler()
{
    set_signal_handler(SIGCHLD, "SIGCHLD", SIG_DFL);
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

int fork_children(int processCount, int* myPid, pid_t* children)
{
    int childCount = 0;

    *myPid = getpid();
    printf("main server process: pid = %d\n", (int)(*myPid));
    if (processCount > 1)
        printf("%d: Starting additional server processes:\n", (int)(*myPid));

    int processIndex = 1;
    for (; processIndex < processCount; ++processIndex)
    {
        pid_t pid = fork();
        if (pid == -1)
        {
            printf("not all server process have been created... continuing...\n");
            return childCount;
        }
        else if (pid == 0)
        { // this is a child process
            *myPid = getpid();
            printf("additional server process: pid = %d\n", (int)(*myPid));
            return 0;
        }
        else
        { // this is the main process. Continue cycling
            children[childCount] = pid;
            ++childCount;
            continue;
        }
    }    
}

void wait_for_remaining_children(pid_t* children, int childCount, int myPid)
{
    unset_sigchld_handler();

    size_t childIndex = 0;
    for (; childIndex < childCount; ++childIndex)
    {
        pid_t cpid = children[childIndex];
        if (cpid == 0)
            continue;

        kill(cpid, SIGINT);

        children[childIndex] = 0;
        int status = 0;
        pid_t waitRes = waitpid(cpid, &status, 0);
        if (waitRes == -1)
        {
            printf("%d: waitpid(%d) : %s\n", (int)myPid, (int)cpid, strerror(errno));
            continue;
        }
        else
        {
            char message[128] = "unknown";
            if (WIFEXITED(status))
                strncpy(message, "normal finish", 128);
            if (WIFSIGNALED(status))
                strncpy(message, "killed by a signal", 128);
            printf("%d: waitpid(%d) : %s\n", (int)myPid, (int)cpid, message);          
        }
    }
}

int main(int argc, char** argv)
{
    if (argc >= 2)
    {
        int cmpRes = strcmp(argv[1], "--help");
        if (cmpRes == 0)
        {
            printf("usage: prefork [serverPort] [processCount]\n");
            exit(EXIT_SUCCESS);
        }    
    }

    uint16_t port = 6666;
    if (argc >= 2)
        port = (uint16_t)atoi(argv[1]);
    printf("server port = %d\n", port);

    int processCount = 2;
    if (argc >= 3)
        processCount = atoi(argv[2]);
    printf("process count = %d\n", processCount);
    
    set_sigchld_handler();
    set_sigint_handler();

    int masterSocket = create_tcp_socket();
    set_reuse_addr_opt(masterSocket);
    bind_server_socket(masterSocket, port);
    listen_tcp_socket(masterSocket);

    pid_t mainPid = getpid();
    pid_t myPid = 0;

    // not zero only for the main process:
    bzero(children, sizeof(children));
    childCount = fork_children(processCount, &myPid, children);

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
                    printf("%d: signal occured... continue accepting...", (int)myPid);
                    continue;
gdb                }
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

        printf("%d: closing connection: %s:%d\n", (int)myPid, clientIpStr, clientPort);
        close(slaveSocket);
    }

    close(masterSocket);
    
    // zombies are comming=)
    if (myPid == mainPid)
        wait_for_remaining_children(children, childCount, myPid);

    exit(EXIT_SUCCESS);
}
