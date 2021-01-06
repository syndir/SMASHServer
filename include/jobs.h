/**
 * @file jobs.h
 * @author Daniel Calabria
 *
 * Header file for jobs.c
 **/

#ifndef JOBS_H
#define JOBS_H

#include <stdint.h>
#include <sys/resource.h>

#include "client.h"
#include "parse.h"

typedef struct client_s client_t;

/* The various states that our jobs can be in */
#define NEW         0
#define RUNNING     1
#define SUSPENDED   2
#define EXITED      3
#define ABORTED     4
#define CANCELED    5


/**
 * Our job structure. This tracks information about each job.
 **/
typedef struct job_s
{
    user_input_t *ui;
    client_t *owner;
    struct rusage ru;

    uint32_t status;
    uint32_t exitcode;
    uint32_t pgid;
    uint32_t jobid;

    uint32_t maxmem;
    uint32_t maxcpu;
    uint32_t usedmem;
    uint32_t usedcpu;

    int32_t priority;
    
    uint32_t envpc;
    char **envp;

    char *stdoutfile;
    char *stderrfile;

    struct job_s *next;
    struct job_s *snext;
} job_t;

/* fxn prototypes for jobs.c */
job_t* jobs_create(user_input_t *ui);
int jobs_insert(client_t *, job_t *);
int jobs_remove(client_t *, job_t *);
int jobs_list(client_t *);
job_t* jobs_lookup_by_jobid(client_t *, int jobid);
job_t* jobs_lookup_by_pid(job_t *joblist, int pid);
char* jobs_status_as_char(int);
void free_jobs(client_t *);
int free_job(job_t *);
int exec_job(client_t *, job_t *job);
int cancel_all_jobs(client_t *);
int wait_for_all(client_t *);
int job_update_status(job_t *job, int status);
int print_job(job_t *j);
int run_in_background(job_t *job, int cont);

#endif // JOBS_H
