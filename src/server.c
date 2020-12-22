/**
 * @file server.c
 * @author Daniel Calabria
 *
 * Handles functionality for the server.
 **/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/un.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/mman.h>

#include "common.h"
#include "debug.h"
#include "server.h"
#include "proto.h"
#include "conn.h"

server_t *server;

volatile sig_atomic_t got_ctrl_c = 0;
volatile sig_atomic_t need_to_reap = 0;

/**
 * void server_handler(int)
 *
 * Shut down the server when receiving SIGINT.
 *
 * @param sig  The signal number.
 **/
void server_handler(int sig)
{
    if(sig == SIGINT || sig == SIGTERM)
        got_ctrl_c = 1;
    if(sig == SIGCHLD)
        need_to_reap = 1;
    if(sig == SIGUSR1)
        debug_enabled = !debug_enabled;
}

/**
 * void handle_all_signals()
 *
 * @brief  Handles all notifications of received signals in a signal-safe
 * manner.
 **/
void handle_all_signals()
{
    debug("handle_all_signals() - ENTER");

    /* did any children exit for any reason? */
    if(need_to_reap)
    {
        pid_t pid;
        int status;
        struct rusage ru;
        while((pid = wait4(-1, &status, WNOHANG | WUNTRACED | WCONTINUED, &ru)) > 0)
        {
            debug("pid %d REAPED", pid);

            job_t *j = NULL;
            if((j = jobs_lookup_by_pid(server->joblist, pid)) == NULL)
            {
                debug("failed to locate job for pid=%d", pid);
                continue;
            }

            memcpy(&j->ru, &ru, sizeof(struct rusage));

            job_update_status(j, status);
            debug("pid %d changed to \'%s\'", j->pgid, jobs_status_as_char(j->status));

            debug("status=%d", j->status);
            switch(j->status)
            {
                case RUNNING:
                    server->numjobs++;
                    break;

                case SUSPENDED:
                case EXITED:
                case ABORTED:
                {
                    /* if a job just stopped, see if we can start another one */
                    server->numjobs--;

                    job_t *j = server->joblist;
                    while(j && server->numjobs < server->maxjobs)
                    {
                        /* only try to start NEW jobs */
                        if(j->status == NEW)
                        {
                            debug("starting new job");
                            if(exec_job(j->owner, j) < 0)
                            {
                                debug("exec_job() failed");
                            }
                        }

                        j = j->snext;
                    }

                    break;
                }

                default:
                    break;
            }

            conn_t *conn = conn_find_by_client(j->owner);
            if(!conn)
                continue;

            /* send an update packet to the client */
            update_t *u = NULL;
            MALLOC(u, sizeof(update_t));
            u->jobid = j->jobid;
            u->status = j->status;
            if(conn->client && conn->client->connected)
                send_pkt(conn->fd, JOB_UPDATE, u);
            FREE(u);
        }

        need_to_reap = 0;
    }

    /* exit server? */
    if(got_ctrl_c)
    {
        printf("Shutting down...\n");
        got_ctrl_c = 0;
        server_shutdown(EXIT_SUCCESS);
    }

    debug("handle_all_signals() - EXIT");
}

/**
 * void server_shutdown(int)
 *
 * Shuts down the server.
 *
 * @param exitcode  The exit code the server should return.
 **/
void server_shutdown(int exitcode)
{
    /* disconnect all connections */
    conn_t *c = server->connlist;
    while(c)
    {
        conn_t *cn = c->next;
        server_disconnect_client(c);
        c = cn;
    }

    /* delete all clients and their corresponding jobs */
    client_t *cl = server->clientlist;
    while(cl)
    {
        client_t *cln = cl->next;
        cancel_all_jobs(cl);
        wait_for_all(cl);
        free_jobs(cl);
        FREE(cl->name);
        FREE(cl);
        cl = cln;
    }

    /* delete the socket */
    if(unlink(server->socket_file) < 0)
        perror("unlink()");

    /* free server resources */
    FREE(server->socket_file);
    FREE(server);

    exit(exitcode);
}

