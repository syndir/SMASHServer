/**
 * @file jobs.c
 * @author Daniel Calabria
 * @id 103406017
 *
 * Job handling for smash.
 *
 * THIS FILE HAS BEEN REPURPOSED FROM HW3.
 **/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#include "common.h"
#include "server.h"
#include "proto.h"
#include "conn.h"
#include "debug.h"
#include "jobs.h"
#include "client.h"
#include "parse.h"

/**
 * void launch_child(command_t *, int )
 *
 * @brief  Actually launches a child process.
 *
 * @param cmd  The command to launch
 * @param pgid  The process group id this should be bound to
 * @param foreground  1 if this should be run in the foreground, 0 if background
 *
 * @return  This function never returns.
 **/
static void launch_child(command_t *cmd, int pgid, int foreground, job_t *j)
{
    debug("launch_child() - ENTER [cmd @ %p]", cmd);

    pid_t pid;

    if(!cmd)
    {
        error("cmd must be non-NULL!");
        exit(EXIT_FAILURE);
    }

    pid = getpid();
    if(pgid == 0)
        pgid = pid;
    setpgid(pid, pgid);

    /* build argv array */
    int argc = 0;
    char **argv = NULL;
    char *executable = NULL;

    /* count number of arguments */
    component_t *c = cmd->components;
    while(c)
    {
        argc++;
        c = c->next;
    }

    argv = malloc(sizeof(char *) * (argc+1));
    if(!argv)
    {
        error("malloc() failed to allocate memory for argv array");
        exit(EXIT_FAILURE);
    }
    memset(argv, 0, sizeof(char *) * (argc+1));

    c = cmd->components;
    argv[0] = c->component;

    c = c->next;

    for(int i = 1; i < argc; i++, c = c->next)
    {
        /* debug("comp is %s", c->component); */
        argv[i] = c->component;
    }
    executable = argv[0];

    debug("RUNNING: %s (pid=%d)", cmd->command, pid);

    /* open output files and dup2() then over for the process */
    if(!j->stdoutfile || !j->stderrfile)
    {
        debug("incorrect job file settings");
        goto launch_child_end;
    }

    int outfd = -1, errfd = -1;

outfd_create:
    if((outfd = creat(j->stdoutfile, S_IRUSR | S_IWUSR)) < 0)
    {
        if(errno == EINTR)
            goto outfd_create;
        PERROR_EXIT("creat()");
    }

errfd_create:
    if((errfd = creat(j->stderrfile, S_IRUSR | S_IWUSR)) < 0)
    {
        if(errno == EINTR)
            goto errfd_create;
        PERROR_EXIT("creat()");
    }

    if(dup2(outfd, STDOUT_FILENO) < 0)
        PERROR_EXIT("dup2()");
    if(close(outfd) < 0)
        PERROR_EXIT("close()");
    if(dup2(errfd, STDERR_FILENO) < 0)
        PERROR_EXIT("dup2()");
    if(close(errfd) < 0)
        PERROR_EXIT("close()");

    /* execvp searches through PATHs so we don't have to */
    debug("executing");
    if(execvpe(executable, argv, j->envp) == -1)
    {
        error("%s", strerror(errno));
        exit(EXIT_FAILURE);
    }

launch_child_end:
    /* shouldn't get here?? */
    exit(EXIT_FAILURE);
}

/**
 * int run_in_background(job_t *)
 *
 * @brief  Sends a SIGCONT signal to the process, to ensure that it's running.
 *
 * @param job  The job to run in the background
 * @param cont  1 if we should send a SIGCONT to the process, 0 otherwise
 *
 * @return  0 on success, -1 on failure
 **/
int run_in_background(job_t *job, int cont)
{
    debug("run_in_background() - ENTER [job @ %p]", job);
    int retval = 0;

    VALIDATE(job,
            "job must be non-NULL",
            -1,
            run_in_background_end);

    /* we can only run something in the foreground that hasn't already started
     * or is currently suspended */
    VALIDATE(job->status == NEW || job->status == SUSPENDED,
            "job is in incorrect state",
            -1,
            run_in_background_end);

    job->status = RUNNING;

    if(cont)
    {
        if(killpg(job->pgid, SIGCONT) < 0)
        {
            error("killpg() failed to send SIGCONT to child: %s", strerror(errno));
        }
    }

run_in_background_end:
    debug("run_in_background - EXIT [%d]", retval);
    return retval;
}

/**
 * int job_wait(job_t *)
 *
 * @brief  Waits for a job to complete.
 *
 * @param job  The job to wait for.
 *
 * @return  0 on success, -1 on error
 **/
