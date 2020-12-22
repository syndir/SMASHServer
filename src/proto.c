/**
 * @file proto.c
 * @author Daniel Calabria
 *
 * Handles transmission of packets, encoding them to our protocol.
 **/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "common.h"
#include "debug.h"
#include "proto.h"

/**
 * int send_pkt(int fd, int packet_type, void *payload)
 *
 * @brief  Sends a packet through fd.
 *
 * @param fd  The file descriptor to write to
 * @param pocket_type  What kind of packet to send
 * @param payload  The data to send
 *
 * @return  0 on success, -errno on error.
 **/
int send_pkt(int fd, char packet_type, void *payload)
{
    debug("send_pkt - ENTER");
    int retval = 0;

    switch(packet_type)
    {
        /* sends an ACK. suitable for a "yes"/"ok" response. */
        case ACK:
        {
            debug("sending ACK to %d", fd);
            WRITE(fd, &packet_type, sizeof(char));
            break;
        }

        /* sends a NACK. suitable for a "no"/"not ok" response. */
        case NACK:
        {
            debug("sending NACK to %d", fd);
            WRITE(fd, &packet_type, sizeof(char));
            break;
        }

        /* login packet */
        case LOGIN:
        {
            VALIDATE(payload, "payload must be non NULL", -EINVAL, send_pkt_end);

            WRITE(fd, &packet_type, sizeof(char));

            char *name = (char *)payload;
            int len = strlen(name) + 1;

            /* write length of name */
            debug("sending %d", len);
            WRITE(fd, &len, sizeof(uint32_t));

            /* write name */
            debug("sending %s", name);
            WRITE(fd, name, len);

            break;
        }

        /* job update packet */
        case JOB_UPDATE:
        {
            VALIDATE(payload, "payload must be non NULL", -EINVAL, send_pkt_end);
            update_t *u = (update_t *)payload;
            WRITE(fd, &packet_type, sizeof(char));
            WRITE(fd, u, sizeof(update_t));
            break;
        }

        /* job submission packet */
        case JOB_SUBMIT:
        {
            VALIDATE(payload, "payload must be non NULL", -EINVAL, send_pkt_end);
            submission_t *s = (submission_t *)payload;

            /* pkt type */
            WRITE(fd, &packet_type, sizeof(char));

            /* maxcpu */
            WRITE(fd, &s->maxcpu, sizeof(uint32_t));

            /* maxmem */
            WRITE(fd, &s->maxmem, sizeof(uint32_t));

            /* priority */
            WRITE(fd, &s->priority, sizeof(int32_t));

            /* cmdline length */
            WRITE(fd, &s->cmdlen, sizeof(uint32_t));

            /* cmdline */
            WRITE(fd, s->cmdline, s->cmdlen);

            /* envpc */
            WRITE(fd, &s->envpc, sizeof(uint32_t));

            /* envp */
            for(int i = 0; i < s->envpc; i++)
            {
                int len = strlen(s->envp[i]);
                WRITE(fd, &len, sizeof(uint32_t));
                WRITE(fd, s->envp[i], len);
            }

            break;
        }

        case JOB_SUBMIT_SUCCESS:
        case JOB_STATUS:
        case JOB_EXPUNGE:
        case JOB_GET_STDOUT:
        case JOB_GET_STDERR:
        {
            VALIDATE(payload, "payload must be non NULL", -EINVAL, send_pkt_end);
            WRITE(fd, &packet_type, sizeof(char));
            uint32_t *jobid = (uint32_t *)payload;
            WRITE(fd, jobid, sizeof(uint32_t));
            break;
        }

        /* JOB_STATUS_RESP */
        case JOB_STATUS_RESP:
        {
            VALIDATE(payload, "paylaod must be non NULL", -EINVAL, send_pkt_end);
            WRITE(fd, &packet_type, sizeof(char));
            status_t *s = (status_t *)payload;
            WRITE(fd, s, sizeof(status_t));
            break;
        }

        /* JOB_LIST_ALL */
        case JOB_LIST_ALL:
        {
            WRITE(fd, &packet_type, sizeof(char));
            break;
        }

        /* JOB_LIST_ALL_RESP */
        case JOB_LIST_ALL_RESP:
        {
            VALIDATE(payload, "payload must be non NULL", -EINVAL, send_pkt_end);
            WRITE(fd, &packet_type, sizeof(char));
            listing_t *l = (listing_t *)payload;
            while(l && l->left >= 0)
            {
                WRITE(fd, &l->jobid, sizeof(uint32_t));
                WRITE(fd, &l->left, sizeof(uint32_t));
                WRITE(fd, &l->cmdlen, sizeof(uint32_t));
                WRITE(fd, l->cmdline, sizeof(char) * l->cmdlen);
                WRITE(fd, &l->status, sizeof(uint32_t));
                WRITE(fd, &l->exitcode, sizeof(int32_t));
                l = l->next;
            }

            break;
        }

        /* JOB_SET_PRI */
        case JOB_SET_PRI:
        {
            VALIDATE(payload, "payload must be non NULL", -EINVAL, send_pkt_end);
            WRITE(fd, &packet_type, sizeof(char));
            priority_t *p = (priority_t *)payload;
            WRITE(fd, p, sizeof(priority_t));
            break;
        }

        /* JOB_SIGNAL */
        case JOB_SIGNAL:
        {
            VALIDATE(payload, "payload must be non NULL", -EINVAL, send_pkt_end);
            WRITE(fd, &packet_type, sizeof(char));
            signal_t *s = (signal_t *)payload;
            WRITE(fd, s, sizeof(signal_t));
            break;
        }

        /* JOB_RESULTS */
        case JOB_RESULTS:
        {
            VALIDATE(payload, "payload must be non NULL", -EINVAL, send_pkt_end);
            WRITE(fd, &packet_type, sizeof(char));
            results_t *r = (results_t *)payload;
            WRITE(fd, &r->length, sizeof(uint32_t));
            WRITE(fd, r->results, r->length);
            break;
        }

        default:
            break;
    }

send_pkt_end:
    debug("send_pkt - EXIT");
    return retval;
}

