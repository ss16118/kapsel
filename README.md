# Kapsel

**Kapsel** (German for capsule) is a simplified Linux container implemented from scratch in C++. The code and implementation are primarily based on the following resources:

- [Go: Building a container from scratch in Go](https://www.youtube.com/watch?v=8fi7uSYlOdc) [video]
- [Python: A workshop on Linux containers: Rebuild Docker from Scratch](https://github.com/Fewbytes/rubber-docker)
- [Ccont: A Linux Container Implemented in C](https://github.com/7aske/ccont)

I have found the first two links in [this repository](https://github.com/danistefanovic/build-your-own-x), which contains numerous tutorials. It is definitely worth looking into for anyone who wants to implement their own tools from ground up.

I started this project mainly to practice C++ and familiarize myself with the basic principles behind Docker containers. Since I am new to both C++ and Linux, the code is far from being perfect. As a disclaimer, please be aware that this is only a toy project and the container that I implemented is incomplete. Therefore, I would highly recommended not to use it in a production environment of any kind.

Quick Start
====================

If you want to try Kapsel yourself, follow the steps below:
- Clone this repository and make sure that cmake (> 3.19) is installed.
- Enter the following commands in the terminal:
```console
$ cd kapsel && mkdir build && cd build
$ cmake .. && make -j
```
- To start the most basic ubuntu container in an interactive bash shell, enter the following command. Note that you have to execute the command with `sudo`, otherwise container initialization will fail.
```console
$ sudo ./kapsel run /bin/bash
Starting container vllrscbn4aca with pid 9467
Container vllrscbn4aca initialized
Executing command: /bin/bash 
root@vllrscbn4aca:/# id
uid=0(root) gid=0(root) groups=0(root)
root@vllrscbn4aca:/# ps
    PID TTY          TIME CMD
      1 ?        00:00:00 kapsel
      2 ?        00:00:00 sh
      3 ?        00:00:00 bash
      6 ?        00:00:00 ps
root@vllrscbn4aca:/# 
```
Usage
====================

```console
./kapsel [OPTION...] [cmd-type] [args]
```

| Options                  | Description                                                                                                                                                                                                                                                                               | Default |
|--------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|---------|
| -t, --rootfs arg         | The root file system for the container. Current options are {'ubuntu', 'alpine', 'arch', 'centos'}.                                                                                                                                                                                       | ubuntu  |
| -i, --container-id arg   | Specify the ID that of the container to run. If the ID points to a image which has been built, run the image.                                                                                                                                                                             |         |
| -r, --root-dir arg       | The directory where all Kapsel related files will be stored.                                                                                                                                                                                                                              | ../res  |
| -b, --build              | Build an image of the container after exiting.                                                                                                                                                                                                                                            | false   |
| -p, --process-number arg | The maximum number of processes can be created in the container. Use 'max' to remove limit                                                                                                                                                                                                | 20      |
| -c, --cpu-share arg      | The relative share of CPU time available for the container.                                                                                                                                                                                                                               | 512     |
| -m, --memory arg         | The user memory limit of the container. Use -1 to remove limit.                                                                                                                                                                                                                           | 256m    |
| -s, --memory-swap arg    | The maximum amount for the sum of memory and swap usage in the container. Use -1 to remove limit.                                                                                                                                                                                         | 512m    |
| -l, --logging            | Enable logging to log file <root-dir>/logs/<container-id>.log.                                                                                                                                                                                                                            |         |
| --cmd-type arg           | Type of actions to perform. Available options are {'run', 'list', 'delete'}.<br/> run   : executes the preceding command inside a container.<br/>list  : lists the container images which have been built.<br/> delete: remove the container images which have the preceding list of IDs. |         |
| --args arg               | The arguments that will passed to command type <cmd-type>. For instance, when <cmd-type> is 'run', args will function as the command to be executed in the container; when <cmd-type> is 'delete', args will be a list of image IDs of the images to be deleted.                          | ""      |


Examples
====================

```console
$ sudo ./kapsel -t alpine run /bin/sh
```
Start a alpine container in an interactive shell. The container will be removed after use.

```console
$ sudo ./kapsel -b -i container run /bin/bash
```
Start a ubuntu container named **container** whose file system will be compressed and stored after exiting from the shell.

```console
$ sudo ./kapsel ls
#              Image ID        Size                   Last Modified
0             container      26.7MB        Sun Aug 29 20:37:58 2021
```
List the container images that have been built.

```console
$ sudo ./kapsel rm container
Removed image with ID container
```
Removes the image with the ID **container**.

Features
====================

Kapsel containers support the following features:
- Isolation in Linux namespaces:
  - UTS
  - PID
  - MOUNT
  - NETWORK
- Control over CGroup resources: 
  - pids.max
  - memory.limit_in_bytes
  - memory.memsw.limit_in_bytes
  - cpu.shares
- Filesystem isolation with `chroot` and `pivot_root`.
- Access to the Internet.
- Being able to run, save and delete a stored container image as a tar archive.

Known Issues
====================
- Sometimes, container network initialization will be stuck possibly due to a bug in the use of the POSIX semaphores. Restarting your computer will likely solve the issue.