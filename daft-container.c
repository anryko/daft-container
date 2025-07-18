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
#include <sys/capability.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <syscall.h>
#include <unistd.h>

#include "utils.c"

static constexpr size_t STACK_SIZE = (size_t)(1024 * 1024); // 1MB
static bool do_verbose = false;

#define verbose(...)                                                           \
    do {                                                                       \
        if (do_verbose)                                                        \
            printf(__VA_ARGS__);                                               \
    } while (0)

noreturn static void command_line_help(char *pname) {
    fprintf(stderr, "For more information use: %s -h\n", pname);
    exit(EXIT_FAILURE);
}

noreturn static void command_line_usage(char *pname) {
    fprintf(stderr, "Usage: %s [options] cmd [arg...]\n", pname);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "    -h        Help\n");
    fprintf(stderr, "    -v        Verbose mode\n");
    fprintf(stderr, "    -r        New root directory (default: rootfs)\n");
    exit(EXIT_FAILURE);
}

struct mount {
    mode_t perms;
    const char *source;
    const char *target;
    const char *fstype;
    unsigned long flags;
    const void *data;
};

static const struct mount mounts_host[] = {
    {0755, "tmpfs", "/dev", "tmpfs", MS_NOSUID | MS_STRICTATIME,
     "mode=0755,size=65536k"},
    {0755, "devpts", "/dev/pts", "devpts", MS_NOSUID | MS_NOEXEC, NULL},
    {0755, "tmpfs", "/dev/shm", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777"}};

static const struct mount mounts_container[] = {
    {0555, "proc", "/proc", "proc", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL},
    {0555, "sysfs", "/sys", "sysfs",
     MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL}};

static const struct dev_mknod {
    const char *path;
    mode_t perms;
    unsigned int device[2]; // [major, minor] for makedev(major, minor)
} dev_mknods[] = {
    {"/dev/null", S_IFCHR | 0666, {1, 3}},
    {"/dev/zero", S_IFCHR | 0666, {1, 5}},
    {"/dev/full", S_IFCHR | 0666, {1, 7}},
    {"/dev/random", S_IFCHR | 0666, {1, 8}},
    {"/dev/urandom", S_IFCHR | 0666, {1, 9}},
    {"/dev/tty", S_IFCHR | 0666, {5, 0}},
    {"/dev/console", S_IFCHR | 0600, {5, 1}},
};

static const struct dev_symlink {
    const char *source;
    const char *target;
} dev_symlinks[] = {
    {"/proc/self/fd/0", "/dev/stdin"},
    {"/proc/self/fd/1", "/dev/stdout"},
    {"/proc/self/fd/2", "/dev/stderr"},
};

struct container {
    int pipe_fd[2];
    pid_t pid;
    char **command;
    char *hostname;
    int flags;
    bool do_root;
    char *new_root_path;
    char *put_root_path;
};

static void container_init(struct container *self, int argc, char *argv[]) {
    int opt;

    self->flags = CLONE_NEWUSER | CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS |
                  CLONE_NEWNET;
    self->hostname = "daft-container";
    self->do_root = true;
    self->new_root_path = "rootfs";
    self->put_root_path = ".old_root";

    if (pipe(self->pipe_fd) == -1) {
        die("pipe");
    }

    while ((opt = getopt(argc, argv, "+hvr:")) != -1) {
        switch (opt) {
        case 'v':
            do_verbose = true;
            break;
        case 'h':
            command_line_usage(argv[0]);
        case 'r':
            self->new_root_path = optarg;
            break;
        default:
            command_line_help(argv[0]);
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "No command provided!\n\n");
        command_line_help(argv[0]);
    }
    self->command = &argv[optind];
}

bool container_pivot_root(struct container self[static 1]) {
    int cwd_fd = -1;

    // Make / private so mount changes don't propagate to the parent namespace.
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == -1) {
        warn("mount('/', MS_PRIVATE)");
        goto error;
    }
    // Make sure new rootfs path is a valid mount point.
    if (mount(self->new_root_path, self->new_root_path, NULL, MS_BIND | MS_REC,
              NULL) == -1) {
        warn("mount('%s'): not a valid mountpoint", self->new_root_path);
        goto error;
    }
    // Save current working directory.
    cwd_fd = open(".", O_RDONLY | O_DIRECTORY);
    if (cwd_fd == -1) {
        warn("open() current directory");
        goto error;
    }
    // Switch to new root directory.
    if (chdir(self->new_root_path) == -1) {
        warn("chdir('%s')", self->new_root_path);
        goto error;
    }
    // Create directory to hold the old root.
    if (mkdir(self->put_root_path, 0700) == -1 && errno != EEXIST) {
        warn("mkdir('%s')", self->put_root_path);
        goto error;
    }
    // Pivot root to current direstory.
    if (syscall(SYS_pivot_root, ".", self->put_root_path) == -1) {
        warn("syscall pivot_root to '%s'", self->new_root_path);
        goto error;
    }
    // Switch to new root after pivot.
    if (chdir("/") == -1) {
        warn("chdir('/') after pivot root");
        goto error;
    }
    // Unmount and remove old root.
    if (umount2(self->put_root_path, MNT_DETACH) == -1) {
        warn("umount2('%s')", self->put_root_path);
        goto error;
    }
    if (rmdir(self->put_root_path) == -1) {
        warn("rmdir('%s')", self->put_root_path);
        goto error;
    }

    close(cwd_fd);
    return true;

error:
    umount2(self->put_root_path, MNT_DETACH);
    rmdir(self->put_root_path);

    // Restore original working dir on failure.
    if (cwd_fd >= 0) {
        fchdir(cwd_fd);
        close(cwd_fd);
    }

    return false;
}

static void container_host_mounts_create(struct container self[static 1]) {
    char mount_path[PATH_MAX];
    size_t num_mounts = sizeof(mounts_host) / sizeof(mounts_host[0]);
    for (size_t i = 0; i < num_mounts; i++) {
        snprintf(mount_path, PATH_MAX, "%s/%s", self->new_root_path,
                 mounts_host[i].target);
        if (mkdir(mount_path, mounts_host[i].perms) == -1 && errno != EEXIST) {
            warn("mkdir('%s')", mount_path);
        }
        if (mount(mounts_host[i].source, mount_path, mounts_host[i].fstype,
                  mounts_host[i].flags, mounts_host[i].data) == -1) {
            warn("mount('%s')", mount_path);
        }
    }
}

static void container_host_mounts_unmount(struct container self[static 1]) {
    char mount_path[PATH_MAX];
    size_t num_mounts = sizeof(mounts_host) / sizeof(mounts_host[0]);
    for (size_t i = num_mounts; i > 0; i--) {
        snprintf(mount_path, PATH_MAX, "%s%s", self->new_root_path,
                 mounts_host[i - 1].target);
        if (umount2(mount_path, MNT_DETACH) == -1) {
            warn("umount2('%s')", mount_path);
        }
    }
}

static void container_clone_mounts_create() {
    size_t num_mounts = sizeof(mounts_container) / sizeof(mounts_container[0]);
    for (size_t i = 0; i < num_mounts; i++) {
        if (mkdir(mounts_container[i].target, mounts_container[i].perms) ==
                -1 &&
            errno != EEXIST) {
            warn("mkdir('%s')", mounts_container[i].target);
        }
        if (mount(mounts_container[i].source, mounts_container[i].target,
                  mounts_container[i].fstype, mounts_container[i].flags,
                  mounts_container[i].data) == -1) {
            warn("mount('%s')", mounts_container[i].target);
        }
    }
}

static void container_host_devices_create(struct container self[static 1]) {
    char dev_path[PATH_MAX];
    size_t num_devs = sizeof(dev_mknods) / sizeof(dev_mknods[0]);
    for (size_t i = 0; i < num_devs; i++) {
        snprintf(dev_path, PATH_MAX, "%s%s", self->new_root_path,
                 dev_mknods[i].path);
        if (mknod(dev_path, dev_mknods[i].perms,
                  makedev(dev_mknods[i].device[0], dev_mknods[i].device[1])) ==
            -1) {
            warn("mknod('%s')", dev_path);
        }
    }
}

static void container_clone_symlinks_create() {
    size_t num_symlinks = sizeof(dev_symlinks) / sizeof(dev_symlinks[0]);
    for (size_t i = 0; i < num_symlinks; i++) {
        if (symlink(dev_symlinks[i].source, dev_symlinks[i].target) == -1 &&
            errno != EEXIST) {
            warn("symlink('%s')", dev_symlinks[i].target);
        }
    }
}

static int container_clone_exec(void *self) {
    struct container *c = (struct container *)self;

    close(c->pipe_fd[1]);

    if (read(c->pipe_fd[0], &(char){0}, 1) != 0) {
        die("failed to read from pipe");
    }
    close(c->pipe_fd[0]);

    verbose("set hostname: %s\n", c->hostname);
    if (sethostname(c->hostname, strlen(c->hostname)) == -1) {
        die("sethostname");
    }

    if (!container_pivot_root(c)) {
        die("container_pivot_root");
    }

    container_clone_mounts_create();
    container_clone_symlinks_create();

    verbose("executing command: %s\n", c->command[0]);
    execvp(c->command[0], c->command);
    die("execvp");
    return -1;
}

bool file_write(const char content[static 1], const char file_path[static 1]) {
    int fd = open(file_path, O_RDWR);
    if (fd == -1) {
        warn("open %s", file_path);
        return false;
    }

    size_t map_len = strlen(content);
    bool status = true;

    if (write(fd, content, map_len) != (ssize_t)map_len) {
        warn("write %s", file_path);
        status = false;
    }

    close(fd);
    return status;
}

static void container_uid_map(struct container self[static 1]) {
    if (!self->do_root) {
        verbose("skipping namespace root escalation\n");
        return;
    }

    constexpr size_t map_buf_size = 100;
    char map_buf[map_buf_size];
    char map_path[PATH_MAX];

    snprintf(map_path, PATH_MAX, "/proc/%d/uid_map", self->pid);
    snprintf(map_buf, map_buf_size, "0 %d 1", getuid());
    if (!file_write(map_buf, map_path)) {
        die("set uid_map");
    }

    snprintf(map_path, PATH_MAX, "/proc/%d/setgroups", self->pid);
    if (!file_write("deny", map_path)) {
        die("set setgroups deny");
    }

    snprintf(map_path, PATH_MAX, "/proc/%d/gid_map", self->pid);
    snprintf(map_buf, map_buf_size, "0 %d 1", getgid());
    if (!file_write(map_buf, map_path)) {
        die("set gid_map");
    }
}

static void container_clone(struct container self[static 1]) {
    char *stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (stack == MAP_FAILED) {
        die("mmap");
    }

    self->pid = clone(container_clone_exec, stack + STACK_SIZE,
                      self->flags | SIGCHLD, self);

    munmap(stack, STACK_SIZE);
}

static void container_wait(struct container self[static 1]) {
    close(self->pipe_fd[1]);

    if (waitpid(self->pid, NULL, 0) == -1) {
        die("waitpid");
    }
}

int main(int argc, char *argv[]) {
    struct container c;
    container_init(&c, argc, argv);

    container_host_mounts_create(&c);
    container_host_devices_create(&c);
    container_clone(&c);
    container_host_mounts_unmount(&c);
    container_uid_map(&c);
    container_wait(&c);

    exit(EXIT_SUCCESS);
}
