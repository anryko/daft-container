#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <sched.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ename.c.inc" // Defines ename and MAX_ENAME

#define ERROR_MSG_BUF_SIZE 500
#define STACK_SIZE (1024 * 1024)

static int pipe_fd[2];

struct config {
    char **command;
    int flags;
    bool do_verbose;
    bool do_root;
};

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

noreturn void errExit(const char *format, ...) {
    va_list argList;

    va_start(argList, format);
    outputError(true, errno, true, format, argList);
    va_end(argList);

    terminate(true);
}

noreturn static void command_line_help(char *pname) {
    fprintf(stderr, "For more information use: %s -h\n", pname);
    exit(EXIT_FAILURE);
}

noreturn static void command_line_usage(char *pname) {
    fprintf(stderr, "Usage: %s [optioins] cmd [arg...]\n", pname);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "    -h        Help\n");
    fprintf(stderr, "    -v        Verbose mode\n");
    exit(EXIT_FAILURE);
}

static void config_init(struct config *cfg, int argc, char *argv[]) {
    int opt;

    cfg->flags = CLONE_NEWUSER;
    cfg->do_verbose = false;
    cfg->do_root = true;

    while ((opt = getopt(argc, argv, "+hv")) != -1) {
        switch (opt) {
        case 'v':
            cfg->do_verbose = true;
            break;
        case 'h':
            command_line_usage(argv[0]);
        default:
            command_line_help(argv[0]);
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "No command provided!\n\n");
        command_line_help(argv[0]);
    }

    cfg->command = &argv[optind];
}

static int clone_exec(void *arg) {
    struct config *cfg = (struct config *)arg;

    close(pipe_fd[1]);

    char ch;
    if (read(pipe_fd[0], &ch, 1) != 0) {
        fprintf(stderr, "child[%d]: failed to read from pipe\n", getpid());
        exit(EXIT_FAILURE);
    }
    close(pipe_fd[0]);

    if (cfg->do_verbose) {
        printf("child[%d]: excuting command: %s\n", getpid(), cfg->command[0]);
    }
    execvp(cfg->command[0], cfg->command);
    errExit("execvp");
}

int file_write(char *content, char *file_path) {
    int fd = open(file_path, O_RDWR);
    if (fd == -1) {
        errMsg("open %s", file_path);
        return -1;
    }

    size_t map_len = strlen(content);
    int status = 0;

    if (write(fd, content, map_len) != (ssize_t)map_len) {
        errMsg("write %s", file_path);
        status = -1;
    }

    close(fd);
    return status;
}

static void child_ns_user_map_setup(struct config *cfg, pid_t child_pid) {
    if (!cfg->do_root) {
        if (cfg->do_verbose) {
            fprintf(stderr, "child[%d]: skipping namespace root escalation",
                    getpid());
        }
        return;
    }
    const int map_buf_size = 100;
    char map_buf[map_buf_size];
    char map_path[PATH_MAX];

    snprintf(map_path, PATH_MAX, "/proc/%d/uid_map", child_pid);
    snprintf(map_buf, map_buf_size, "0 %d 1", getuid());
    if (file_write(map_buf, map_path) == -1) {
        errExit("set uid_map");
    }

    snprintf(map_path, PATH_MAX, "/proc/%d/setgroups", child_pid);
    if (file_write("deny", map_path) == -1) {
        errExit("set setgroups deny");
    }

    snprintf(map_path, PATH_MAX, "/proc/%d/gid_map", child_pid);
    snprintf(map_buf, map_buf_size, "0 %d 1", getgid());
    if (file_write(map_buf, map_path) == -1) {
        errExit("set gid_map");
    }
}

int main(int argc, char *argv[]) {
    struct config cfg;
    config_init(&cfg, argc, argv);

    if (pipe(pipe_fd) == -1) {
        errExit("pipe");
    }

    char *stack = mmap(NULL, (size_t)STACK_SIZE, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (stack == MAP_FAILED) {
        errExit("mmap");
    }

    pid_t child_pid = clone(clone_exec, stack + (ptrdiff_t)STACK_SIZE,
                            cfg.flags | SIGCHLD, &cfg);

    munmap(stack, (size_t)STACK_SIZE);

    child_ns_user_map_setup(&cfg, child_pid);

    close(pipe_fd[1]);

    if (waitpid(child_pid, NULL, 0) == -1) {
        errExit("waitpid");
    }

    exit(EXIT_SUCCESS);
}
