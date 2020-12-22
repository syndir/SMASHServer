/**
 * @file debug.h
 * @author Daniel Calabria
 * @id 103406017
 *
 * Some helpful macros for printing debug and error information.
 **/

#ifndef DEBUG_H
#define DEBUG_H

/**
 * Debug information printing macro. Only functional if compiled with DEBUG
 * defined.
 **/
#define debug(str, ...) \
    if(debug_enabled) \
    { \
    fprintf(stderr, "DEBUG [%s:%d]: " str "\n", \
            __FILE__, \
            __LINE__, \
            ##__VA_ARGS__); \
    }

/**
 * ERROR macro -> prints an error message to stderr, regardless of debug level.
 **/
#define error(str, ...) \
    fprintf(stderr, "ERROR: " str "\n", \
            ##__VA_ARGS__)

#endif // DEBUG_H
