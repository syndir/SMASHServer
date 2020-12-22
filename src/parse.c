/**
 * @file parse.c
 * @author Daniel Calabria
 * @id 103406017
 *
 * Parses a string into it's comprised parts.
 *
 * A 'user_input' is of the form: `./a; ./b; ./c -t c`
 *
 * Given this as user input, it will be further decomposed into:
 * A `command` list which is of the form: `./a`, `./b`, and `./c -t c`
 *
 * Each command will be then be decomposed into it's individual delimited
 * components:
 * A `component` is of the form: `./a`, `./b`, and `./c` `-t` `c`
 *
 * THIS FILE HAS BEEN REPURPOSED FROM HW3.
 **/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "common.h"
#include "debug.h"
#include "parse.h"

/**
 * void free_components(component_t *)
 *
 * @brief  Frees memory associated with components, which are stored as SLL.
 *         This will traverse the list, freeing the entries as it progresses.
 *
 * @param comp  The head of the list of components to free.
 **/
static void free_components(component_t *comp)
{
    if(comp)
    {
        component_t *n = comp->next;
        while(comp)
        {
            n = comp->next;
            FREE(comp->component);
            FREE(comp);
            comp = n;
        }
    }
}

/**
 * void free_commands(command_t *)
 *
 * @brief  Frees memory associated with commands, which are stored as SLL. This
 *         will traverse the list, freeing the entries as it progresses.
 *
 * @param cmd  The head of the list of commands to free.
 **/
static void free_commands(command_t *cmd)
{
    if(cmd)
    {
        command_t *n = cmd->next;
        while(cmd)
        {
            n = cmd->next;
            free_components(cmd->components);
            cmd->components = NULL;
            FREE(cmd->command);
            /* FREE(cmd->redirect_stdout); */
            /* FREE(cmd->redirect_stderr); */
            /* FREE(cmd->redirect_stdin); */
            FREE(cmd);
            cmd = n;
        }
    }
}

/**
 * int free_input(user_input_t *)
 *
 * @brief Cleans up a user_input, freeing all nested structs as necessary.
 *
 * @param ui  Pointer to the user_input to clean up.
 * @return  0 on success, -errno on failure.
 **/
int free_input(user_input_t *ui)
{
    debug("free_input() - ENTER (ui @ %p)", ui);
    int retval = 0;

    VALIDATE(ui,
            "can not clean a NULL user_input",
            -EINVAL,
            free_input_end);

    free_commands(ui->commands);
    ui->commands = NULL;
    FREE(ui->input);
    FREE(ui);

free_input_end:
    debug("free_input() - EXIT [%d]", retval);
    return retval;
}

/**
 * int insert_component(component_t *, command_t *)
 *
 * @brief  Inserts the given component at the end of the component list stored
 *         in cmd.
 *
 * @param comp  The component to insert into cmd
 * @param cmd  The command_t which contains the list to append comp to.
 *
 * @return  0 on success, -errno on failure.
 **/
static int insert_component(component_t *comp, command_t *cmd)
{
    debug("insert_component - ENTER [comp @ %p, cmd @ %p]", comp, cmd);
    int retval = 0;

    VALIDATE(comp,
            "comp must be non-NULL",
            -EINVAL,
            insert_component_end);
    VALIDATE(cmd,
            "cmd must be non-NULL",
            -EINVAL,
            insert_component_end);

    if(!cmd->components)
    {
        cmd->components = comp;
        goto insert_component_end;
    }

    component_t *c = cmd->components;
    while(c->next)
        c = c->next;
    c->next = comp;

insert_component_end:
    debug("insert_component() - EXIT [%d]", retval);
    return retval;
}

/**
 * int insert_command(command_t *, user_input_t *)
 *
 * @brief  Inserts the given command at the end of the command list stored in
 *         ui.
 *
 * @param cmd  The command to append to the end of the list
 * @param ui  The user_input_t which contains the list to append cmd to
 *
 * @return  0 on success, -errno on failure
 **/
static int insert_command(command_t *cmd, user_input_t *ui)
{
    debug("insert_command() - ENTER [cmd @ %p, ui @ %p]", cmd, ui);
    int retval = 0;

    VALIDATE(cmd,
            "cmd must be non-NULL",
            -EINVAL,
            insert_command_end);
    VALIDATE(ui,
            "ui must be non-NULL",
            -EINVAL,
            insert_command_end);

    if(!ui->commands)
    {
        ui->commands = cmd;
        goto insert_command_end;
    }

    command_t *c = ui->commands;
    while(c->next)
        c = c->next;
    c->next = cmd;

insert_command_end:
    debug("insert_command() - EXIT [%d]", retval);
    return retval;
}

/**
 * user_input_t* parse_input(char *)
 *
 * @brief Parses the given string, splitting it into commands and components.
 *
 * @param input  The char* string to parse
 * @return  A pointer to a user_input_t representing the parsed input
 **/
user_input_t* parse_input(char *input)
{
    debug("parse_input() - ENTER [input @ %p (\'%s\')]", input, input);
    user_input_t *retval = NULL;

    char *ctok = NULL;
    char *iptr = input, *cptr = NULL;
    char *savecptr = NULL;

    VALIDATE(input,
            "can not parse a NULL input",
            NULL,
            parse_input_end);

    /* create the user_input_t */
    if((retval = malloc(sizeof(user_input_t))) == NULL)
    {
        error("malloc() returned NULL: %s", strerror(errno));
        goto parse_input_fail;
    }
    memset(retval, 0, sizeof(user_input_t));

    /* save a copy of the original input */
    retval->input = strdup(input);

    /* create + insert the command_t into the user_input_t */
    command_t *newc = malloc(sizeof(command_t));
    if(!newc)
    {
        error("malloc() failed to allocate buffer for command_t");
        goto parse_input_fail;
    }
    memset(newc, 0, sizeof(command_t));
    newc->command = strdup(iptr);
    insert_command(newc, retval);

    /* add the component_t's to the command_t */
    for(cptr = iptr; ; cptr = NULL)
    {
        ctok = strtok_r(cptr, COMPONENT_DELIMS, &savecptr);
        if(!ctok)
        {
            debug("no token found for COMPONENT_DELIMS");
            goto parse_input_loop_end;
        }
        debug("c-token -> %s", ctok);

        component_t *newcomp = malloc(sizeof(component_t));
        if(!newcomp)
        {
            error("malloc() failed to allocate buffer for component_t");
            goto parse_input_fail;
        }
        memset(newcomp, 0, sizeof(component_t));

        newcomp->component = strdup(ctok);
        insert_component(newcomp, newc);
    }


parse_input_fail:
    free_input(retval);

parse_input_loop_end:

parse_input_end:
    debug("parse_input() - EXIT [%p]", retval);
    return retval;
}

