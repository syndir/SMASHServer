/**
 * @file client_main.c
 * @author Daniel Calabria
 *
 * Main driver for client program.
 **/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

#include "common.h"
#include "debug.h"
#include "client.h"
#include "proto.h"
#include "parse.h"
#include "io.h"

#define USERNAME_PROMPT     "username: "
#define CLIENT_PROMPT       "client> "

extern char **environ;

int running = 1;
volatile sig_atomic_t debug_enabled = 0;
char *socket_file = NULL;
char *cmdline = NULL;

/**
 * void print_help()
 *
 * @brief  Prints the client help message.
 **/
void print_help()
{
    printf( "Commands:\n"
"    submit [max_cpu] [max_mem] [pri] [cmd] : Submit a new job to the server,\n"
"                                             with the specified resource \n"
"                                             limitations given by max_cpu and\n"
"                                             max_mem, running at priority pri\n"
"                                             by max_cpu and max_mem\n"
"    list                                   : List all jobs for client\n"
"    stdout [jobid]                         : Get the standard output results of\n"
"                                             the specified completed job\n"
"    stderr [jobid]                         : Get the standard error results of\n"
"                                             the specified completed job\n"
"    status [jobid]                         : Get the status of the job with the\n"
"                                             specified id\n"
"    kill [jobid]                           : Terminates the job with the\n"
"                                             specified id\n"
"    stop [jobid]                           : Stops the job with the specified id\n"
"    resume [jobid]                         : Resumes a stopped job with the\n"
"                                             specified id\n"
"    pri [jobid] [priority]                 : Adjust the priority level of a job\n"
"    expunge [jobid]                        : Removes the specified job from the\n"
"                                             client\'s list of jobs\n"
"    help                                   : Displays this list of commands\n"
"    quit                                   : Disconnect and close the client\n"
    );
}

/**
 * int client_handle_input(client_t *c)
 *
 * @brief  Handles user input for the client.
 *
 * @return  0 on success, -1 on error.
 **/
