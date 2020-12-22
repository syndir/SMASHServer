#ifndef PROTO_H
#define PROTO_H

#include <stdint.h>
#include <sys/time.h>
#include <sys/resource.h>

/**
 * MUTUAL:
 *  ACK                 - acknowledgement of command successfully received
 *  NACK                - command was unsuccessfully received
 *
 * CLIENT specific:
 *  LOGIN               - should be first packet sent by client on connection.
 *  JOB_SUBMIT          - client wants to submit a new job.
 *  JOB_STATUS          - client wants to know status of a job
 *  JOB_SIGNAL          - client wants to kill/stop/start a job
 *  JOB_SET_PRI         - client wants to change the priority of a job
 *  JOB_GET_STDOUT      - client wants the standard output of a job
 *  JOB_GET_STDERR      - client wants toe standard err of a job
 *  JOB_LIST_ALL        - client wants a list of all their jobs (+ status)
 *  JOB_EXPUNGE         - client wants to remove a job from their joblist
 *
 * SERVER specific:
 *  JOB_UPDATE          - sent by server to client when status a job changes
 *  JOB_SUBMIT_SUCCESS  - job was successfully submitted to server.
 *  JOB_RESULTS         - sent by server to client, packet contains results of a job
 *  JOB_STATUS_RESP     - response to a client's STATUS request
 *  JOB_LIST_ALL_RESP   - response to a client's LIST_ALL request
 **/

#define ACK             1 /* ACKnowledgement */
#define NACK            2 /* Non-ACKknowledgement */

/* CLIENT specific */
#define LOGIN           3 /* LOGIN packet */
#define JOB_SUBMIT      4 /* SUBMIT new job */
#define JOB_STATUS      5 /* get some job's STATUS */
#define JOB_SIGNAL      6 /* SIGNAL target job */
#define JOB_SET_PRI     7 /* change the PRIORITY/NICE of some job */
#define JOB_GET_STDOUT  8 /* retrieved STANDARD OUTPUT of a job */
#define JOB_GET_STDERR  9 /* retrieved STANDARD ERROR of a job */
#define JOB_LIST_ALL   10 /* list all info about client jobs */
#define JOB_EXPUNGE    11 /* EXPUNGE a specified job */

/* SERVER specific */
#define JOB_SUBMIT_SUCCESS  12 /* job was successfully submitted */
#define JOB_STATUS_RESP     13 /* response packet for status request */
#define JOB_UPDATE          14 /* update packet for a job status change */
#define JOB_LIST_ALL_RESP   15 /* response packet for a listing of all client jobs */
#define JOB_RESULTS         16 /* results packet containing output of a job */

/**
 * job submission structure
 **/
typedef struct submission_s
{
    uint32_t maxcpu;
    uint32_t maxmem;
    int32_t priority;

    uint32_t cmdlen;
    char *cmdline;

    uint32_t envpc;
    char **envp;
} submission_t;

/**
 * job status structure 
 **/
typedef struct status_s
{
    uint32_t status;
    int32_t exitcode;

    uint32_t maxcpu;
    uint32_t maxmem;
    int32_t priority;

    struct rusage ru;
} status_t;

/**
 * job update structure
 **/
typedef struct update_s
{
    uint32_t jobid;
    uint32_t status;
} update_t;

/**
 * job listing structure
 **/
typedef struct listing_s
{
    uint32_t jobid;
    uint32_t left;
    uint32_t cmdlen;
    char *cmdline;
    uint32_t status;
    int32_t exitcode;
    
    struct listing_s *next;
} listing_t;

/**
 * job priority change structure
 **/
typedef struct priority_s
{
    uint32_t jobid;
    int32_t priority;
} priority_t;

/**
 * job signal request structure
 **/
typedef struct signal_s
{
    uint32_t jobid;
    uint32_t signal;
} signal_t;

/**
 * job results structure
 **/
typedef struct results_s
{
    uint32_t length;
    char*    results;
} results_t;

/* fxn prototypes */
int send_pkt(int fd, char packet_type, void *payload);
int recv_pkt(int fd, void **payload);

#endif // PROTO_H