int job_wait(job_t *job)
{
    debug("job_wait() - ENTER [job @ %p]", job);
    int retval = 0;

    pid_t pid;
    int status;

    VALIDATE(job,
            "job must be non-NULL",
            -1,
            job_wait_end);

#ifdef EXTRA_CREDIT
    if(enable_rusage)
    {
        struct rusage r;
        if((pid = wait4(job->pgid, &status, WUNTRACED, &r)) > 0)
        {
            debug("REAPED %d", pid);
            struct timeval endtime, res;
            if(gettimeofday(&endtime, NULL) < 0)
            {
                /* even if we failed getting the time, we still want to clean
                 * up the job properly */
                error("gettimeofday() failed: %s", strerror(errno));
                retval = -1;
            }

            /* how long did the job run for in real time? */
            timersub(&endtime, &job->starttime, &res);

            job_update_status(job, status);

            if(job->status == EXITED || job->status == ABORTED)
            {
                dprintf(STDERR_FILENO,
                        "TIMES: real=%ld.%1lds user=%ld.%1lds sys=%ld.%1lds\n",
                        res.tv_sec, res.tv_usec,
                        r.ru_utime.tv_sec, r.ru_utime.tv_usec,
                        r.ru_stime.tv_sec, r.ru_stime.tv_usec);
            }
        }
        goto job_wait_end;
    }
#endif

    if((pid = waitpid(job->pgid, &status, WUNTRACED)) > 0)
    {
        debug("REAPED %d", pid);
        job_update_status(job, status);
    }

job_wait_end:
    debug("job_wait() - EXIT [%d]", retval);
    return retval;
}

/**
 * int exec_job(client_t *, job_t *)
 *
 * @brief  Begins execution of a particular job.
 *
 * @param c  The client which holds the job
 * @param job  The job to begin execution of
 *
 * @return  0 on success, -1 on failure
 **/