/**
 * int server_init()
 *
 * @brief  Initializes the server data structure.
 *
 * @return  0 on success, -errno on error
 **/
int server_init()
{
    MALLOC(server, sizeof(server_t));
    memset(server, 0, sizeof(server_t));
    server->maxjobs = INT_MAX;
    server->socket_file = strdup(SOCKET_NAME);

    return 0;
}

/**
 * int server_handle_client(client_t *)
 *
 *
 * @param client  Contains the fd of the client
 * @return  0 on success, -errno on error
 **/
int server_handle_client(conn_t *conn)
{
    debug("server_handle_client() - ENTER");
    int retval = 0;
    int r;
    void *payload = NULL;


    VALIDATE(conn, "conn must not be NULL", -EINVAL, server_handle_client_end);

    if((r = recv_pkt(conn->fd, &payload)) < 0)
    {
        debug("error dealing with client %d. disconnecting it", conn->fd);
        server_disconnect_client(conn);
        FREE(payload);
        goto server_handle_client_end;
    }

    /* what did they want to do? */
    switch(r)
    {
        /* client login */
        case LOGIN:
        {
            char *name = (char *)payload;
            debug("server received login packet for %s", name);
            conn->client = server_login_client(name);
            FREE(name);
            if(conn->client && conn->client->connected)
                send_pkt(conn->fd, (conn->client ? ACK : NACK), NULL);
            break;
        }

        /* job submission */
        case JOB_SUBMIT:
        {
            VALIDATE(conn->client, "client must be non NULL", -EINVAL,
                    server_handle_client_end);
            submission_t *s = (submission_t *)payload;
            debug("server received JOB_SUBMIT for user=%s", conn->client->name);

            user_input_t *ui = NULL;
            ui = parse_input(s->cmdline);
            if(!ui)
            {
                debug("parse_input() failed");
                for(int i = 0; i < s->envpc; i++)
                    FREE(s->envp[i]);
                FREE(s->cmdline);
                FREE(s);
                if(conn->client && conn->client->connected)
                    send_pkt(conn->fd, NACK, NULL);
                retval = -1;
                goto server_handle_client_end;
            }

            job_t *j = NULL;
            j = jobs_create(ui);
            VALIDATE(j, "failed to create job", -ENOMEM, server_handle_client_end);
            j->maxmem = s->maxmem;
            j->maxcpu = s->maxcpu;
            j->priority = s->priority;
            j->envpc = s->envpc;
            j->envp = s->envp;

            if(jobs_insert(conn->client, j) < 0)
            {
                error("failed to insert job into joblist");
                exit(EXIT_FAILURE);
            }

            debug("jobid is %d", j->jobid);
            for(int i = 0; i < s->envpc; i++)
                FREE(s->envp[i]);
            FREE(s->cmdline);
            FREE(s);
            if(conn->client && conn->client->connected)
                send_pkt(conn->fd, JOB_SUBMIT_SUCCESS, &j->jobid);
            printf("client \'%s\' submitted a new job.\n", conn->client->name);

            if(exec_job(conn->client, j) < 0)
            {
                if(conn->client && conn->client->connected)
                    send_pkt(conn->fd, NACK, NULL);
                debug("exec_job() failed");
                retval = -1;
                goto server_handle_client_end;
            }

            break;
        }

        /* job status of a particular job */
        case JOB_STATUS:
        {
            VALIDATE(conn->client, "client must be non NULL", -EINVAL,
                    server_handle_client_end);
            uint32_t *jobid = (uint32_t *)payload;
            debug("server received JOB_STATUS for user=%s jobid=%d",
                    conn->client->name, *jobid);
            job_t *j = jobs_lookup_by_jobid(conn->client, *jobid);
            FREE(jobid);

            if(!j)
            {
                if(conn->client && conn->client->connected)
                    send_pkt(conn->fd, NACK, NULL);
                break;
            }

            status_t *s = NULL;
            MALLOC(s, sizeof(status_t));
            s->status = j->status;
            s->exitcode = j->exitcode;
            s->maxmem = j->maxmem;
            s->maxcpu = j->maxcpu;
            s->priority = getpriority(PRIO_PGRP, j->pgid);
            memcpy(&s->ru, &j->ru, sizeof(struct rusage));
            if(conn->client && conn->client->connected)
                send_pkt(conn->fd, JOB_STATUS_RESP, s);
            FREE(s);

            break;
        }

        /* job listing request */
        case JOB_LIST_ALL:
        {
            VALIDATE(conn->client, "client must be non NULL", -EINVAL,
                    server_handle_client_end);
            debug("server received JOB_LIST_ALL for user=%s", conn->client->name);

            int jobcount = 0;
            job_t *j = conn->client->jobs;
            while(j)
            {
                jobcount++;
                j = j->next;
            }

            if(jobcount <= 0)
            {
                if(conn->client && conn->client->connected)
                    send_pkt(conn->fd, NACK, NULL);
                break;
            }

            debug("client has %d jobs", jobcount);
            listing_t *mainl = NULL;
            MALLOC(mainl, sizeof(listing_t));
            j = conn->client->jobs;

            listing_t *l = mainl, *ln = NULL;
            for(int i = 0; i < jobcount; i++)
            {
                l->left = jobcount - i - 1;
                l->jobid = j->jobid;
                l->cmdline = strdup(j->ui->input);
                l->cmdlen = strlen(l->cmdline)+1;
                l->status = j->status;
                l->exitcode = j->exitcode;

                if(l->left > 0)
                {
                    MALLOC(ln, sizeof(listing_t));
                    l->next = ln;
                    l = l->next;
                }
                j = j->next;
            }

            if(conn->client && conn->client->connected)
                send_pkt(conn->fd, JOB_LIST_ALL_RESP, mainl);

            l = mainl;
            while(l)
            {
                ln = l->next;
                FREE(l->cmdline);
                FREE(l);
                l = ln;
            }

            break;
        }

        /* priority change request */
        case JOB_SET_PRI:
        {
            VALIDATE(conn->client, "client must be non NULL", -EINVAL,
                    server_handle_client_end);
            priority_t *p = (priority_t *)payload;
            debug("server received JOB_SET_PRI for user=%s", conn->client->name);

            job_t *j = jobs_lookup_by_jobid(conn->client, p->jobid);
            if(!j)
            {
                if(conn->client && conn->client->connected)
                    send_pkt(conn->fd, NACK, NULL);
                FREE(p);
                break;
            }

            debug("j->pgid for setpri is %d", j->pgid);
            int res = setpriority(PRIO_PGRP, j->pgid, p->priority);
            if(res == 0)
            {
                if(conn->client && conn->client->connected)
                    send_pkt(conn->fd, ACK, NULL);
            }
            else
            {
                perror("setpriority()");
                if(conn->client && conn->client->connected)
                    send_pkt(conn->fd, NACK, NULL);
            }
            FREE(p);
            break;
        }

        /* signal request (stop/start/kill) */
        case JOB_SIGNAL:
        {
            VALIDATE(conn->client, "client must be non NULL", -EINVAL,
                    server_handle_client_end);
            signal_t *s = (signal_t *)payload;
            debug("server received JOB_SIGNAL for user=%s jobid=%d signal=%d",
                    conn->client->name, s->jobid, s->signal);

            job_t *j = jobs_lookup_by_jobid(conn->client, s->jobid);
            if(!j)
            {
                if(conn->client && conn->client->connected)
                    send_pkt(conn->fd, NACK, NULL);
            }
            else
            {
                if(conn->client && conn->client->connected)
                    send_pkt(conn->fd, ACK, NULL);
                killpg(j->pgid, s->signal);
            }

            FREE(s);
            break;
        }

        /* expunge request */
        case JOB_EXPUNGE:
        {
            VALIDATE(conn->client, "client must be non NULL", -EINVAL,
                    server_handle_client_end);
            uint32_t *jobid = (uint32_t *)payload;
            debug("server got JOB_EXPUNGE for user=%s jobid=%d",
                    conn->client->name, *jobid);

            job_t *j = jobs_lookup_by_jobid(conn->client, *jobid);
            if(!j)
            {
                if(conn->client && conn->client->connected)
                    send_pkt(conn->fd, NACK, NULL);
            }
            else
            {
                if(conn->client && conn->client->connected)
                    send_pkt(conn->fd, ACK, NULL);
                /* make sure its not running */
                if(j->status == RUNNING || j->status == SUSPENDED)
                    killpg(j->pgid, SIGKILL);

                jobs_remove(conn->client, j);
            }

            FREE(jobid);
            break;
        }

        case JOB_GET_STDOUT:
        case JOB_GET_STDERR:
        {
            VALIDATE(conn->client, "client must be non NULL", -EINVAL,
                    server_handle_client_end);
            uint32_t *jobid = (uint32_t *)payload;
            debug("server got RESULTS REQUEST (%d) for user=%s jobid=%d",
                    r, conn->client->name, *jobid);

            job_t *j = jobs_lookup_by_jobid(conn->client, *jobid);
            FREE(jobid);

            if(!j)
            {
                if(conn->client && conn->client->connected)
                    send_pkt(conn->fd, NACK, NULL);
                goto server_handle_client_end;
            }

            /* if the job's not done, don't return any results.
             * it's like baking. don't take the cake out of the oven before
             * the timer goes off... */
            if(j->status != ABORTED && j->status != EXITED)
            {
                if(conn->client && conn->client->connected)
                    send_pkt(conn->fd, NACK, NULL);
                goto server_handle_client_end;
            }


            char *c = (r == JOB_GET_STDOUT ? j->stdoutfile : j->stderrfile);
            struct stat s;
            if(stat(c, &s) < 0)
            {
                debug("stat failed for results file for \'%s\'", c);
                if(conn->client && conn->client->connected)
                    send_pkt(conn->fd, NACK, NULL);
                goto server_handle_client_end;
            }

            int fd = open(c, O_RDONLY);
            if(fd < 0)
            {
                perror("open()");
                if(conn->client && conn->client->connected)
                    send_pkt(conn->fd, NACK, NULL);
                goto server_handle_client_end;
            }

            results_t *results = NULL;
            MALLOC(results, sizeof(results_t));
            results->length = s.st_size;

            /* if the file is empty, then by definition there are no results */
            if(results->length == 0)
            {
                debug("results file is empty");
                if(conn->client && conn->client->connected)
                    send_pkt(conn->fd, NACK, NULL);
                goto server_handle_client_end;
            }

            results->results = mmap(0, results->length, PROT_READ, MAP_PRIVATE, fd, 0);
            if(results->results == MAP_FAILED)
                PERROR_EXIT("mmap()");
            if(conn->client && conn->client->connected)
                send_pkt(conn->fd, JOB_RESULTS, results);
            munmap(results->results, results->length);
            FREE(results);

            break;
        }

        default:
            debug("OTHER: %d", r);
            break;
    }


server_handle_client_end:
    debug("server_handle_client() - EXIT");
    return retval;
}