int client_handle_input(client_t *c)
{
    debug("client_handle_input() - ENTER");
    int retval = 0;
    int res = 0;
    char *buf = NULL;

    VALIDATE(c, "client must be non NULL", -1, client_handle_input_end);

    if(cmdline)
    {
        running = 0;
        buf = cmdline;
        goto skip_client_input;
    }

    buf = io_readline();

skip_client_input:
    if(!buf)
    {
        debug("io_readline() failed");
        retval = -1;
        goto client_handle_input_end;
    }

    debug("read input from stdin: \'%s\'", buf);
    if(strlen(buf) == 0)
        goto client_handle_input_end;

    char *cmd = NULL, *saveptr = NULL;
    cmd = strtok_r(buf, " ", &saveptr);
    if(cmd == NULL)
        cmd = buf;

    if(strncmp(cmd, "help", strlen(cmd)) == 0)
    {
        print_help();
    }
    else if(strncmp(cmd, "quit", strlen(cmd)) == 0)
    {
        running = 0;
    }
    else if(strncmp(cmd, "submit", strlen(cmd)) == 0)
    {
        if((res = client_submit_job(c, saveptr)) < 0)
        {
            if(res == -EINVAL)
            {
                goto client_handle_input_end;
            }
            else
            {
                debug("failed to submit job");
                retval = -1;
                goto client_handle_input_end;
            }
        }
    }
    else if(strncmp(cmd, "list", strlen(cmd)) == 0)
    {
        client_list_jobs(c);
    }
    else if(strncmp(cmd, "status", strlen(cmd)) == 0)
    {
        if((res = client_get_status(c, saveptr)) < 0)
        {
            goto client_handle_input_end;
        }

    }
    else if(strncmp(cmd, "kill", strlen(cmd)) == 0)
    {
        char *endp = NULL;
        int jobid = strtol(saveptr, &endp, 10);
        if(*endp != '\0' || saveptr[0] == *endp)
        { res = -EINVAL; goto client_handle_input_end; }

        if((res = client_kill(c, jobid, SIGKILL)) < 0)
        {
            goto client_handle_input_end;
        }
    }
    else if(strncmp(cmd, "stop", strlen(cmd)) == 0)
    {
        char *endp = NULL;
        int jobid = strtol(saveptr, &endp, 10);
        if(*endp != '\0' || saveptr[0] == *endp)
        { res = -EINVAL; goto client_handle_input_end; }

        if((res = client_kill(c, jobid, SIGSTOP)) < 0)
        {
            goto client_handle_input_end;
        }
    }
    else if(strncmp(cmd, "resume", strlen(cmd)) == 0)
    {
        char *endp = NULL;
        int jobid = strtol(saveptr, &endp, 10);
        if(*endp != '\0' || saveptr[0] == *endp)
        { res = -EINVAL; goto client_handle_input_end; }

        if((res = client_kill(c, jobid, SIGCONT)) < 0)
        {
            goto client_handle_input_end;
        }
    }
    else if(strncmp(cmd, "expunge", strlen(cmd)) == 0)
    {
        char *endp = NULL;
        int jobid = strtol(saveptr, &endp, 10);
        if(*endp != '\0' || saveptr[0] == *endp)
        { res = -EINVAL; goto client_handle_input_end; }

        if((res = client_expunge(c, jobid)) < 0)
        {
            goto client_handle_input_end;
        }
    }
    else if(strncmp(cmd, "pri", strlen(cmd)) == 0)
    {
        char *tok = NULL, *endp = NULL;

        tok = strtok_r(NULL, " ", &saveptr);
        if(!tok) { res = -EINVAL; goto client_handle_input_end; }
        int jobid = strtol(tok, &endp, 10);
        if(*endp != '\0' || saveptr[0] == *endp)
        { res = -EINVAL; goto client_handle_input_end; }

        tok = saveptr;
        int pri = strtol(tok, &endp, 10);
        if(*endp != '\0' || saveptr[0] == *endp)
        { res = -EINVAL; goto client_handle_input_end; }

        if((res = client_change_priority(c, jobid, pri)) < 0)
        {
            goto client_handle_input_end;
        }
    }
    else if(strncmp(cmd, "stdout", strlen(cmd)) == 0)
    {
        char *endp = NULL;
        int jobid = strtol(saveptr, &endp, 10);
        if(*endp != '\0' || saveptr[0] == *endp)
        { res = -EINVAL; goto client_handle_input_end; }

        if((res = client_stdout(c, jobid)) < 0)
        {
            goto client_handle_input_end;
        }
    }
    else if(strncmp(cmd, "stderr", strlen(cmd)) == 0)
    {
        char *endp = NULL;
        int jobid = strtol(saveptr, &endp, 10);
        if(*endp != '\0' || saveptr[0] == *endp)
        { res = -EINVAL || saveptr[0] == *endp; }

        if((res = client_stderr(c, jobid)) < 0)
        {
            goto client_handle_input_end;
        }
    }
    else
    {
        debug("invalid command");
    }

client_handle_input_end:
    FREE(buf);

    debug("client_handle_input() - EXIT");
    return retval;
}

/**
 * int client_handle_server(client_t *client)
 *
 * @brief  Handles messages from the server.
 *
 * @param client  The client receiving the message.
 * @return  0 on success, -errno on error.
 **/
int client_handle_server(client_t *client)
{
    debug("client_handle_server - ENTER");

    int retval = 0;
    VALIDATE(client, "client must be non NULL", -EINVAL, client_handle_server_end);

    int r = 0;
    void *payload = NULL;

    if((r = recv_pkt(client->clientfd, &payload)) < 0)
    {
        debug("\rerror dealing with fd %d. disconnecting it", client->clientfd);
        retval = -1;
        goto client_handle_server_end;
    }

    switch(r)
    {
        case JOB_UPDATE:
        {
            update_t *u = (update_t *)payload;
            debug("id=%d status=%d", u->jobid, u->status);
            printf("\r[%d] Changed state and is now \'%s\'\n",
                    u->jobid, jobs_status_as_char(u->status));
            break;
        }

        default:
        {
            debug("server sent unknown packet for unknown reason");
            break;
        }
    }

    FREE(payload);

client_handle_server_end:
    debug("client_handle_server - EXIT");
    return retval;
}