/**
 * int recv_pkt(int fd, void **payload)
 *
 * @brief  Receives a packet on fd.
 * @param fd  The file descriptor to read from
 * @param payload  Pointer to pointer for payload storage
 *
 * @return  The type of packet received on success, -errno on error.
 **/
int recv_pkt(int fd, void **payload)
{
    debug("recv_pkt - ENTER");
    int retval = 0;
    char c;
    int r;

    /* read 1 byte to determine what COMMAND prefix it is */
    while((r = read(fd, &c, sizeof(char))) < 0)
    {
        if(errno == EINTR)
            continue;
        if(errno == EBADF)
            return -EBADF;
        /* PERROR_EXIT("read()"); */
        return -errno;
    }

    /* client gave EOF? */
    if(r == 0)
    {
        /* disconnect client */
        retval = -1;
        debug("client fd %d gave EOF", fd);
        goto recv_pkt_end;

    }


    /* what did they want to do? */
    switch(c)
    {
        case ACK:
        {
            debug("got ACK");
            retval = ACK;
            break;
        }

        case NACK:
        {
            debug("got NACK");
            retval = NACK;
            break;
        }

        /* some job changed status */
        case JOB_UPDATE:
        {
            debug("update packet incoming");
            update_t *u = NULL;
            MALLOC(u, sizeof(update_t));
            READ(fd, u, sizeof(update_t));
            *payload = u;
            retval = JOB_UPDATE;

            break;
        }

        /* login packet */
        case LOGIN:
        {
            debug("login packet incoming");

            /* read length of name */
            int len;
            READ(fd, &len, sizeof(uint32_t));
            debug("name length %d", len);

            char *name = NULL;
            MALLOC(name, len);
            READ(fd, name, len);
            debug("read name %s", name);

            *payload = name;
            retval = LOGIN;

            break;
        }

        /* job submission packet */
        case JOB_SUBMIT:
        {
            debug("submission packet incoming");
            submission_t *j = NULL;
            MALLOC(j, sizeof(submission_t));

            /* cpu */
            READ(fd, &j->maxcpu, sizeof(uint32_t));
            debug("maxcpu %d", j->maxcpu);

            /* mem */
            READ(fd, &j->maxmem, sizeof(uint32_t));
            debug("maxmem %d", j->maxmem);

            /* priority */
            READ(fd, &j->priority, sizeof(int32_t));
            debug("pri %d", j->priority);

            /* cmdlen */
            READ(fd, &j->cmdlen, sizeof(uint32_t));
            debug("cmd len %d", j->cmdlen);

            /* cmd */
            MALLOC(j->cmdline, sizeof(char) * (j->cmdlen + 1));
            READ(fd, j->cmdline, j->cmdlen);
            debug("cmd: %s", j->cmdline);

            /* envpc */
            READ(fd, &j->envpc, sizeof(uint32_t));
            debug("envpc %d", j->envpc);

            /* envp */
            MALLOC(j->envp, sizeof(char *) * (j->envpc + 1));
            for(int i = 0; i < j->envpc; i++)
            {
                int len = 0;
                READ(fd, &len, sizeof(uint32_t));

                MALLOC(j->envp[i], sizeof(char) * (len + 1));
                READ(fd, j->envp[i], len);
                debug("got envp[%d] = %s", i, j->envp[i]);
            }

            retval = JOB_SUBMIT;
            *payload = j;
            break;
        }

        case JOB_SUBMIT_SUCCESS:
        case JOB_STATUS:
        case JOB_EXPUNGE:
        case JOB_GET_STDOUT:
        case JOB_GET_STDERR:
        {
            uint32_t *jobid = NULL;
            MALLOC(jobid, sizeof(uint32_t));
            READ(fd, jobid, sizeof(uint32_t));
            retval = c;
            *payload = jobid;
            break;
        }

        /* response to a JOB_STATUS request */
        case JOB_STATUS_RESP:
        {
            status_t *s = NULL;
            MALLOC(s, sizeof(status_t));
            READ(fd, s, sizeof(status_t));
            retval = JOB_STATUS_RESP;
            *payload = s;
            break;
        }

        /* JOB_LIST_ALL */
        case JOB_LIST_ALL:
        {
            retval = c;
            break;
        }

        /* response to a JOB_LIST_ALL request */
        case JOB_LIST_ALL_RESP:
        {
            listing_t *mainl = NULL, *l = NULL, *ln = NULL;

            MALLOC(mainl, sizeof(listing_t));

            l = mainl;
            do
            {
                READ(fd, &l->jobid, sizeof(uint32_t));
                READ(fd, &l->left, sizeof(uint32_t));
                READ(fd, &l->cmdlen, sizeof(uint32_t));
                MALLOC(l->cmdline, sizeof(char) * l->cmdlen);
                READ(fd, l->cmdline, sizeof(char) * l->cmdlen);
                READ(fd, &l->status, sizeof(uint32_t));
                READ(fd, &l->exitcode, sizeof(uint32_t));

                ln = NULL;
                if(l->left > 0)
                    MALLOC(ln, sizeof(listing_t));
                l->next = ln;
                l = l->next;
            } while(l);

            *payload = mainl;
            retval = c;
            break;
        }

        /* JOB_SET_PRI */
        case JOB_SET_PRI:
        {
            priority_t *pri = NULL;
            MALLOC(pri, sizeof(priority_t));
            READ(fd, pri, sizeof(priority_t));
            *payload = pri;
            retval = c;
            break;
        }

        /* JOB_SIGNAL (kill/stop/resume) */
        case JOB_SIGNAL:
        {
            signal_t *s = NULL;
            MALLOC(s, sizeof(signal_t));
            READ(fd, s, sizeof(signal_t));
            *payload = s;
            retval = c;
            break;
        }

        /* JOB_RESULTS */
        case JOB_RESULTS:
        {
            results_t *result = NULL;
            MALLOC(result, sizeof(results_t));
            READ(fd, &result->length, sizeof(uint32_t));
            MALLOC(result->results, sizeof(char) * (result->length + 1));
            READ(fd, result->results, result->length);
            *payload = result;
            retval = c;
            break;
        }

        default:
            break;
    }

recv_pkt_end:
    debug("recv_pkt - EXIT");
    return retval;
}