/**
 * client_t* server_login_client(char *name)
 *
 * @brief  Performs client login.
 *
 * #param name  The username of the client
 *
 * @return  The client this user logged in as, or NULL on error.
 **/
client_t* server_login_client(char *name)
{
    client_t *retval = NULL;

    VALIDATE(name, "name must be non NULL", NULL, server_login_client_end);
    VALIDATE(strlen(name) > 0, "name must be len>0", NULL, server_login_client_end);

    client_t *cl = server->clientlist;

    /* does user already exist? */
    while(cl)
    {
        if(strlen(name) == strlen(cl->name) &&
           strncmp(cl->name, name, strlen(cl->name)) == 0)
        {
            if(cl->connected == 1)
            {
                debug("%s trying to log in but already connected", name);
                retval = NULL;
                goto server_login_client_end;
            }

            debug("found old record for client %s", cl->name);
            cl->connected = 1;
            retval = cl;
            goto server_login_client_end;
        }

        cl = cl->next;
    }

    /* create new client_t for user */
    debug("no client record for \'%s\' exists. creating new record", name);
    MALLOC(retval, sizeof(client_t));
    retval->connected = 1;
    retval->name = strdup(name);

    retval->next = server->clientlist;
    server->clientlist = retval;

    printf("Client \'%s\' successfully logged in.\n", retval->name);
server_login_client_end:
    return retval;
}

