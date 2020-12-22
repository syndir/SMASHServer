/**
 * @file server_main.c
 * @author Daniel Calabria
 *
 * Main driver for server program.
 **/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <signal.h>
#include <errno.h>
#include <sys/select.h>
#include <fcntl.h>

#include "common.h"
#include "debug.h"
#include "server.h"
#include "proto.h"

volatile sig_atomic_t debug_enabled = 0;

/**
 * void usage(char *pname)
 *
 * @brief  Prints a usage string, then exits the program. This function never
 * returns.
 *
 * @param pname  The name of the program
 **/
void usage(char *pname)
{
    printf("Usage: %s [-f socket_file] [-d] [-n maxjobs] [-h]\n"
           "    -f socketfile :  Specifies the socket file to use for the server\n"
           "    -d            :  Enables debugging output\n"
           "    -n maxjobs    :  Maximum number of jobs the server can concurrently run\n"
           "    -h            :  Displays this help message\n"
           , pname);
    exit(EXIT_FAILURE);
}

/**
 * int main(int, char *[])
 *
 * main method for server.
 **/
int main(int argc, char *argv[])
{
    int sockfd = 0;
    struct sockaddr_un s_addr;

    /* initialize the server structure */
    if(server_init() < 0)
        PERROR_EXIT("server_init()");

    /* server->socket_file = strdup(SOCKET_NAME); */

    /* command line options */
    int opt;
    while((opt = getopt(argc, argv, "f:dn:h")) != -1)
    {
        switch(opt)
        {
            case 'f':
            {
                FREE(server->socket_file);
                server->socket_file = strdup(optarg);
                break;
            }

            case 'd':
            {
                debug_enabled = 1;
                break;
            }

            case 'n':
            {
                char *endp = NULL;
                server->maxjobs = strtol(optarg, &endp, 10);
                if(*endp != '\0' || server->maxjobs < 1)
                {
                    printf("Invalid max number of jobs.\n");
                    usage(argv[0]);
                }
                break;
            }

            case 'h':
            default:
                usage(argv[0]);
        }
    }

    /* install signal handler */
    struct sigaction sa;
    sa.sa_handler = server_handler;
    if(sigemptyset(&sa.sa_mask) < 0)
        PERROR_EXIT("sigemptyset()");
    sa.sa_flags = SA_RESTART;
    if(sigaction(SIGINT, &sa, NULL) < 0)
        PERROR_EXIT("sigaction()");
    if(sigaction(SIGTERM, &sa, NULL) < 0)
        PERROR_EXIT("siaction()");
    if(sigaction(SIGCHLD, &sa, NULL) < 0)
        PERROR_EXIT("sigaction()");
    if(sigaction(SIGUSR1, &sa, NULL) < 0)
        PERROR_EXIT("sigaction()");
    if(sigaction(SIGPIPE, &sa, NULL) < 0)
        PERROR_EXIT("sigaction()");

    /* do we need to delete an old file that was left over? */
    if(access(server->socket_file, R_OK | W_OK) != -1)
    {
        if(errno != ENOENT)
        {
            printf("File \'%s\' already exists.\n"
                   "If you wish to use this file as a socket file, manually remove it.\n",
                   server->socket_file);
            exit(EXIT_FAILURE);
        }
    }

    /* set up socket */
    if((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
        PERROR_EXIT("socket()");

    if(fcntl(sockfd, F_SETFD, FD_CLOEXEC) == -1)
        PERROR_EXIT("fcntl()");

    s_addr.sun_family = AF_UNIX;
    strncpy(s_addr.sun_path, server->socket_file, sizeof(s_addr.sun_path)-1);
    if(bind(sockfd, (struct sockaddr *)&s_addr, sizeof(struct sockaddr_un)) < 0)
    {
        close(sockfd);
        PERROR_EXIT("bind()");
    }

    if(listen(sockfd, 1024) < 0)
    {
        close(sockfd);
        PERROR_EXIT("listen()");
    }

    printf("Server socket is open and listening on %s\n", server->socket_file);

    int connfd = -1;
    fd_set fds;
    int nfds;
    int n = -1;

    /* main server loop */
    while(1)
    {
        /* block all signals, handle any notifications, unblock all signals */
        sigset_t mask, o_mask;
        sigfillset(&mask);
        sigprocmask(SIG_BLOCK, &mask, &o_mask);
        handle_all_signals();

        /* set up the list of fd's to examine */
        FD_ZERO(&fds);
        FD_SET(sockfd, &fds);
        nfds = sockfd;

        conn_t *conn = server->connlist;
        while(conn)
        {
            if(conn->fd < 0)
                continue;

            FD_SET(conn->fd, &fds);
            if(conn->fd > nfds)
                nfds = conn->fd;
            conn = conn->next;
        }

        /* who's got stuff for us to read? */
        n = pselect(nfds+1, &fds, NULL, NULL, NULL, &o_mask);
        sigprocmask(SIG_SETMASK, &o_mask, NULL);

        if(n < 0)
        {
            /* interrupted? start over */
            if(errno == EINTR)
                continue;

            PERROR_EXIT("select()");
        }

        /* listening socket has pending connections? */
        if(FD_ISSET(sockfd, &fds))
        {
server_main_accept:
            if((connfd = accept(sockfd, NULL, NULL)) < 0)
            {
                if(errno == EINTR)
                    goto server_main_accept;
                PERROR_EXIT("accept()");
            }

            if(fcntl(connfd, F_SETFD, FD_CLOEXEC) == -1)
                PERROR_EXIT("fcntl()");

            printf("New connection on fd=%d\n", connfd);

            server_register_conn(connfd);
        }

        /* do any of the clients need to be serviced? */
        conn_t *c = server->connlist;
        while(c)
        {
            conn_t *cn = c->next; /* in case we end up being dico'd/freed */
            if(c->fd > 0 && FD_ISSET(c->fd, &fds))
            {
                debug("client has data on %d", c->fd);
                sigprocmask(SIG_BLOCK, &mask, &o_mask);
                server_handle_client(c);
                sigprocmask(SIG_SETMASK, &o_mask, NULL);
            }

            c = cn;
        }
    }

    /* really we should never get here */
    close(sockfd);
    server_shutdown(EXIT_SUCCESS);
    return EXIT_SUCCESS;
}
