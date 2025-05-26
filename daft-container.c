#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>
#include <unistd.h>

#include "ename.c.inc" // Defines ename and MAX_ENAME

#define ERROR_MSG_BUF_SIZE 500

noreturn static void terminate(bool useExit3) {
    char *s = getenv("EF_DUMPCORE");

    if (s != nullptr && *s != '\0') {
        // Terminate with coredump if EF_DUMPCORE ENV variable is set
        abort();
    } else if (useExit3) {
        // Flushes output and exit
        exit(EXIT_FAILURE);
    } else {
        // Skip all cleanup and immediately exit
        _exit(EXIT_FAILURE);
    }
}

static void outputError(bool useErr, int err, bool flushStdout,
                        const char *format, va_list ap) {
    char buf[ERROR_MSG_BUF_SIZE];
    char userMsg[ERROR_MSG_BUF_SIZE];
    char errText[ERROR_MSG_BUF_SIZE];

    vsnprintf(userMsg, ERROR_MSG_BUF_SIZE, format, ap);

    if (useErr) {
        snprintf(errText, ERROR_MSG_BUF_SIZE, " [%s %s]",
                 (err > 0 && err <= MAX_ENAME) ? ename[err] : "UNKNOWN",
                 strerror(err));
    } else {
        snprintf(errText, ERROR_MSG_BUF_SIZE, ":");
    }

    snprintf(buf, ERROR_MSG_BUF_SIZE, "ERROR%s %s\n", errText, userMsg);

    if (flushStdout) {
        fflush(stdout);
    }
    fputs(buf, stderr);
    fflush(stderr);
}

void errMsg(const char *format, ...) {
    va_list argList;
    int savedErrno;

    savedErrno = errno;

    va_start(argList, format);
    outputError(true, errno, true, format, argList);
    va_end(argList);

    errno = savedErrno;
}

void errExit(const char *format, ...) {
    va_list argList;

    va_start(argList, format);
    outputError(true, errno, true, format, argList);
    va_end(argList);

    terminate(true);
}

int main(int argc, char *argv[]) {
    errno = 1;
    errMsg("error");
    errExit("exit");
}
