/**
 * @file client.c
 * @author Daniel Calabria
 *
 * Handles client functionality.
 **/

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
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
#include "jobs.h"

#define USERNAME_PROMPT     "username: "
#define CLIENT_PROMPT       "client> "

extern char **environ;

/**
 * int client_cleanup(client_t *c)
 *
 * @brief  Performs resource cleanup for a client.
 *
 * @param c  The client to release the resources of.
 * @return  0 on success, -errno on error.
 **/
int client_cleanup(client_t *c)
{
    debug("client_cleanup() - ENTER");
    int retval = 0;
    VALIDATE(c, "client must be non NULL", -EINVAL, client_cleanup_end);

    FREE(c->name);
    FREE(c);

client_cleanup_end:
    debug("client_cleanup() - EXIT");
    return retval;
}

/**
 * int client_login(client_t *c)
 *
 * @brief  Performs a login sequence for a client.
 * @param c  The client to perform the login sequence for.
 *
 * @return  0 on success, -errno on error.
 **/
int client_login(client_t *c)
{
    int retval = 0;
    VALIDATE(c, "client must be non NULL", -EINVAL, client_login_end);

    if(send_pkt(c->clientfd, LOGIN, c->name) < 0)
    {
        PERROR_EXIT("send_pkt()");
    }

    if(recv_pkt(c->clientfd, NULL) != ACK)
    {
        printf("Error logging in. Exiting.\n");
        PERROR_EXIT("recv_pkt()");
    }

client_login_end:
    return retval;
}

/**
 * int client_submit_job(client_t *client, char *str)
 *
 * @brief  Parses str and sends the information for submitting a new job to the
 * server.
 *
 * @param client  The client sending the job request
 * @param str  The string containing all information about the job.
 *
 * @return  0 on success, -errno on error.
 **/
int client_submit_job(client_t *client, char *str)
{
    int retval = 0;
    int res = 0;

    VALIDATE(client, "client must be non NULL", -EINVAL, client_submit_job_end);
    VALIDATE(str, "command string must be non NULL", -EINVAL, client_submit_job_end);

    submission_t *job = NULL;
    MALLOC(job, sizeof(submission_t));

    /* the string should be of the format:
     *    <maxcpu> <maxmem> <priority> <commandline>
     */
    char *tok = NULL, *saveptr = NULL;

    /* extract maxcpu */
    tok = strtok_r(str, " ", &saveptr);
    if(!tok) { FREE(job); return -EINVAL; }
    job->maxcpu = strtol(tok, NULL, 10);
    debug("maxcpu: %d", job->maxcpu);

    /* extract maxmem */
    tok = strtok_r(NULL, " ", &saveptr);
    if(!tok) { FREE(job); return -EINVAL; }
    job->maxmem = strtol(tok, NULL, 10);
    debug("maxmem: %d", job->maxmem);

    /* extract priority */
    tok = strtok_r(NULL, " ", &saveptr);
    if(!tok) { FREE(job); return -EINVAL; }
    job->priority = strtol(tok, NULL, 10);
    debug("priority: %d", job->priority);

    /* rest is commandline */
    job->cmdline = saveptr;
    job->cmdlen = strlen(job->cmdline);
    debug("command line len: %d, line: \'%s\'", job->cmdlen, job->cmdline);

    /* store envp + count of vars */
    job->envp = environ;
    while(job->envp[job->envpc])
        job->envpc++;

    debug("got %d environ vars", job->envpc);

    if(send_pkt(client->clientfd, JOB_SUBMIT, job) < 0)
    {
        FREE(job);
        PERROR_EXIT("send_pkt()");
    }
    FREE(job);

    int *jobid = NULL;
    /* MALLOC(jobid, sizeof(int)); */
    res = recv_pkt(client->clientfd, (void *)&jobid);
    if(res == JOB_SUBMIT_SUCCESS)
    {
        printf("[%d] Job submitted.\n", *jobid);
    }
    else if(res == NACK)
    {
        printf("Job submission failed!\n");
    }
    else
        printf("???\n");

    FREE(jobid);
client_submit_job_end:
    return retval;
}

/**
 * int client_get_status(client_t *c, char *str)
 *
 * @brief  Retrieves the status of a particular job.
 *
 * @param c  The client
 * @param str  The string containing the jobid
 *
 * @return  0 on success, -errno on error.
 **/
