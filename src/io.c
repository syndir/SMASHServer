/**
 * @file io.c
 * @author Daniel Calabria
 **/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>
#include <unistd.h>
#include <signal.h>

#include "common.h"
#include "debug.h"
#include "io.h"

/**
 * int io_print_prompt(const char *)
 *
 * @brief  Outputs the given char* to stdout.
 *
 * @return  0 on success, -errno on failure
 **/
inline int io_print_prompt(const char *prompt)
{
    debug("io_print_prompt() - ENTER");
    int retval = 0;
    int r;

    VALIDATE(prompt,
            "prompt must be non-NULL",
            -EINVAL,
            io_print_prompt_end);

    if((r = dprintf(STDOUT_FILENO, "%s", prompt)) < 0)
    {
        retval = r;
        error("dprintf() failed: %s", strerror(retval));
        goto io_print_prompt_end;
    }

    VALIDATE((fflush(stdout) == 0),
            "fflush() failed to flush standard output stream",
            -errno,
            io_print_prompt_end);

io_print_prompt_end:
    debug("io_print_prompt() - EXIT [%d]", retval);
    return retval;
}

/**
 * char* io_readline()
 *
 * @brief  Reads a line of text from the file descriptor for standard input.
 *         This works by checking whether the input fd has pending input, then
 *         reading that input 1 byte at a time into the buffer. This is so that
 *         we don't block on read() calls.
 *
 * @return  A pointer to the read input, or NULL on EOF or error. The caller is
 *          responsible for freeing this memory.
 **/
char* io_readline()
{
    debug("io_readline() - ENTER");

    /* let buf be the base address of the buffer containing the input, and
     * let bufp be a pointer to the address within the buffer which we increase
     * as we continue to read input */
    char *buf = NULL, *bufp = NULL;
    char c;
    int bufsize = 64;
    int ready, old_errno;
    fd_set inputfd;

    /* allocate a 64-byte buffer for initial use. if we later determine that
     * this is not enough space, we can realloc() it to a larger size via
     * doubling. we could start this from 1, but that would be wasteful and
     * would incur a lot of doubling calls right away. 64 is a good start. */
    if((buf = malloc(sizeof(char) * bufsize)) == NULL)
    {
        error("malloc() failed to allocate %d bytes", bufsize);
        goto io_readline_end;
    }

    bufp = buf; /* we start storing @ start of buf */

    while(1)
    {
        /* make an fd_set containing just standard input.
         * this may be modified by the pselect call, so we set it each iteration
         * of the loop */
        FD_ZERO(&inputfd);
        FD_SET(STDIN_FILENO, &inputfd);

        /* wait on STDOUT_FILENO being ready for reading */
        ready = pselect(1, &inputfd, NULL, NULL, NULL, NULL);
        old_errno = errno;

        if(ready < 0)
        {
            /* pselect failed? */
            if(old_errno == EINTR)
            {
                debug("pselect() got EINTR. restarting");
                continue;
            }

            /* if pselect returned a negative value and it wasn't due to being
             * interrupted, something else horrible happened. */
            error("pselect() failed: %s", strerror(old_errno));
            goto io_readline_fail;
        }
        else if(ready == 0)
        {
            /* treat as newline/enter */
            debug("pselect() returned 0.");
            goto io_readline_end;
        }
        else
        {
            /* there is data available for reading. */

            /* read ONE character */
            if(read(STDIN_FILENO, &c, 1) != 1)
            {
                if(bufp - buf == 0)
                {
                    /* ctrl-d on empty line, EOF/EOT */
                    debug("read() failed to read 1 char and buffer has no contents");
                    goto io_readline_fail;
                }

                debug("read() failed to read 1 char and buffer has content");
                goto io_readline_end;
            }

            /* was a newline read? if so, we're done here. */
            if(c == '\n')
            {
                goto io_readline_end;
            }

            /* do we have enough room in buf to store this character? */
            if(bufp - buf >= bufsize - 1)
            {
                debug("not enough room in buf! resizing to %d bytes.",
                        bufsize << 1);
                bufsize = bufsize << 1; /* double size */
                int pos = bufp - buf;   /* offset that bufp is placed at
                                           relative to buf */
                char *bufn = realloc(buf, bufsize);
                if(!bufn)
                {
                    old_errno = errno;
                    error("realloc() failed: %s", strerror(old_errno));
                    goto io_readline_fail;
                }

                buf = bufn;
                bufp = buf + pos;
            }

            /* store the character and advance bufp offset */
            *bufp = c;
            bufp++;
        }
    }

io_readline_fail:
    FREE(buf);

io_readline_end:
    /* need to add null terminator to the input? */
    if(buf)
    {
        *bufp = '\0';
    }

    debug("io_readline() - EXIT [buf @ %p (\'%s\')]", buf, buf);
    return buf;
}
