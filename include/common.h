/**
 * @file common.h
 * @author Daniel Calabria
 * @id 103406017
 **/

#ifndef COMMON_H
#define COMMON_H

#include <signal.h>

/* filename of the socket */
#define SOCKET_NAME ".cse376hw4.socket"

extern volatile sig_atomic_t debug_enabled;

/* Macro to perform NULL checks on values (or other expressions
 * which also evaluate to 0) */
#define VALIDATE(arg, msg, rval, jmp) \
    { \
        if(!(arg)) \
        { \
            debug("%s", (msg)); \
            retval = (rval); \
            goto jmp; \
        } \
    }

/* MALLOC macro */
#define MALLOC(var, size) \
    { \
        (var) = malloc(size); \
        if((var) == NULL) \
        { \
            PERROR_EXIT("malloc()"); \
        } \
        memset((var), 0, (size)); \
    }

/* FREE macro -- if x is non-NULL, free() it, then set it to NULL */
#define FREE(x) \
    if((x)) \
    { \
        free((x)); \
        (x) = NULL; \
    }

/* PERROR_EXIT macro -- prints an error message, then exits the program */
#define PERROR_EXIT(x) \
    { \
        perror((x));\
        exit(EXIT_FAILURE); \
    }


/* WRITE macro */
#define WRITE(fd, msg, len) \
    { \
        if(write((fd), (msg), (len)) < 0) \
        { \
            if(errno == EBADF || errno == EPIPE) return -1; \
            PERROR_EXIT("write()"); \
        } \
    }

#define READ(fd, msg, len) \
{ \
    int r = 0; \
    while((r = read((fd), (msg), (len))) < 0) \
    { \
        if(errno == EINTR) continue; \
        if(errno == EBADF) return -1; \
        PERROR_EXIT("read()"); \
    } \
    if(r == 0) \
    { \
        perror("read()");\
        return -1;\
    } \
}

#endif // COMMON_H
