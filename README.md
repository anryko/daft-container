# daft-container

Small and stupid Linux cgroup2 container written in C.

## Motivation

Study Linux containers and cgroup2 namespaces.

## Installation

```sh
git clone --depth=1 https://github.com/anryko/daft-container.git
cd daft-container
make
```

## Usage

```sh
$ ./daft-container -h
Usage: ./daft-container [optioins] cmd [arg...]
Options:
    -h        Help
    -v        Verbose mode
    -r        New root directory
$ id
uid=1000(user) gid=1000(user) groups=1000(user),100(users)
$ ./daft-container -v -r rootfs bash
child[3480]: set hostname: daft-container
child[3480]: excuting command: bash
# id
uid=0(root) gid=0(root) groups=0(root),65534(nogroup)
# hostname
daft-container
# ps -ef
PID   USER     TIME  COMMAND
    1 root      0:00 bash
    2 root      0:00 ps -ef
# mount
/dev/sdc on / type ext4 (rw,relatime,discard,errors=remount-ro,data=ordered)
proc on /proc type proc (rw,nosuid,nodev,noexec,relatime)
```
