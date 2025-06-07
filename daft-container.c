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
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syscall.h>
#include <unistd.h>

#include "ename.c.inc" // Defines ename and MAX_ENAME

static constexpr size_t ERROR_MSG_BUF_SIZE = 500;
static constexpr size_t STACK_SIZE = (size_t)(1024 * 1024); // 1MB

static int pipe_fd[2] = {};

struct config {
    char **command;
    char *hostname;
    int flags;
    bool do_verbose;
    bool do_root;
    char *new_root_path;
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
    fprintf(stderr, "    -r        New root directory\n");
    exit(EXIT_FAILURE);
}

static void config_init(struct config *cfg, int argc, char *argv[]) {
    int opt;

    cfg->flags = CLONE_NEWUSER | CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS;
    cfg->hostname = "daft-container";
    cfg->do_verbose = false;
    cfg->do_root = true;
    cfg->new_root_path = nullptr;

    while ((opt = getopt(argc, argv, "+hvr:")) != -1) {
        switch (opt) {
        case 'v':
            cfg->do_verbose = true;
            break;
        case 'h':
            command_line_usage(argv[0]);
        case 'r':
            cfg->new_root_path = optarg;
            break;
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

bool child_ns_mnt_root_pivot(const char new_root_path[static 1]) {
    const char *put_root_path = ".old_root";
    int cwd_fd = -1;
    bool is_proc_mounted = false;

    // Make / private so mount changes don't propagate to the parent namespace.
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == -1) {
        errMsg("mount('/', MS_PRIVATE)");
        goto error;
    }
    // Make sure new rootfs path is a valid mount point.
    if (mount(new_root_path, new_root_path, NULL, MS_BIND, NULL) == -1) {
        errMsg("'%s' not a valid mountpoint");
        goto error;
    }
    // Save current working directory.
    cwd_fd = open(".", O_RDONLY | O_DIRECTORY);
    if (cwd_fd == -1) {
        errMsg("open() current directory");
        goto error;
    }
    // Switch to new root directory.
    if (chdir(new_root_path) == -1) {
        errMsg("chdir('%s')", new_root_path);
        goto error;
    }
    // Create directory to hold the old root.
    if (mkdir(put_root_path, 0700) == -1 && errno != EEXIST) {
        errMsg("mkdir('%s')", put_root_path);
        goto error;
    }
    // Pivot root to current direstory.
    if (syscall(SYS_pivot_root, ".", put_root_path) == -1) {
        errMsg("syscall pivot_root to '%s'", new_root_path);
        goto error;
    }
    // Switch to new root after pivot.
    if (chdir("/") == -1) {
        errMsg("chdir('/') after pivot root");
        goto error;
    }
    // Ensure /proc exists at new root.
    if (mkdir("/proc", 0555) == -1 && errno != EEXIST) {
        errMsg("mkdir('/proc')");
        goto error;
    }
    // Mount fresh /proc
    if (mount("proc", "/proc", "proc", MS_NOSUID | MS_NOEXEC | MS_NODEV,
              NULL) == -1) {
        errMsg("mount('/proc')");
        goto error;
    }
    is_proc_mounted = true;
    // Unmount and remove old root.
    if (umount2(put_root_path, MNT_DETACH) == -1) {
        errMsg("umount2('%s')", put_root_path);
        goto error;
    }
    if (rmdir(put_root_path) == -1) {
        errMsg("rmdir('%s')", put_root_path);
        goto error;
    }

    close(cwd_fd);
    return true;

error:
    if (is_proc_mounted) {
        umount2("/proc", MNT_DETACH);
    }

    umount2(put_root_path, MNT_DETACH);
    rmdir(put_root_path);

    // Restore original working dir on failure.
    if (cwd_fd >= 0) {
        fchdir(cwd_fd);
        close(cwd_fd);
    }

    return false;
}

static int clone_exec(void *arg) {
    struct config *cfg = (struct config *)arg;

    close(pipe_fd[1]);

    char ch;
    if (read(pipe_fd[0], &ch, 1) != 0) {
        fprintf(stderr, "failed to read from pipe\n");
        exit(EXIT_FAILURE);
    }
    close(pipe_fd[0]);

    if (cfg->do_verbose) {
        printf("set hostname: %s\n", cfg->hostname);
    }
    if (sethostname(cfg->hostname, strlen(cfg->hostname)) == -1) {
        errExit("sethostname");
    }

    if (!child_ns_mnt_root_pivot(cfg->new_root_path)) {
        errExit("root pivot");
    }

    if (cfg->do_verbose) {
        printf("excuting command: %s\n", cfg->command[0]);
    }
    execvp(cfg->command[0], cfg->command);
    errExit("execvp");
}

bool file_write(const char content[static 1], const char file_path[static 1]) {
    int fd = open(file_path, O_RDWR);
    if (fd == -1) {
        errMsg("open %s", file_path);
        return false;
    }

    size_t map_len = strlen(content);
    bool status = true;

    if (write(fd, content, map_len) != (ssize_t)map_len) {
        errMsg("write %s", file_path);
        status = false;
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

    constexpr size_t map_buf_size = 100;
    char map_buf[map_buf_size];
    char map_path[PATH_MAX];

    snprintf(map_path, PATH_MAX, "/proc/%d/uid_map", child_pid);
    snprintf(map_buf, map_buf_size, "0 %d 1", getuid());
    if (!file_write(map_buf, map_path)) {
        errExit("set uid_map");
    }

    snprintf(map_path, PATH_MAX, "/proc/%d/setgroups", child_pid);
    if (!file_write("deny", map_path)) {
        errExit("set setgroups deny");
    }

    snprintf(map_path, PATH_MAX, "/proc/%d/gid_map", child_pid);
    snprintf(map_buf, map_buf_size, "0 %d 1", getgid());
    if (!file_write(map_buf, map_path)) {
        errExit("set gid_map");
    }
}

int main(int argc, char *argv[]) {
    struct config cfg;
    config_init(&cfg, argc, argv);

    if (pipe(pipe_fd) == -1) {
        errExit("pipe");
    }

    char *stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (stack == MAP_FAILED) {
        errExit("mmap");
    }

    pid_t child_pid =
        clone(clone_exec, stack + STACK_SIZE, cfg.flags | SIGCHLD, &cfg);

    munmap(stack, STACK_SIZE);

    child_ns_user_map_setup(&cfg, child_pid);

    close(pipe_fd[1]);

    if (waitpid(child_pid, NULL, 0) == -1) {
        errExit("waitpid");
    }

    exit(EXIT_SUCCESS);
}