int exec_job(client_t *c, job_t *job)
{
    debug("exec_job() - ENTER [job @ %p]", job);
    int retval = 0;

    VALIDATE(c, "client must be non NULL", -1, exec_job_end);

    VALIDATE(job,
            "job must be non-NULL",
            -1,
            exec_job_end);

    debug("%d / %d jobs", server->numjobs, server->maxjobs);
    VALIDATE(server->numjobs < server->maxjobs,
            "no room to start another job",
            0,
            exec_job_end);

    command_t *cmd = job->ui->commands;
    VALIDATE(cmd,
            "command is not valid",
            -1,
            exec_job_end);

    pid_t ppid = fork();

    if(ppid < 0)
    {
        error("fork() failed to spawn child process: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if(ppid == 0)
    {
        /* child */
        job->pgid = ppid;

        /* set up resource limits */
        struct rlimit rlim;
        rlim.rlim_cur = job->maxcpu;
        rlim.rlim_max = job->maxcpu;
        if(setrlimit(RLIMIT_CPU, &rlim) < 0)
            PERROR_EXIT("setrlimit()");

        rlim.rlim_cur = job->maxmem;
        rlim.rlim_max = job->maxmem;
        if(setrlimit(RLIMIT_AS, &rlim) < 0)
            PERROR_EXIT("setrlimit()");

        /* set priority */
        setpriority(PRIO_PROCESS, job->pgid, job->priority);

        /* launch child */
        launch_child(cmd, job->pgid, 0, job);
    }
    else
    {
        /* parent */
        job->pgid = ppid;

        /* if(interactive) */
        {
            setpgid(ppid, ppid);
        }
    }

    server->numjobs++;
    run_in_background(job, 0);

    /* send an update packet to the client */
    conn_t *conn = conn_find_by_client(c);
    if(!conn)
        goto exec_job_end;

    update_t *u = NULL;
    MALLOC(u, sizeof(update_t));
    u->jobid = job->jobid;
    u->status = job->status;
    send_pkt(conn->fd, JOB_UPDATE, u);
    FREE(u);

exec_job_end:
    debug("exec_job() - EXIT [%d]", retval);
    return retval;
}

/**
 * int job_update_status(job_t *, int)
 *
 * @brief  Updates the status field of the job
 *
 * @param j  The job to update
 * @param status  The status code
 *
 * @return  0 on success, -errno on failure
 **/
int job_update_status(job_t *j, int status)
{
    debug("job_update_status() - ENTER [job @ %p, status=%d]", j, status);
    int retval = 0;

    VALIDATE(j,
            "job must be non-NULL",
            -EINVAL,
            job_update_status_end);

    if(WIFSTOPPED(status))
    {
        /* stopped/suspended */
        j->status = SUSPENDED;
    }
    else if(WIFCONTINUED(status))
    {
        /* continued */
        j->status = RUNNING;
    }
    else if(WIFSIGNALED(status))
    {
        /* killed/aborted */
        j->status = ABORTED;
        j->exitcode = WTERMSIG(status);
        debug("ABORTED: %s <signal=%d>", j->ui->input, j->exitcode);

    }
    else if(WIFEXITED(status))
    {
        /* exited */
        j->status = EXITED;
        j->exitcode = WEXITSTATUS(status);
        debug("ENDED: %s <ret=%d>", j->ui->input, j->exitcode);
    }

job_update_status_end:
    debug("job_update_status() - EXIT [%d]", retval);
    return retval;
}

/**
 * int free_job(job_t *)
 *
 * @brief  Frees all memory associated with the corresponding job structure.
 *
 * @param job  The job to free
 *
 * @return  0 on success, -errno on failure
 **/
int free_job(job_t *job)
{
    debug("free_job() - ENTER [job @ %p]", job);
    int retval = 0;

    VALIDATE(job,
            "job is already NULL",
            -EINVAL,
            free_job_end);

    free_input(job->ui);

    for(int i = 0; i < job->envpc; i++)
        FREE(job->envp[i]);
    FREE(job->envp);
    if(job->stdoutfile)
    {
        unlink(job->stdoutfile);
        FREE(job->stdoutfile);
    }
    if(job->stderrfile)
    {
        unlink(job->stderrfile);
        FREE(job->stderrfile);
    }
    FREE(job);

free_job_end:
    debug("free_job() - EXIT [%d]", retval);
    return retval;
}

/**
 * void free_jobs(jobs_t *)
 *
 * @brief Frees all jobs in the joblist.
 *
 * @param jobs  The list of jobs to free
 **/
void free_jobs(client_t *c)
{
    debug("free_jobs() - ENTER");
    if(!c)
        return;

    job_t *j = c->jobs, *jn = NULL;

    while(j)
    {
        jn = j->next;
        free_job(j);
        j = jn;
    }

    c->jobs = NULL;
    debug("free_jobs() - EXIT");
}

/**
 * job_t* jobs_create(user_input_t *)
 *
 * @brief  Creates a new job structure, storing the user_input_t* inside of it.
 *
 * @param ui  Pointer to the user_input to store within the created job.
 *
 * @return  A pointer to the newly created job_t, or NULL on failure.
 **/
job_t* jobs_create(user_input_t *ui)
{
    debug("jobs_create() - ENTER [ui @ %p]", ui);
    job_t *retval = NULL;

    VALIDATE(ui,
            "user_input_t must be non-NULL",
            NULL,
            jobs_create_end);

    retval = malloc(sizeof(job_t));
    VALIDATE(retval,
            "malloc() failed to allocate new job_t",
            NULL,
            jobs_create_end);

    /* clear it, instead of setting everything to 0 by hand */
    memset(retval, 0, sizeof(job_t));

    retval->ui = ui;

jobs_create_end:
    debug("jobs_create() - EXIT [%p]", retval);
    return retval;
}

/**
 * int jobs_insert(client_t *c, job_t *)
 *
 * @brief  Inserts a job into the joblist, and updates the job's jobid to be
 *         1 higher than the previous node's jobid (or 1, for the first job).
 *
 * @param c  The client who owns the job
 * @param job  The job to insert
 *
 * @return  0 on success, -errno on failure
 **/
int jobs_insert(client_t *c, job_t *job)
{
    debug("jobs_insert() - ENTER [job @ %p]", job);
    int retval = 0;

    VALIDATE(c,
            "client must be non NULL",
            -EINVAL,
            jobs_insert_end);

    VALIDATE(job,
            "job must be non-NULL",
            -EINVAL,
            jobs_insert_end);

    job->next = NULL;
    job->owner = c;

    /* insert the job into the clients list of jobs */
    /* empty list? */
    if(c->jobs == NULL)
    {
        job->jobid = c->numjobs++;
        c->jobs = job;
    }
    else
    {
        job_t *j = c->jobs;
        while(j->next)
            j = j->next;

        job->jobid = c->numjobs++;
        j->next = job;
    }

    /* insert the job in the servers list of ALL jobs */
    if(server->joblist == NULL)
    {
        job->snext = server->joblist;
        server->joblist = job;
    }
    else
    {
        job_t *j = server->joblist;
        while(j->snext)
            j = j->snext;
        j->snext = job;
    }


    /* set up the output files for the job */
    char outf[NAME_MAX];
    struct timeval tv;
    gettimeofday(&tv, NULL);

    snprintf(outf, NAME_MAX-1, "%s_%ld%ld.out",
            c->name, tv.tv_sec, tv.tv_usec);
    debug("using \'%s\' for stdout file", outf);
    job->stdoutfile = strdup(outf);

    snprintf(outf, NAME_MAX-1, "%s_%ld%ld.err",
            c->name, tv.tv_sec, tv.tv_usec);
    debug("using \'%s\' for stderr file", outf);
    job->stderrfile = strdup(outf);

jobs_insert_end:
    debug("jobs_insert() - EXIT [%d]", retval);
    return retval;
}

/**
 * int jobs_remove(client_t *, job_t *)
 *
 * @brief  Remove the specified job from the joblist.
 *
 * @param c  The client which owns the job
 * @param job  The job to remove
 *
 * @return  0 on success, -errno on failure
 **/
int jobs_remove(client_t *c, job_t *job)
{
    debug("jobs_remove() - ENTER [job @ %p]", job);
    int retval = -EINVAL;

    VALIDATE(c,
            "client must be non NULL",
            -EINVAL,
            jobs_remove_end);

    job_t *j = c->jobs;
    job_t *jp = NULL;

    VALIDATE(job,
            "can not remove a NULL job",
            -EINVAL,
            jobs_remove_end);

    /* remove from client list of jobs */
    /* head of list? */
    if(j == job)
    {
        c->jobs = c->jobs->next;
        retval = 0;
        goto jobs_remove_done_client;
    }
    while(j)
    {
        if(j == job)
        {
            jp->next = j->next;
            retval = 0;
            break;
        }

        jp = j;
        j = j->next;
    }

jobs_remove_done_client:
    /* remove from server list of ALL jobs */
    j = server->joblist;
    if(j == job)
    {
        server->joblist = server->joblist->snext;
        free_job(j);
        retval = 0;
        goto jobs_remove_end;
    }
    jp = j; j = j->snext;
    while(j)
    {
        if(j == job)
        {
            jp->snext = j->snext;
            free_job(j);
            retval = 0;
            break;
        }

        jp = j; j = j->snext;
    }

jobs_remove_end:
    debug("jobs_remove() - EXIT [%d]", retval);
    return retval;
}

/**
 * job_t* jobs_lookup_by_jobid(client_t *, int)
 *
 * @brief  Finds and returns a pointer to the job with the specified jobid.
 *
 * @param c  The client to search through
 * @param jobid  The jobid to find
 *
 * @return  A pointer to the job_t structure with the corresponding jobid, or
 *          NULL if not found or on error.
 **/
job_t* jobs_lookup_by_jobid(client_t *c, int jobid)
{
    debug("jobs_lookup_by_jobid() - ENTER [jobid=%d]", jobid);

    job_t *retval = NULL;
    VALIDATE(c,
            "client must be non NULL",
            NULL,
            jobs_lookup_by_id_end);

    retval = c->jobs;

    while(retval)
    {
        if(retval->jobid == jobid)
            break;
        retval = retval->next;
    }

jobs_lookup_by_id_end:
    debug("jobs_lookup_by_jobid() - EXIT [%p]", retval);
    return retval;
}

/**
 * job_t* jobs_lookup_by_pid(job_t *joblist, int pid)
 *
 * @brief  Searches through a list of jobs looking for the specified pid.
 *
 * @param joblist  The list of jobs to search
 * @param pid  The pid of the target job
 *
 * @return  A pointer to the job_t with the corresponding pid, or NULL if not
 * found or on error.
 **/
job_t* jobs_lookup_by_pid(job_t *joblist, int pid)
{
    debug("jobs_lookup_by_pid() - ENTER [pid=%d]", pid);

    job_t *retval = NULL;
    VALIDATE(joblist, "joblist must be non NULL", NULL, jobs_lookup_by_pid_end);

    retval = joblist;

    while(retval)
    {
        if(retval->pgid == pid)
            break;
        retval = retval->snext;
    }

jobs_lookup_by_pid_end:
    debug("jobs_lookup_by_pid() - EXIT [%p]", retval);
    return retval;
}

/**
 * int cancel_all_jobs(client_t *)
 *
 * @brief  Cancels all jobs currently running.
 *
 * @param c  The client to cancel the jobs of
 * @return  0 on success, -errno on error
 **/
int cancel_all_jobs(client_t *c)
{
    debug("cancel_all_jobs() - ENTER");
    int retval = 0;

    VALIDATE(c, "client must be non NULL", -EINVAL, cancel_all_jobs_end);

    job_t *j = c->jobs;
    while(j)
    {
        if(j->status == RUNNING || j->status == SUSPENDED)
        {
            /* kill it without prejudice */
            debug("canceling job with pid=%d", j->pgid);
            killpg(j->pgid, SIGKILL);
            j->status = CANCELED;
        }
        else if(j->status == NEW)
            j->status = ABORTED;

        j = j->next;
    }

cancel_all_jobs_end:
    debug("cancel_all_jobs() - EXIT [%d]", retval);
    return retval;
}

/**
 * int wait_for_all()
 *
 * @brief  Waits for all jobs to complete, so that we may reap all children
 *         properly. This is only intended to be used after cancel_all_jobs()
 *         is called, so that we may properly reap all child processes before
 *         exiting the shell.
 *
 * @param c  The client to wait for
 * @return  0 on success, -errno on error
 **/
int wait_for_all(client_t *c)
{
    debug("wait_for_all() - ENTER");
    int retval = 0;

    VALIDATE(c, "client must be non NULL", -EINVAL, wait_for_all_end);
    job_t *j = c->jobs;

    pid_t pid;
    int status;

    while(j)
    {
        /* live process? */
        if(j->status == RUNNING || j->status == SUSPENDED || j->status == CANCELED)
        {
            while((pid = waitpid(j->pgid, &status, 0)) < 0)
            {
                if(errno == EINTR)
                    continue;

                /* since we're probably being called from the exit callback, we
                 * can't exit() here. use _exit(). */
                _exit(-1);

            }

            if(WIFEXITED(status))
            {
                j->status = EXITED;
                j->exitcode = WEXITSTATUS(status);
            }
            else if(WIFSIGNALED(status))
            {
                j->status = ABORTED;
                j->exitcode = WTERMSIG(status);
            }
        }

        j = j->next;
    }

wait_for_all_end:
    debug("wait_for_all() - END [%d]", retval);
    return retval;
}

/**
 * int print_job(job_t *)
 *
 * @brief  Prints information about a job
 *
 * @param j  The job to output
 *
 * @return  0 on success, -errno on error.
 **/
int print_job(job_t *j)
{
    debug("print_job() - ENTER [job @ %p]", j);
    int retval = 0;

    VALIDATE(j,
            "job must be non-NULL",
            -EINVAL,
            print_job_end);

    if(j->status != EXITED && j->status != ABORTED)
    {
        if(dprintf(STDOUT_FILENO, "[%d] (%s) %s\n",
                    j->jobid, jobs_status_as_char(j->status), j->ui->input) < 0)
        {
            retval = -1;
            goto print_job_end;
        }
    }
    else
    {
        if(dprintf(STDOUT_FILENO, "[%d] (%s <%d>) %s\n",
                    j->jobid, jobs_status_as_char(j->status), j->exitcode,
                    j->ui->input) < 0)
        {
            retval = -1;
            goto print_job_end;
        }
    }

print_job_end:
    debug("print_job() - EXIT [%d]", retval);
    return retval;
}

/**
 * int jobs_list(client_t *)
 *
 * @brief  Outputs a neatly formatted list of all jobs our shell is currently
 *         responsible for.
 *
 * @param c  The client to retrieve the listing of.
 * @return  0 on success, -1 on error
 **/
int jobs_list(client_t *c)
{
    debug("jobs_list() - ENTER");
    int retval = 0;

    VALIDATE(c, "client must be non NULL", -1, jobs_list_end);

    job_t *j = c->jobs;
    while(j)
    {
        VALIDATE(print_job(j) == 0,
                "print_job() failed to output job information",
                -1,
                jobs_list_end);
        if(j->status == EXITED || j->status == ABORTED)
        {
            job_t *jn = j->next;
            jobs_remove(c, j);
            j = jn;
        }
        else
            j = j->next;
    }

jobs_list_end:
    debug("jobs_list() - EXIT [%d]", retval);
    return retval;
}

/**
 * char* jobs_status_as_char(int)
 *
 * @brief  Converts a job status as an integer to a string representation.
 *
 * @param status  The status to convert
 *
 * @return  The string corresponding to the status, or NULL if not valid.
 **/
char* jobs_status_as_char(int status)
{
    switch(status)
    {
        case NEW:
            return "new";

        case RUNNING:
            return "running";

        case SUSPENDED:
            return "suspended";

        case EXITED:
            return "exited";

        case ABORTED:
            return "aborted";

        case CANCELED:
            return "canceled";

        default:
            return NULL;
    }
}