int client_get_status(client_t *c, char *str)
{
    debug("client_get_status() - ENTER");
    int retval = 0;

    VALIDATE(c, "client must be non NULL", -EINVAL, client_get_status_end);
    VALIDATE(str, "str must be non NULL", -EINVAL, client_get_status_end);

    uint32_t jobid = 0;
    char *endp = NULL;

    jobid = strtol(str, &endp, 10);
    if(*endp != '\0' || str[0] == *endp)
    {
        retval = -EINVAL;
        goto client_get_status_end;
    }

    if(send_pkt(c->clientfd, JOB_STATUS, &jobid) < 0)
        PERROR_EXIT("send_pkt()");


    status_t *s = NULL;
    MALLOC(s, sizeof(status_t));
    int res = recv_pkt(c->clientfd, (void *)&s);
    if(res == JOB_STATUS_RESP)
    {
        struct timeval result_tv;
        timeradd(&s->ru.ru_stime, &s->ru.ru_stime, &result_tv);

        printf("(%s)", jobs_status_as_char(s->status));

        /* completed */
        if(s->status == EXITED)
        {
            printf(" <exitcode=%d>", s->exitcode);
        }
        else if(s->status == ABORTED)
        {
            printf(" <signal=%d>", s->exitcode);
        }

        /* have ran at least some time */
        if(s->status == EXITED || s->status == ABORTED || s->status == SUSPENDED)
        {
            printf(" <cputime=%ld.%ld> <maxrss=%ld>",
                   result_tv.tv_sec,
                   result_tv.tv_usec,
                   s->ru.ru_maxrss);
        }
        printf(" <priority=%d> (limits: [cpu=%d] [mem=%d])",
                    s->priority, s->maxcpu, s->maxmem);

        /* did the process go over resource limits? */
        struct timeval maxtv;
        maxtv.tv_sec = s->maxcpu;
        maxtv.tv_usec = 0;


        if(result_tv.tv_sec > maxtv.tv_sec)
            printf(" [EXCEEDED USER CPU LIMIT]");
        else if(result_tv.tv_sec == maxtv.tv_sec)
        {
            if(result_tv.tv_usec >= maxtv.tv_usec)
                printf(" [EXCEEDED USER CPU LIMIT]");
        }

        if(s->ru.ru_maxrss >= s->maxmem)
            printf(" [EXCEEDED USER MEM LIMIT]");

        printf("\n");
    }
    else
    {
        printf("No such job found.\n");
    }

    FREE(s);
client_get_status_end:

    debug("client_get_status() - EXIT");
    return retval;
}

/**
 * int client_list_jobs(client_t *c)
 *
 * @brief  Retrieves all jobs belonging to the client and displays them.
 *
 * @param c  The client to list the jobs of
 * @return  0 on success, -errno on error
 **/
int client_list_jobs(client_t *c)
{
    debug("client_list_jobs() - ENTER");
    int retval = 0;
    void *payload = NULL;

    VALIDATE(c, "client must be non NULL", -EINVAL, client_list_jobs_end);

    if(send_pkt(c->clientfd, JOB_LIST_ALL, NULL) < 0)
        PERROR_EXIT("send_pkt()");

    int res = recv_pkt(c->clientfd, &payload);
    if(res == NACK)
    {
        printf("\rNo results returned.\n");
        goto client_list_jobs_end;
    }
    else if(res != JOB_LIST_ALL_RESP)
    {
        debug("incorrect response type");
        goto client_list_jobs_end;
    }

    listing_t *mainl = (listing_t *)payload;
    listing_t *l = NULL, *ln = NULL;

    l = mainl;
    while(l)
    {
        printf("\r[%d] (%s) %s", l->jobid, jobs_status_as_char(l->status), l->cmdline);
        if(l->status == EXITED)
        {
            printf(" <exitcode=%d>", l->exitcode);
        }
        else if(l->status == ABORTED)
        {
            printf(" <signal=%d>", l->exitcode);
        }

        printf("\n");

        l = l->next;
    }

    l = mainl;
    while(l)
    {
        ln = l->next;
        FREE(l->cmdline);
        FREE(l);
        l = ln;
    }

client_list_jobs_end:
    debug("client_list_jobs() - END");
    return retval;
}


/**
 * int client_change_priority(client_t *c, int jobid, int priority)
 *
 * @brief  Changes the priority of a job.
 *
 * @param jobid  The job to change
 * @param priority  The new priority of the job
 *
 * @return  0 on success, -errno on error.
 **/
