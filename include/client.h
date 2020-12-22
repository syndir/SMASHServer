/**
 * @file client.h
 * @author Daniel Calabria
 *
 * Header file for client functionality.
 **/

#ifndef CLIENT_H
#define CLIENT_H

#include "jobs.h"

/* Represents a client */
typedef struct client_s
{
    int clientfd;   /* the fd the client is on */
    char *name;     /* name of the client */
    int connected;  /* if the client is currently connected */

    job_t *jobs;
    int numjobs;

    struct client_s *next;
} client_t;

/* fxn prototypes */
int client_cleanup(client_t *c);
int client_login(client_t *c);
int client_submit_job(client_t *client, char *str);
int client_get_status(client_t *c, char *str);
int client_list_jobs(client_t *c);
int client_change_priority(client_t *c, int jobid, int priority);
int client_kill(client_t *c, int jobid, int signum);
int client_expunge(client_t *c, int jobid);
int client_stdout(client_t *c, int jobid);
int client_stderr(client_t *c, int jobid);

#endif // CLIENT_H
