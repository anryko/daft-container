#pragma once

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

[[gnu::format(printf, 4, 5)]]
static inline void errmsg(bool doexit, int excode, bool adderr, const char *fmt,
                          ...) {
    if (fmt != NULL) {
        va_list argp;
        va_start(argp, fmt);
        vfprintf(stderr, fmt, argp);
        va_end(argp);
        if (adderr) {
            fprintf(stderr, ": ");
        }
    }
    if (adderr) {
        fprintf(stderr, "%m");
    }
    fprintf(stderr, "\n");
    if (doexit) {
        exit(excode);
    }
}

#define die(...) errmsg(true, EXIT_FAILURE, true, __VA_ARGS__);
#define warn(...) errmsg(false, 0, true, __VA_ARGS__)
