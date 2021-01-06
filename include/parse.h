/**
 * @file parse.h
 * @author Daniel Calabria
 *
 * Header file for parse.c
 *
 * parse.c is responsible for parsing user input and managing the memory
 * associated with the breakdown of these inputs.
 *
 * Given the input `ls -l | sort`:
 *
 * A user_input is the raw user input -> `ls -l | sort`
 * It will contain a list of 2 commands -> `ls -l` and `sort`.
 * Each command will be broken into individual components, separated by
 * whitespace; (`ls -l` -> `ls`, `-l`) and (`sort` -> `sort`).
 **/

#ifndef PARSE_H
#define PARSE_H

#define COMPONENT_DELIMS    "\t\r\n "   /* Delimiters for components */

/**
 * The component structure is used to maintain a list of individual tokens
 * comprising a command.
 **/
typedef struct component_s
{
    char *component;
    struct component_s *next;   /* next component */
} component_t;

/**
 * The command struct is used to wrap information about a particular command,
 * along with any redirection and piping requirements 
 **/
typedef struct command_s
{
    char *command;

    int in_fd;                  /* if we need to redirect w/ pipes */
    int out_fd;

    component_t *components;    /* each command is composed of 1+ components */
    struct command_s *next;     /* next command */
} command_t;

/**
 * The user_input struct is used to wrap/contain an ENTIRE line of user input
 * in it's unadulterated form. It contains a list of broken down commands.
 **/
typedef struct user_input_s
{
    char *input;

    command_t *commands;        /* each user_input is composed of 1+ commands */
} user_input_t;

/* fxn prototypes for parse.c */
user_input_t* parse_input(char *input);
int free_input(user_input_t *ui);

#endif // PARSE_H