/**
 * conn_t* server_register_conn(int)
 *
 * @brief  Adds a client to the connection list.
 *
 * @param fd  The fd the client is connected to.
 *
 * @return  A pointer to the newly created conn_t, or NULL.
 **/
conn_t* server_register_conn(int fd)
{
    debug("server_register_conn() - ENTER");
    conn_t *retval = NULL;
    VALIDATE(server,
            "server must be initialized",
            NULL,
            server_register_client_end);

    /* create new conn obj */
    MALLOC(retval, sizeof(conn_t));
    retval->fd = fd;

    retval->next = server->connlist;
    server->connlist = retval;

server_register_client_end:
    debug("server_register_conn() - EXIT");
    return retval;
}

/**
 * int server_disconnect_client(conn_t *)
 *
 * @brief  Disconnects a client from the server, removing it's entry from the
 * connlist, but maintaining its info in the clientlist.
 *
 * @param c  The connection to disconnect
 * @return  0 on success, -1 on error.
 **/
int server_disconnect_client(conn_t *c)
{
    debug("server_disconnect_client() - ENTER");
    int retval = 0;

    VALIDATE(c, "conn must be non NULL", -1, server_disconnect_client_end);
    if(c->fd > 0)
    {
        if(c->client)
            printf("client \'%s\' (fd=%d) disconnected\n", c->client->name, c->fd);
        else
            printf("client @ fd=%d disconnected\n", c->fd);

        close(c->fd);
        c->fd = -1;
        if(c->client)
            c->client->connected = 0;

        /* remove connection from connlist */
        conn_t *cl = server->connlist;
        if(server->connlist == c)
        {
            server->connlist = c->next;
            FREE(c);
            goto server_disconnect_client_end;
        }

        while(cl && cl->next != c)
            cl = cl->next;

        if(cl && cl->next == c)
        {
            cl->next = c->next;
            FREE(c);
        }
    }

server_disconnect_client_end:
    debug("server_disconnect_client() - EXIT");
    return retval;
}

/**
 * int server_remove_client(client_t *)
 *
 * @brief  Removes a client from the server.
 *
 * @param client  The client to remove
 * @return  0 on success, -errno on error.
 **/
int server_remove_client(client_t *client)
{
    debug("server_remove_client() - ENTER");
    int retval = 0;
    VALIDATE(client, "client must be non NULL", -EINVAL, server_remove_client_end);

    debug("removing client \'%s\' (fd=%d)", client->name, client->clientfd);

    /* free resources held by client */
    cancel_all_jobs(client);
    free_jobs(client);
    client->jobs = NULL;

    /* remove the client from the server records */
    if(server->clientlist == client)
    {
        /* first element? */
        server->clientlist = client->next;
        client_cleanup(client);
    }
    else
    {
        client_t *c = server->clientlist;
        while(c && c->next != client)
            c = c->next;

        if(c && c->next == client)
        {
            c->next = client->next;
            client_cleanup(client);
        }
    }

server_remove_client_end:
    debug("server_remove_client() - EXIT");
    return retval;
}