/**
 * void usage(char *pname)
 *
 * @brief  Prints client usage string.
 *
 * @param pname  The name of the program
 **/
void usage(char *pname)
{
    printf("Usage: %s [-f socket_file] [-d] [-u username] [-c command] [-h]\n"
           "    -f socket_file : Name of file to use for socket communications\n"
           "    -d             : Enable debugging output\n"
           "    -u username    : Specify the username to log in as\n"
           "    -c \"command\"   : If specified, client will only execute the specified\n"
           "                     command before exiting, This must be combined with\n"
           "                     the -u option\n"
           "    -h             : Display this help message\n",
           pname);
    exit(EXIT_FAILURE);
}

/**
 * int main(int argc, char *argv[])
 *
 * @brief  Main method for client.
 **/
int main(int argc, char *argv[])
{
    client_t *client = NULL;
    char *name = NULL;
    socket_file = strdup(SOCKET_NAME);

    /* get command line options */
    int opt;
    while((opt = getopt(argc, argv, "f:du:hc:")) != -1)
    {
        switch(opt)
        {
            case 'f':
            {
                FREE(socket_file);
                socket_file = strdup(optarg);
                break;
            }

            case 'd':
            {
                debug_enabled = 1;
                break;
            }

            case 'u':
            {
                FREE(name);
                name = strdup(optarg);
                break;
            }

            case 'h':
            {
                usage(argv[0]);
                break;
            }

            case 'c':
            {
                FREE(cmdline);
                cmdline = strdup(optarg);
                break;
            }

            default:
                usage(argv[0]);
                break;
        }
    }

    if(cmdline && !name)
        usage(argv[0]);

    /* connect to server */
    int sockfd;
    struct sockaddr_un s_addr;
    memset(&s_addr, 0, sizeof(struct sockaddr_un));

    if((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
        PERROR_EXIT("socket()");

    s_addr.sun_family = AF_UNIX;
    strncpy(s_addr.sun_path, socket_file, sizeof(s_addr.sun_path) - 1);

    if(connect(sockfd, (struct sockaddr *) &s_addr, sizeof(struct sockaddr_un)) < 0)
    {
        close(sockfd);
        PERROR_EXIT("connect()");
    }

    debug("client connected to socket @ %s", socket_file);

    /* get username of client */
    if(name)
        goto client_login;

client_get_username:
    io_print_prompt(USERNAME_PROMPT);
    name = io_readline();

    if(!name || strlen(name) < 1)
    {
        printf("Please enter a valid username.\n");
        FREE(name);
        goto client_get_username;
    }

client_login:
    /* login to server */
    MALLOC(client, sizeof(client_t));
    client->name = name;
    client->clientfd = sockfd;
    client_login(client);

    if(cmdline)
    {
        running = 0;
        client_handle_input(client);
        goto end;
    }

    /* main loop */
    while(running)
    {
        io_print_prompt(CLIENT_PROMPT);

        /* set up fd set to include stdin and the socket */
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        FD_SET(sockfd, &fds);

        /* wait until something is available on either of them */
        int n = select(sockfd+1, &fds, NULL, NULL, NULL);
        if(n < 0)
        {
            if(errno == EINTR)
                continue;
            PERROR_EXIT("select()");
        }
        else if(n == 0)
        {
        }
        else
        {
            if(FD_ISSET(STDIN_FILENO, &fds))
            {
                if(client_handle_input(client) < 0)
                {
                    debug("client_handle_input() failed.");
                    goto end;
                }
            }
            else if(FD_ISSET(sockfd, &fds))
            {
                if(client_handle_server(client) < 0)
                {
                    debug("client_handle_server() failed.");
                    goto end;
                }
            }
        }

    }

end:
    close(sockfd);
    client_cleanup(client);
    FREE(socket_file);
    return EXIT_SUCCESS;
}
