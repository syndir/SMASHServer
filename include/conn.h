/**
 * @file conn.h
 * @author Daniel Calabria
 **/

#ifndef CONN_H
#define CONN_H

#include "client.h"

/* Represents a connection */
typedef struct conn_s
{
    int fd;

    client_t *client;

    struct conn_s *next;
} conn_t;

/* fxn prototypes */
conn_t *conn_create(int fd);
void conn_disconnect(conn_t *);
void conn_remove(conn_t *);
void conn_cleanup(conn_t *);
conn_t* conn_find_by_client(client_t *cl);

#endif // CONN_H
