# daft-container

Small and stupid Linux container written in C.

## Motivation

Study Linux containers.

## Installation

```sh
git clone --depth=1 https://github.com/anryko/daft-container.git
cd daft-container
sudo apt install -y make
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
Usage: ./daft-container [options] cmd [arg...]
Options:
    -h        Help
    -v        Verbose mode
    -r        New root directory (default: rootfs)
$ sudo ./daft-container -v bash
set hostname: daft-container
excuting command: bash
root@daft-container:/# id
uid=0(root) gid=0(root) groups=0(root)
root@daft-container:/# hostname
daft-container
root@daft-container:/# ps -ef
UID          PID    PPID  C STIME TTY          TIME CMD
root           1       0  0 19:00 ?        00:00:00 bash
root           6       1  0 19:00 ?        00:00:00 ps -ef
root@daft-container:/# mount
/dev/sdc on / type ext4 (rw,relatime,discard,errors=remount-ro,data=ordered)
tmpfs on /dev type tmpfs (rw,nosuid,size=65536k,mode=755)
devpts on /dev/pts type devpts (rw,nosuid,noexec,relatime,mode=600,ptmxmode=000)
tmpfs on /dev/shm type tmpfs (rw,nosuid,nodev,relatime)
proc on /proc type proc (rw,nosuid,nodev,noexec,relatime)
sysfs on /sys type sysfs (ro,nosuid,nodev,noexec,relatime)
root@daft-container:/# ls -lah /dev/null
crw-r--r-- 1 root root 1, 3 Jun 15 19:00 /dev/null
```

## Main Resources

- [Namespaces in operation](https://lwn.net/Articles/531114/)
- [lucavallin/barco](https://github.com/lucavallin/barco)
