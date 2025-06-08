# daft-container

Small and stupid Linux cgroup2 container written in C.

## Motivation

Study Linux containers and cgroup2 namespaces.

## Installation

```sh
git clone --depth=1 https://github.com/anryko/daft-container.git
cd daft-container
sudo make setup_apt
make
```

## Setup

Setup `rootfs` directory to use as new containter root.

```sh
make rootfs
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
set hostname: daft-container
excuting command: bash
# id
uid=0(root) gid=0(root) groups=0(root),65534(nogroup)
# hostname
daft-container
# ps -ef
UID          PID    PPID  C STIME TTY          TIME CMD
root           1       0  0 20:39 ?        00:00:00 bash
root           6       1  0 20:39 ?        00:00:00 ps -ef
# mount
/dev/sdc on / type ext4 (rw,relatime,discard,errors=remount-ro,data=ordered)
proc on /proc type proc (rw,nosuid,nodev,noexec,relatime)
```
