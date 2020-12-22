/**
 * @file server.h
 * @author Daniel Calabria
 **/

#ifndef SERVER_H
#define SERVER_H

#include "client.h"
#include "conn.h"

/* Server representation */
typedef struct server_s
{
    int maxjobs;
    int numjobs;

    client_t *clientlist;
    conn_t *connlist;
    job_t *joblist;

    char *socket_file;
} server_t;

extern server_t *server;

/* fxn prototypes */
void server_shutdown(int exitcode);
int server_disconnect_client(conn_t *c);
int server_remove_client(client_t *c);
client_t* server_login_client(char *name);
int server_handle_client(conn_t *conn);
void handle_all_signals();
void server_handler(int sig);
int server_init();
conn_t* server_register_conn(int fd);

#endif // SERVER_H