int client_change_priority(client_t *c, int jobid, int priority)
{
    debug("client_change_priority() - ENTER");
    int retval = 0;

    VALIDATE(c, "client must be non NULL", -EINVAL, client_change_priority_end);

    priority_t *pri = NULL;
    MALLOC(pri, sizeof(priority_t));

    pri->jobid = jobid;
    pri->priority = priority;

    if(send_pkt(c->clientfd, JOB_SET_PRI, pri) < 0)
        PERROR_EXIT("send_pkt()");

    FREE(pri);

    void *payload = NULL;
    int res = recv_pkt(c->clientfd, &payload);

    if(res == NACK)
        printf("No such job found.\n");
    else if(res == ACK)
        printf("Job priority changed.\n");
    else
        debug("UNKNOWN PACKET");

client_change_priority_end:
    debug("client_change_priority() - EXIT");
    return retval;
}


/**
 * int client_kill(client_t *c, int jobid, int signum)
 *
 * @brief  Requests the server send signal to job.
 *
 * @param jobid  The job to signal
 * @param signum  The signal to send
 *
 * @return  0 on success, -errno on error.
 **/
int client_kill(client_t *c, int jobid, int signum)
{
    debug("client_kill() - ENTER");
    int retval = 0;

    VALIDATE(c, "client must be non NULL", -EINVAL, client_kill_end);

    signal_t *s = NULL;
    MALLOC(s, sizeof(signal_t));
    s->jobid = jobid;
    s->signal = signum;

    send_pkt(c->clientfd, JOB_SIGNAL, s);
    FREE(s);

    int res = recv_pkt(c->clientfd, NULL);
    if(res == NACK)
        printf("No such job found.\n");
    else if(res == ACK)
        printf("Signal sent.\n");
    else
        debug("UNKNOWN PACKET");

client_kill_end:
    debug("client_kill() - END");
    return retval;
}

/**
 * int client_expunge(client_t *c, int jobid)
 *
 * @brief  Removes a job from the client/server joblist.
 *
 * @param c  The client to remove the job from
 * @param jobid  The job id to remove
 *
 * @return  0 on success, -errno on error
 **/
int client_expunge(client_t *c, int jobid)
{
    debug("client_expunge() - ENTER");
    int retval = 0;

    VALIDATE(c, "client must be non NULL", -EINVAL, client_expunge_end);

    send_pkt(c->clientfd, JOB_EXPUNGE, &jobid);
    int res = recv_pkt(c->clientfd, NULL);
    if(res == ACK)
        printf("\rJob expunged.             \n");
    else if(res == NACK)
        printf("\rNo such job found.              \n");
    else
        debug("UNKNOWN PACKET");

client_expunge_end:
    debug("client_expunge() - EXIT");
    return retval;
}

/**
 * int client_stdout(client_t *c, int jobid)
 *
 * @brief  Retrieves the stdout output from the server
 *
 * @param c  The client requesting the output
 * @param jobid  The job id to get the output of
 *
 * @return  0 on success, -errno on error
 **/
int client_stdout(client_t *c, int jobid)
{
    debug("client_stdout() - ENTER");
    int retval = 0;

    VALIDATE(c, "client must be non NULL", -EINVAL, client_stdout_end);

    send_pkt(c->clientfd, JOB_GET_STDOUT, &jobid);

    void *payload = NULL;
    int res = recv_pkt(c->clientfd, &payload);
    if(res == JOB_RESULTS)
    {
        results_t *r = (results_t *)payload;
        printf("\n"
               "%s\n", r->results);
        FREE(r->results);
        FREE(r);
    }
    else if(res == NACK)
    {
        printf("\rServer returned no results for job.\n");
    }
    else
    {
        debug("UNKNOWN PACKET");
    }

client_stdout_end:
    debug("client_stdout() - EXIT");
    return retval;
}

/**
 * int client_stderr(client_t *c, int jobid)
 *
 * @brief  Retreives the stderr output from the server
 *
 * @param c  The client requesting the output
 * @param jobid  The job id to get the output of
 *
 * @return  0 on success, -errno on error
 **/
int client_stderr(client_t *c, int jobid)
{
    debug("client_stderr() - ENTER");
    int retval = 0;

    VALIDATE(c, "client must be non NULL", -EINVAL, client_stderr_end);

    send_pkt(c->clientfd, JOB_GET_STDERR, &jobid);

    void *payload = NULL;
    int res = recv_pkt(c->clientfd, &payload);
    if(res == JOB_RESULTS)
    {
        results_t *r = (results_t *)payload;
        printf("\n"
               "%s\n", r->results);
        FREE(r->results);
        FREE(r);
    }
    else if(res == NACK)
    {
        printf("\rServer returned no results for job.\n");
    }
    else
    {
        debug("UNKNOWN PACKET");
    }

client_stderr_end:
    debug("client_stderr() - EXIT");
    return retval;
}
