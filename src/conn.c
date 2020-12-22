/**
 * @file conn.c
 * @author Daniel Calabria
 *
 * Handles connection functionality.
 **/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "server.h"
#include "debug.h"
#include "conn.h"
#include "client.h"

/**
 * conn_t* conn_create(int fd)
 *
 * @brief  Creates a new connection associated with fd.
 *
 * @param fd  The file descriptor for this connection.
 * @return  The newly created conn_t, or NULL.
 **/
conn_t* conn_create(int fd)
{
    conn_t *c = NULL;
    MALLOC(c, sizeof(conn_t));
    c->fd = fd;

    return c;
}

/**
 * void conn_disconnect(conn_t *c)
 *
 * @brief  Performs client disconnection for client c.
 *
 * @param c  The client to disconnect.
 **/
void conn_disconnect(conn_t *c)
{
    close(c->fd);
    c->client->connected = 0;
}

/**
 * void conn_cleanup(conn_t *c)
 *
 * @brief  Performs cleanup of resources associated with a conn_t.
 *
 * @param c  The connection to release the resources of.
 **/
void conn_cleanup(conn_t *c)
{
    client_cleanup(c->client);
    c->client = NULL;
    conn_disconnect(c);
    FREE(c);
}

/**
 * void conn_remove(conn_t *c)
 *
 * @brief  Removes a connection from the server's connection list.
 *
 * @param c  The connection to remove
 **/
void conn_remove(conn_t *c)
{
    conn_disconnect(c);
    /* conn_cleanup(c); */

    if(c == server->connlist)
    {
        if(c->next)
            server->connlist = c->next;
        else
            server->connlist = NULL;
        FREE(c);
        return;
    }

    conn_t *cl = server->connlist;
    while(cl && cl->next != c)
        cl = cl->next;

    if(cl)
    {
        cl->next = c->next;
        FREE(c);
    }
}


/**
 * void conn_find_by_client(client_t *cl)
 *
 * @brief  Finds a conn_t containing the specified client.
 *
 * @param cl  The client to find
 *
 * @return  The conn_t containing the client, or NULL if none found.
 **/
conn_t* conn_find_by_client(client_t *cl)
{
    debug("conn_find_by_client() - ENTER");
    conn_t *retval = NULL;

    VALIDATE(cl, "client must be non NULL", NULL, conn_find_by_client_end);

    retval = server->connlist;
    while(retval)
    {
        if(retval->client == cl)
            return retval;

        retval = retval->next;
    }

conn_find_by_client_end:
    debug("conn_find_by_client() - EXIT");
    return retval;
}

