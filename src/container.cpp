//
// Created by siyuan on 18/07/2021.
//
#include <string>
#include <iostream>
#include <sys/mount.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <filesystem>
#include <unistd.h>
#include <cstring>
#include <sched.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <algorithm>
#include <thread>
#include <fcntl.h>
#include <loguru/loguru.hpp>

#include "constants.h"
#include "container.h"
#include "utils.h"

std::map<std::string, std::string> stringToDownloadUrl = {
        { "ubuntu", UBUNTU_IMAGE_URL },
        { "alpine", ALPINE_IMAGE_URL },
        { "centos", CENTOS_IMAGE_URL },
        { "arch", ARCH_IMAGE_URL }
};

/**
 * A struct representing a linux device file.
 */
struct Device
{
    std::string name;
    int type;
    int major;
    int minor;
};


/**
 * A wrapper around the pivot_root() function.
 * More details on the man page of pivot_root().
 */
long pivot_root(const char* newRoot, const char* putOld)
{
    return syscall(SYS_pivot_root, newRoot, putOld);
}

/**
 * Allocates memory for a Container struct and initializes it with the given parameters.
 *
 * @param distroName: name of the linux distro to be used as the root file system.
 * @param containerId: a string that uniquely identifies a container.
 * @param rootDir: the root directory of the container
 * @param command: the command to be executed in the container.
 * @return the created Container struct.
 */
Container* createContainer(
        std::string& distroName,
        std::string& containerId,
        std::string& rootDir,
        std::string& command,
        ResourceLimits* resourceLimits,
        bool buildImage)
{

    auto* container = new Container;
    container->distroName = distroName;
    container->id = containerId;
    container->rootDir = rootDir;
    container->command = command;
    container->buildImage = buildImage;

    // Retrieves the name of the current user
    char buffer[128];
    getlogin_r(buffer, 128);
    container->currentUser = std::string(buffer);
    container->resourceLimits = resourceLimits;

    // Initializes network semaphores
    // Uses sem_open to create the semaphores since they will be shared among processes
    container->networkNsSemaphore = sem_open(NETWORK_NS_SEM_NAME, O_CREAT | O_EXCL, 0600, 0);
    container->networkInitSemaphore = sem_open(NETWORK_INIT_SEM_NAME, O_CREAT | O_EXCL, 0600, 0);

    return container;
}

/**
 * Sets up the root file system of the container as per the specified linux distro and the
 * root directory (the lower-dir in an overlay fs). This function performs the following actions:
 *
 * 1. Checks if the cache directory exists in the root directory. Creates it if it does not.
 * 2. Checks if the rootfs archive for the specified distro exists. Fetches it from the pre-defined
 * download URL if it is not present in the cache directory.
 * 3. If 'buildImage' is false and the 'rootfs' directory does not exist for the specified distro,
 * creates said directory and unpacks the file system of the distro in it. If 'buildImage' is true,
 * unpacks the file system into the container's directory in <root-dir>/containers/<container-id>/.
 * Implementation based on https://github.com/Fewbytes/rubber-docker/blob/master/levels/10_setuid/rd.py
 */
void setUpContainerImage(Container* container)
{
    std::string rootDir = container->rootDir;
    std::string distroName = container->distroName;

    std::string cacheDir = rootDir + "/cache/" + distroName;

    // Creates a cache directory to store the downloaded file systems if it does not exist
    if (!std::filesystem::exists(cacheDir))
    {
        if (std::filesystem::create_directories(cacheDir))
            std::cout << "Create cache directory " + cacheDir << ": SUCCESS" << std::endl;
        else
            throw std::runtime_error("Create cache directory " + cacheDir + ": FAILED");
    }


    std::string downloadUrl = stringToDownloadUrl[container->distroName];

    std::string baseArchiveName(basename(downloadUrl.c_str()));
    std::string rootfsArchive = cacheDir + "/" + baseArchiveName;

    char buffer[256];

    // Downloads the file system archive if it is not present in the cache directory
    if (!std::filesystem::exists(rootfsArchive))
    {
        LOG_F(INFO, "Rootfs for %s does not exist", container->distroName.c_str());
        LOG_F(INFO, "Downloading %s from %s", baseArchiveName.c_str(), downloadUrl.c_str());
        sprintf(buffer, "wget -O %s %s -q --show-progress", rootfsArchive.c_str(), downloadUrl.c_str());
        if (system(buffer) == -1)
            throw std::runtime_error("Download rootfs archive for " + distroName + ": FAILED");
    }

    // Extracts the archive to the location depending on the value of 'buildImage'
    // If 'buildImage' is true, extracts the file system to the container's directory.
    // Otherwise, extracts the files to distro's directory.
    std::string rootfsDestDir;
    if (container->buildImage)
    {
        container->dir = container->rootDir + "/containers/" + container->id;
        container->rootfs = container->dir + "/rootfs";
        rootfsDestDir = container->rootfs;
    }
    else
    {
        rootfsDestDir = cacheDir + "/rootfs";
    }

    // Creates the destination folder, and extracts the files
    if (!std::filesystem::exists(rootfsDestDir))
    {
        if (std::filesystem::create_directories(rootfsDestDir))
            LOG_F(INFO, "Create directory %s: SUCCESS", rootfsDestDir.c_str());
        else
            throw std::runtime_error("Create directory " + rootfsDestDir + ": FAILED");

        LOG_F(INFO, "Extracting rootfs from %s to %s", rootfsArchive.c_str(), rootfsDestDir.c_str());
        sprintf(buffer, "tar xvf %s -C %s > /dev/null", rootfsArchive.c_str(), rootfsDestDir.c_str());
        if (system(buffer) == -1)
            throw std::runtime_error("Extract " + rootfsArchive + " to " + rootfsDestDir + ": FAILED");
    }
}

/**
 * Sets up the required folders in the container's directory (the upper-dir, merged-dir and
 * work-dir in an overlay fs). Performs the following actions:
 * 1. Creates the container's directory in 'rootDir', if it does not exist.
 * 2. Creates the folders needed for the overlay fs. Details of an overlay fs can be
 * found here: https://wiki.archlinux.org/title/Overlay_filesystem.
 * Implementation based on https://github.com/Fewbytes/rubber-docker/blob/master/levels/10_setuid/rd.py
 *
 * @param container the container whose file system will be created.
 */
void setUpContainerOverlayFs(Container* container)
{
    char buffer[256];
    container->dir = container->rootDir + "/containers/" + container->id;
    std::string containerDir = container->dir;

    // Creates the container directory and extracts the specified root file system
    if (!std::filesystem::exists(containerDir))
    {
        if (std::filesystem::create_directories(containerDir))
            LOG_F(INFO, "Create container directory %s: SUCCESS", containerDir.c_str());
        else
            throw std::runtime_error("Create container directory " + containerDir + ": FAILED");

        // Creates copy-on-write (upper), and work directory for the overlay fs
        std::string upperDir = containerDir + "/copy-on-write";
        std::string workDir = containerDir + "/work";
        container->rootfs = containerDir + "/rootfs";
        LOG_F(INFO, "Setting up overlay fs directories in %s", containerDir.c_str());
        std::vector<std::string> dirs = { upperDir, workDir, container->rootfs };
        for (const auto& dir : dirs)
        {
            if (!std::filesystem::create_directories(dir))
                throw std::runtime_error("Create " + dir + ": FAILED");
        }
        LOG_F(INFO, "Set up overlay fs directories in %s: SUCCESS", containerDir.c_str());
    }
}


/**
 * A helper function which retrieves the next available IP address
 * for the container. Returns an empty string if the operation fails.
 */
std::string getContainerVEthIp()
{
    try
    {
        // Counts the number of containers that already exist by
        // checking the number of connections to the bridge
        int connectionCount =
                std::stoi(systemWithOutput("brctl show " + BRIDGE_NAME + " | grep veth1 | wc -l")) + 1;
        return getNextIp(BRIDGE_IP, connectionCount);
    }
    catch (std::exception& ex)
    {
        return std::string();
    }
}


/**
 * Initializes the networking environment for the given container by
 * performing the following actions:
 * 1. If not already present, creates a new network bridge interface named 'kapsel',
 * sets its status to 'up', and assigns it an IPv4 address.
 * 2. Adds the container's id as a new network namespace by calling 'ip netns add'.
 * 3. Creates a new pair of veths.
 * 4. Places one end of the veth pair to the new network space.
 * 5. Moves the other end of the veth pair to the bridge.
 * 6. Assigns an IPv4 address to veth0.
 * 7. Ups veth0.
 * 8. Ups container localhost.
 * 9. Ups veth1.
 * 10. Adds the bridge's IP as the default gateway for the container network.
 * References:
 * - https://dev.to/polarbit/how-docker-container-networking-works-mimic-it-using-linux-network-namespaces-9mj
 */
void initializeContainerNetwork(Container* container)
{
    LOG_F(INFO, "Initializing container network environment");
    try
    {
        auto* networkNsSemaphore = sem_open(NETWORK_NS_SEM_NAME, 0);
        if (networkNsSemaphore == SEM_FAILED)
            throw std::runtime_error("sem_open failed for " + std::string(NETWORK_NS_SEM_NAME));
        auto* networkInitSemaphore = sem_open(NETWORK_INIT_SEM_NAME, 0);
        if (networkInitSemaphore == SEM_FAILED)
            throw std::runtime_error("sem_open failed for " + std::string(NETWORK_INIT_SEM_NAME));

        std::string newNetworkNs = container->id;
        // Takes the first 9 characters of the container's ID as the suffix
        // for the names of the veth pair since a valid interface name contains
        // less than 16 characters
        std::string suffix = container->id.substr(0, 9);
        container->vEthPair = std::make_pair("veth0@" + suffix, "veth1@" + suffix);
        auto vEthPair = container->vEthPair;

        std::string containerIp = getContainerVEthIp();
        if (containerIp.empty())
            throw std::runtime_error("Obtain IPv4 address for container: FAILED");
        LOG_F(INFO, "Container IP: %s", containerIp.c_str());

        // Adds the container's ID as a new network namespace
        if (system(("ip netns add " + newNetworkNs).c_str()) == -1)
            throw std::runtime_error("Create namespace " + newNetworkNs + ": FAILED");
        // Unblocks the thread that is attempting to mount /var/run/netns/<new-network-ns>
        sem_post(networkNsSemaphore);
//        sem_post(sem_open(NETWORK_NS_SEM_NAME, 0));
        // Blocks itself until the container's registers its namespace in setUpNetworkNamespace()
        sem_wait(networkInitSemaphore);
//        sem_wait(sem_open(NETWORK_INIT_SEM_NAME, 0));
        std::vector<std::string> commands = {
                // Creates a veth pair
                "ip link add " +  vEthPair.first + " type veth peer name " + vEthPair.second,
                // Moves one end of the veth pair to the new namespace
                "ip link set " + vEthPair.first + " netns " + newNetworkNs,
                // Moves the other end of the veth pair to the bridge
                "ip link set " + vEthPair.second + " master " + BRIDGE_NAME,
                // Assigns an IPv4 address to the first interface in the veth pair
                "ip netns exec " + newNetworkNs + " ip addr add " + containerIp + "/24 dev " + vEthPair.first,
                // Ups veth0
                "ip netns exec " + newNetworkNs + " ip link set " + vEthPair.first + " up",
                // Ups the container's localhost
                "ip netns exec " + newNetworkNs + " ip link set lo up",
                // Ups veth1
                "ip link set " + vEthPair.second + " up",
                // Adds the bridge as the default gateway
                "ip netns exec " + newNetworkNs + " ip route add default via " + BRIDGE_IP
        };

        // Checks if the bridge 'kapsel' already exists
        std::string bridgeFilePath = "/sys/class/net/" + BRIDGE_NAME + "/bridge";
        if (!std::filesystem::exists(bridgeFilePath))
        {
            // Changes the policy on IP table
            // FIXME There might exist another solution
            // From: https://serverfault.com/questions/694889/cannot-ping-linux-network-namespace-within-the-same-subnet
            commands.insert(commands.begin(), "iptables --policy FORWARD ACCEPT");
            // Enable sending requests and getting responses to/from internet
            // From: https://dev.to/polarbit/how-docker-container-networking-works-mimic-it-using-linux-network-namespaces-9mj
            std::string broadcast = BRIDGE_IP;
            broadcast.pop_back();
            broadcast.push_back('1');
            std::string command = "iptables -t nat -A POSTROUTING -s " + broadcast + "/24 ! -o " + BRIDGE_NAME + " -j MASQUERADE";
            commands.insert(commands.begin(), command);
            // Assigns an IP address to the bridge
            commands.insert(commands.begin(), "ip addr add " + BRIDGE_IP + "/24 brd + dev " + BRIDGE_NAME);
            // Ups the bridge interface
            commands.insert(commands.begin(), "ip link set " + BRIDGE_NAME + " up");
            // Creates a network bridge
            commands.insert(commands.begin(), "ip link add name " + BRIDGE_NAME + " type bridge");
        }

        for (const auto& command : commands)
        {
            if (system(command.c_str()) == -1)
                throw std::runtime_error("Execute command " + command + ": FAILED");
        }

        sem_post(networkInitSemaphore);
//        sem_post(sem_open(NETWORK_NS_SEM_NAME, 0));
        LOG_F(INFO, "Initialize container network environment: SUCCESS");
    }
    catch (std::exception& ex)
    {
        LOG_F(ERROR, "Initialize container network environment: FAILED");
        LOG_F(ERROR, "%s", ex.what());
    }
}



/**
 * Prepares and sets up the environment required for the container to
 * run correctly. Performs the following actions:
 * 1. Downloads and extracts the rootfs to a specified folder.
 * 2. Initializes the overlay fs folders in the container's directory
 * if 'buildImage' is false.
 * 3. Initializes the networking environment for the given container.
 *
 * @param container a struct representing the container whose file system will initialized.
 * @return true if set up succeeds, false otherwise.
 */
bool setUpContainer(Container* container)
{
    LOG_F(INFO, "Set up container %s", container->id.c_str());
    try
    {
        setUpContainerImage(container);
        if (!container->buildImage)
            setUpContainerOverlayFs(container);

        // Makes the current user the owner of the container directory
        char buffer[256];
        std::string currentUser = container->currentUser;
        std::string containerDir = container->dir;
        sprintf(buffer, "chown -R %s %s", currentUser.c_str(), containerDir.c_str());
        if (system(buffer) == -1)
            throw std::runtime_error("Make" + currentUser + " the owner of " + containerDir + ": FAILED");
        else
            LOG_F(INFO, "Setting the owner of %s to %s", containerDir.c_str(), currentUser.c_str());
        LOG_F(INFO, "Set up container %s: SUCCESS", container->id.c_str());
    }
    catch (std::exception& ex)
    {
        LOG_F(ERROR, "Set up container %s: FAILED", container->id.c_str());
        LOG_F(ERROR, "%s", ex.what());
        return false;
    }
    // Spawns a worker thread to initialize the network environment for the container
    std::thread networkWorker(initializeContainerNetwork, container);
    networkWorker.detach();
    return true;
}

/**
 * Creates the memory stack needed by the cloned process.
 * Implementation based on:
 * https://cesarvr.github.io/post/2018-05-22-create-containers/
 *
 * @param stackSize the stackSize of the stack to be initialized, defaults to 65K.
 * @return a pointer to the bottom of the memory stack.
 */
char* createStack(const uint32_t stackSize = 65536)
{
    auto* stack = new (std::nothrow) char[stackSize];
    if (stack == nullptr)
        throw std::runtime_error("Allocate memory: FAILED");
    return stack + stackSize;
}


/**
 * Limits the number of process the container can create by
 * creating a directory in cgroup's 'pids' folder and writing
 * the limit to the corresponding files.
 *
 * Implementation from:
 * https://github.com/cesarvr/container/blob/master/container.cc
 */
void setUpProcessLimit(Container* container)
{
    LOG_F(INFO, "Setting up pid limits");
    std::string pidDir = CGROUP_FOLDER + "/pids/" + container->id;
    if (!std::filesystem::create_directories(pidDir))
        throw std::runtime_error("Create directory " + pidDir + ": FAILED");


    if (!appendToFile(pidDir + "/pids.max", container->resourceLimits->processNumber))
        throw std::runtime_error("Write to file 'pids.max': FAILED");

    if (!appendToFile(pidDir + "/notify_on_release", "1"))
        throw std::runtime_error("Write to file 'notify_on_release': FAILED");

    if (!appendToFile(pidDir + "/cgroup.procs", std::to_string(container->pid)))
        throw std::runtime_error("Write to 'cgroup.procs': FAILED");

    LOG_F(INFO, "Set up pid limits: SUCCESS");
}

/**
 * Limits the memory usage of the container by creating a
 * directory in the cgroup's memory folder and moving the
 * process to the 'tasks' file and then writing the limits
 * to the following files:
 * - memory.limit_in_bytes
 * - memory.memsw.limit_in_bytes
 * Can be tested with:
 * dd if=/dev/zero of=output.dat  bs=24M  count=1
 * To read more about the memory cgroup and subsystem, please check
 * out the following link:
 * https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/6/html/resource_management_guide/sec-memory
 *
 * Implementation from:
 * https://github.com/Fewbytes/rubber-docker/blob/master/levels/10_setuid/rd.py
 */
void setUpMemoryLimit(Container* container)
{
    LOG_F(INFO, "Setting up memory limits");
    std::string memoryDir = CGROUP_FOLDER + "/memory/" + container->id;
    if (!std::filesystem::create_directories(memoryDir))
        throw std::runtime_error("Create directory " + memoryDir + ": FAILED");

    // Adds the container's process to the 'tasks' file
    if (!appendToFile(memoryDir + "/tasks", std::to_string(container->pid)))
        throw std::runtime_error("Write to file 'tasks': FAILED");

    if (!appendToFile(memoryDir + "/memory.limit_in_bytes", container->resourceLimits->memory))
        throw std::runtime_error("Write to file 'memory.limit_in_bytes': FAILED");

    if (!appendToFile(memoryDir + "/memory.memsw.limit_in_bytes", container->resourceLimits->swapMemory))
        throw std::runtime_error("Write to file 'memory.memsw.limit_in_bytes': FAILED");

    LOG_F(INFO, "Set up memory limits: SUCCESS");
}

/**
 * Limits the CPU usage of the processes inside the container. Imposes a
 * soft limit by adding the container's process to the 'tasks' file and
 * writing the allocated share to 'cpu.shares'.
 * To understand relationship between the allocated shares and the actual
 * CPU utilization rate, the following links can be used as references:
 * - https://oakbytes.wordpress.com/2012/09/02/cgroup-cpu-allocation-cpu-shares-examples/
 * - https://www.batey.info/cgroup-cpu-shares-for-docker.html
 * - https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/6/html/resource_management_guide/sec-cpu
 *
 * Implementation from:
 * https://github.com/Fewbytes/rubber-docker/blob/master/levels/10_setuid/rd.py
 */
void setUpCpuLimit(Container* container)
{
    LOG_F(INFO, "Setting up CPU limits");
    std::string cpuDir = CGROUP_FOLDER + "/cpu/" + container->id;
    if (!std::filesystem::create_directories(cpuDir))
        throw std::runtime_error("Create directory " + cpuDir + ": FAILED");

    // Adds the container's process to the 'tasks' file
    if (!appendToFile(cpuDir + "/tasks", std::to_string(container->pid)))
        throw std::runtime_error("Write to file 'tasks': FAILED");

    if (!appendToFile(cpuDir + "/cpu.shares", std::to_string(container->resourceLimits->cpuShare)))
        throw std::runtime_error("Write to file 'cpu.shares': FAILED");

    LOG_F(INFO, "Set up CPU limits: SUCCESS");
}


/**
 * Initializes the amount of computing resources to which the container has access
 * (e.g. memory, cpu, number of processes, etc).
 */
void setUpResourceLimits(Container* container)
{
    LOG_F(INFO, "Setting up container resource limits");
    setUpProcessLimit(container);
    setUpCpuLimit(container);
    setUpMemoryLimit(container);
    LOG_F(INFO, "Set up container resource limits: SUCCESS");
}


/**
 * Registers the network namespace of the container by bind-mounting /proc/<pid>/ns/net
 * to /var/run/netns/<container_id>.
 * Note that two semaphores are used in this place to guarantee that a new network
 * namespace has been added with 'ip netns add' before mounting /proc/self/ns/net
 * to the new namespace.
 *
 * References:
 * - https://gist.github.com/cfra/39f4110366fa1ae9b1bddd1b47f586a3
 * - https://www.schutzwerk.com/en/43/posts/namespaces_03_pid_net/
 */
void setUpNetworkNamespace(Container* container)
{
    LOG_F(INFO, "Setting up network namespace");

    std::string procNetPath = "/proc/self/ns/net";
    std::string networkNamespacePath = "/var/run/netns/" + container->id;
    sem_wait(container->networkNsSemaphore);
//    sem_wait(sem_open(NETWORK_NS_SEM_NAME, 0));
    if (mount(procNetPath.c_str(), networkNamespacePath.c_str(), nullptr, MS_BIND, nullptr) != 0)
    {
        sem_post(container->networkInitSemaphore);
        throw std::runtime_error("Register network namespace with mount: FAILED [Errno " + std::to_string(errno) + "]");
    }

    sem_post(container->networkInitSemaphore);
//    sem_post(sem_open(NETWORK_INIT_SEM_NAME, 0));
    LOG_F(INFO, "Set up network namespace: SUCCESS");
}

/**
 * Mounts the overlay fs of the given container so that the rootfs archive does
 * not have to be unpacked every time a new container is created. More details can
 * be found at:
 * - https://www.kernel.org/doc/Documentation/filesystems/overlayfs.txt
 * - https://wiki.archlinux.org/title/Overlay_filesystem
 */
void mountOverlayFileSystem(Container* container)
{
    LOG_F(INFO, "Mounting overlay fs %s", container->rootfs.c_str());
    std::string imageRootDir = container->rootDir + "/cache/" + container->distroName + "/rootfs";
    std::string upperDir = container->dir + "/copy-on-write";
    std::string workDir = container->dir + "/work";
    std::string mountData = "lowerdir=" + imageRootDir + ",upperdir=" + upperDir + ",workdir=" + workDir;
    if (mount("overlay", container->rootfs.c_str(), "overlay", MS_NODEV, mountData.c_str()) != 0)
        throw std::runtime_error("Mount overlay fs: FAILED");
    LOG_F(INFO, "Mounting overlay fs %s: SUCCESS", container->rootfs.c_str());
}

/**
 * Changes the root file system so that the container's fs can be isolated.
 * If 'buildImage' is false, uses pivot_root() to make the container's rootfs
 * directory the new root file system.
 * If 'buildImage' is true, uses chroot.
 *
 * Implementation based on:
 * https://github.com/Fewbytes/rubber-docker/blob/master/levels/10_setuid/rd.py
 *
 */
void changeRoot(Container* container)
{
    LOG_F(INFO, "Isolating file system");

    if (container->buildImage)
    {
        if (chroot(container->rootfs.c_str()) != 0)
            throw std::runtime_error("[ERROR] chroot " + container->rootfs + ": FAILED");
        chdir("/");
    }
    else
    {
        std::string tempDir = container->rootfs + "/temp";
        if (!std::filesystem::create_directories(tempDir))
            throw std::runtime_error("Create temp directory " + tempDir + ": FAILED");

        if (pivot_root(container->rootfs.c_str(), tempDir.c_str()) != 0)
            throw std::runtime_error("Pivot root: FAILED [Errno " + std::to_string(errno) + "]");

        chdir("/");

        // Unmounts the temp directory
        if (umount2("/temp", MNT_DETACH))
            throw std::runtime_error("Unmount temp directory: FAILED [Errno " + std::to_string(errno) + "]");

        if (rmdir("/temp") != 0)
            throw std::runtime_error("Remove temp directory: FAILED [Errno " + std::to_string(errno) + "]");
    }
    LOG_F(INFO, "Isolate file system: SUCCESS");
}

/**
 * Mounts the necessary directories (e.g. proc, sys, dev) after entering
 * the chroot jail.
 */
void mountDirectories(Container* container)
{
    std::string containerDir = container->dir;
    LOG_F(INFO, "Mounting directories: proc, sys, dev");

    if (mount("proc", "/proc", "proc", 0, nullptr) != 0)
        throw std::runtime_error("Mount /proc: FAILED [Errno " + std::to_string(errno) + "]");

    if (mount("sysfs", "/sys", "sysfs", 0, nullptr) != 0)
        throw std::runtime_error("Mount /sys: FAILED [Errno " + std::to_string(errno) + "]");

    if (mount("tmpfs", "/dev", "tmpfs", MS_NOSUID | MS_STRICTATIME, nullptr) != 0)
        throw std::runtime_error("Mount /dev: FAILED [Errno " + std::to_string(errno) + "]");

    LOG_F(INFO, "Mounting directories: SUCCESS");
}

/**
 * Creates and adds some basic devices inside the container.
 * Implementation from:
 * - https://github.com/Fewbytes/rubber-docker/blob/master/levels/10_setuid/rd.py
 * - https://github.com/dmitrievanthony/sprat/blob/master/src/container.c
 */
void setUpDev(Container* container)
{
    LOG_F(INFO, "Creating basic devices");
    std::string devDir = "/dev/";
    // Creates and mounts /dev/pts
    std::string devPtsDir = devDir + "pts";
    if (!std::filesystem::exists(devPtsDir))
    {
        if (!std::filesystem::create_directories(devPtsDir))
            throw std::runtime_error("Create " + devPtsDir + ": FAILED");

        if (mount("devpts", devPtsDir.c_str(), "devpts", MS_NOEXEC | MS_NOSUID, "newinstance,ptmxmode=0666,mode=620,gid=5") < 0)
            throw std::runtime_error("Mount " + devPtsDir + ": FAILED [" + std::to_string(errno) + "]");
    }

    // Creates symlinks for stdin, stdout and stderr
    std::vector<std::string> streams = { "stdin", "stdout", "stderr" };
    if (symlink("/proc/self/fd", (devDir + "fd").c_str()) != 0)
        throw std::runtime_error("Create symlink for fd: FAILED [" + std::to_string(errno) + "]");

    for (std::size_t i = 0; i < streams.size(); i++)
    {
        std::string stream = streams.at(i);
        if (symlink(("/proc/self/fd/" + std::to_string(i)).c_str(), (devDir + stream).c_str()) != 0)
            throw std::runtime_error("Create symlink for " + stream + ": FAILED [" + std::to_string(errno) + "]");
    }

    std::vector<Device> devs = {
            { "null", S_IFCHR, 1, 3 },
            { "zero", S_IFCHR, 1, 5 },
            { "random", S_IFCHR, 1, 8 },
            { "urandom", S_IFCHR, 1, 9 },
            { "console", S_IFCHR, 136, 1 },
            { "tty", S_IFCHR, 5, 0 },
            { "full", S_IFCHR, 1, 7 }
    };

    for (const auto& dev : devs)
    {
        if (mknod((devDir + dev.name).c_str(), 0666 | dev.type, makedev(dev.major, dev.minor)) != 0)
            throw std::runtime_error("Create device " + dev.name + ": FAILED [" + std::to_string(errno) + "]");
    }
    LOG_F(INFO, "Create basic devices: SUCCESS");
}


/**
 * Clears the environment variables and initializes new ones which will be
 * used in the container.
 */
void setUpVariables(Container* container)
{
    LOG_F(INFO, "Setting up environment variables");
    clearenv();

    setenv("HOME", "/", 0);
    setenv("DISPLAY", ":0.0", 0);
    setenv("TERM", "xterm-256color", 0);
    setenv("PATH", "/bin:/sbin:/usr/bin:/usr/sbin:/src:/usr/local/bin:/usr/local/sbin", 0);

    LOG_F(INFO, "Set up environment variables: SUCCESS");
}


/**
 * Initializes a containerized environment in which the given Container will be run.
 * Performs the following actions in order upon entering the execute() function:
 * 1. Initializes all the resource limits of the container (e.g. memory, process, etc).
 * 2. Sets up network namespace.
 * 3. Mounts the root mount as private and recursively so that the sub-mounts will
 * not be visible to the parent mount.
 * 4. Mounts the overlay file system if 'buildImage' is set to false.
 * 5. Changes the root file system uses pivot_root(). This step has the effect as
 * performing chroot, except for the fact that it is more secure.
 * 6. Mounts the a list of required directories to the root file system in the container.
 * 7. Creates and sets up basic devices in the container.
 * 8. Sets up the environment variables in the container.
 * 9. Adds name server to resolv.conf.
 * 10. Changes the host name of the container
 * @return true if the all containment actions have been performed successfully, false otherwise.
 */
bool enterContainment(Container* container)
{
    LOG_F(INFO, "Initializing container %s", container->id.c_str());
    try
    {
        container->networkNsSemaphore = sem_open(NETWORK_NS_SEM_NAME, 0);
        if (container->networkNsSemaphore == SEM_FAILED)
            throw std::runtime_error("sem_open failed for " + std::string(NETWORK_NS_SEM_NAME));
        container->networkInitSemaphore = sem_open(NETWORK_INIT_SEM_NAME, 0);
        if (container->networkInitSemaphore == SEM_FAILED)
            throw std::runtime_error("sem_open failed for " + std::string(NETWORK_INIT_SEM_NAME));

        setUpNetworkNamespace(container);
        setUpResourceLimits(container);
        // From:
        // https://github.com/swetland/mkbox/blob/master/mkbox.c
        // https://github.com/dmitrievanthony/sprat/blob/master/src/container.c
        // ensure that changes to our mount namespace do not "leak" to
        // outside namespaces (what mount --make-rprivate / does)
        if (mount("/", "/", nullptr, MS_PRIVATE | MS_REC, nullptr) != 0)
            throw std::runtime_error("Set MS_PRIVATE to fs: FAILED " + std::to_string(errno) + "]");

        if (!container->buildImage)
            mountOverlayFileSystem(container);

        changeRoot(container);
        mountDirectories(container);
        setUpDev(container);
        setUpVariables(container);
        // Adds DNS resolver to resolv.conf
        appendToFile("/etc/resolv.conf", "nameserver " + DEFAULT_NAMESERVER);
        // Sets the new hostname to be the ID of the container
        sethostname(container->id.c_str(), container->id.length());
        // Blocks the current thread until network environment lization is finished
        sem_wait(container->networkInitSemaphore);
//        sem_wait(sem_open(NETWORK_INIT_SEM_NAME, 0));
        sem_close(container->networkNsSemaphore);
        sem_close(container->networkInitSemaphore);
    }
    catch (std::exception& ex)
    {
        LOG_F(ERROR, "Initialize container: FAILED");
        LOG_F(ERROR, "%s", ex.what());
        return false;
    }
    LOG_F(INFO, "Initialize container: SUCCESS");
    std::cout << "Container " << container->id << " initialized" << std::endl;
    return true;
}

/**
 * Unmounts the directories that have been mounted after entering the
 * chroot jail (e.g. proc, sys, dev).
 */
void unmountDirectories()
{
    LOG_F(INFO, "Unmounting directories: proc, sys, dev");
    std::vector<std::string> dirs = { "/proc", "/sys", "/dev/pts", "/dev" };
    for (const auto& dir : dirs)
    {
        if (umount(dir.c_str()) != 0)
            throw std::runtime_error("Unmount " + dir + ": FAILED [Errno " + std::to_string(errno) + "]");
    }

    LOG_F(INFO, "Unmounting directories: SUCCESS");
}


/**
 * Performs the following actions before exiting from the container:
 * 1. Unmounts the mounted directories
 */
void exitContainment(Container* container)
{
    unmountDirectories();
}


/**
 * Runs the container by invoking the system() function. Initializes the containerized
 * environment with the enterContainment() function. Performs actions listed in
 * exitContainment() upon exiting the container.
 *
 * @param arg: pointer to the Container struct which represents the container will be run.
 */
int execute(void* arg)
{
    auto* container = (Container*) arg;
    container->pid = getpid();
    if (!enterContainment(container))
        return -1;

    std::string command = container->command;
    std::cout << "Executing command: " << command << std::endl;
    if (system(command.c_str()) == -1)
    {
        LOG_F(ERROR, "Execute command %s: FAILED [Errno %d]", command.c_str(), errno);
        return -1;
    }
    exitContainment(container);
    return 0;
}

/**
 * Starts a containerized process by invoking the clone() function.
 * The new namespaces created are: pid, uts, network, mount. Waits
 * for the cloned process to finish.
 *
 * Implementations from:
 * - https://cesarvr.github.io/post/2018-05-22-create-containers/
 * - https://github.com/7aske/ccont/blob/master/source/jail.c
 */
void startContainer(Container* container)
{
    std::string info = "Starting container " + container->id + " with pid " + std::to_string(getpid());
    LOG_F(INFO, "%s", info.c_str());
    std::cout << info << std::endl;

    int flags =  SIGCHLD | CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS | CLONE_NEWNET;
    char* childStack = createStack();
    int pid = clone(execute, childStack, flags, (void*) container);
    if (pid < 0)
    {
        LOG_F(ERROR, "Start container %s: FAILED [Unable to create child process %d]", container->id.c_str(), pid);
        return;
    }
    int exitStatus;
    // Waits for the Container to finish executing the given command.
    if (waitpid(pid, &exitStatus, 0) == -1)
        LOG_F(INFO, "waitpid() failed for child process %d", pid);
    if (WIFEXITED(exitStatus))
    {
        info = "Container " + container->id + " exit status " + std::to_string(WEXITSTATUS(exitStatus));
        LOG_F(INFO, "%s", info.c_str());
        std::cout << info << std::endl;
    }
    else
    {
        // If seg fault happens during the execution
        LOG_F(ERROR, "Container %s exited with status: %d", container->id.c_str(), exitStatus);
    }
}

/**
 * Packages the rootfs directory of the container into a tarball and saves
 * it to <root-dir>/images.
 */
void buildContainerImage(Container* container)
{
    LOG_F(INFO, "Building image for container %s", container->id.c_str());
    std::cout << "Building image for container " << container->id << "..." << std::endl;
    std::string imageDir = container->rootDir + "/images";
    if (!std::filesystem::exists(imageDir))
    {
        if (!std::filesystem::create_directories(imageDir))
            throw std::runtime_error("Create directory " + imageDir + ": FAILED");
        else
            LOG_F(INFO, "Create directory %s: SUCCESS", imageDir.c_str());
    }

    std::string imageFilePath = imageDir + "/" + container->id + ".tar.gz";
    char buffer[256];
    sprintf(buffer, "tar -czf %s -C %s .", imageFilePath.c_str(), container->rootfs.c_str());
    if (system(buffer) == -1)
        throw std::runtime_error("Build image for container: FAILED");

    std::cout << "Container image saved to " << imageFilePath << std::endl;
    LOG_F(INFO, "Build image for container: SUCCESS");
}


/**
 * Removes the cgroup limitations imposed on the container
 * for pids, CPU and memory by deleting the corresponding directories
 * in /sys/fs/cgroup.
 */
void removeCGroupDirs(Container* container)
{
    LOG_F(INFO, "Removing CGroup folders of container %s", container->id.c_str());
    std::vector<std::string> resources = {
            "pids", "memory", "cpu"
    };
    for (const auto& resource : resources)
    {
        std::string dir = CGROUP_FOLDER + "/" + resource + "/" + container->id;
        // FIXME Only rmdir can remove the directory in cgroup, yet it always has an errno of 21
        if (rmdir(dir.c_str()) < 0 && errno != 21)
        {
            throw std::runtime_error("Remove directory " + dir + ": FAILED [Errno " + std::to_string(errno) + "]");
        }
    }
    LOG_F(INFO, "Remove CGroup folders: SUCCESS");
}


/**
 * Removes the directory that contains the file system of the given Container.
 */
void removeContainerDirectory(Container* container)
{
    std::string containerDir = container->dir;
    LOG_F(INFO, "Removing %s", containerDir.c_str());
    std::error_code errorCode;
    if (!std::filesystem::remove_all(containerDir, errorCode))
    {
        throw std::runtime_error("Remove directory " + containerDir + ": FAILED " + errorCode.message());
    }
    LOG_F(INFO, "Removing %s: SUCCESS", containerDir.c_str());
}


/**
 * Cleans up the networking environment by performing the following actions:
 * 1. Unmounts /var/run/netns/<container_id>.
 * 2. Deletes the veth pair.
 * 3. Deletes the network namespace by running 'ip netns del'.
 */
void cleanUpContainerNetwork(Container* container)
{
    LOG_F(INFO, "Cleaning up container network environment");

    std::string networkNamespacePath = "/var/run/netns/" + container->id;
    if (umount(networkNamespacePath.c_str()) != 0)
        throw std::runtime_error("Unmount " + networkNamespacePath + ": FAILED [Errno " + std::to_string(errno) + "]");

    std::vector<std::string> commands = {
            "ip link delete " + container->vEthPair.second,
            "ip netns del " + container->id
    };
    for (const auto& command : commands)
    {
        if (system(command.c_str()) == -1)
            throw std::runtime_error("Execute command " + command + ": FAILED");
    }

    LOG_F(INFO, "Clean up container network environment: SUCCESS");
}

/**
 * Frees all resources occupied by the given container struct.
 */
void destroyContainer(Container* container)
{
    delete container->resourceLimits;

    sem_close(container->networkNsSemaphore);
    sem_unlink(NETWORK_INIT_SEM_NAME);
    sem_close(container->networkInitSemaphore);
    sem_unlink(NETWORK_NS_SEM_NAME);

    delete container;
}

/**
 * Cleans up the system after a container finishes running.
 * Performs the following actions:
 * 1. Builds a tarball image for the container and saves it
 * to <root-dir>/images.
 * 2. Deletes the container rootfs directory.
 * 3. Removes the container-associated folders created in the cgroup folder.
 * 4. Cleans up the networking environment of the container.
 * 5. Deallocates all the memory taken up by the given Container struct.
 * @return true if clean up succeeds, false otherwise.
 */
bool cleanUpContainer(Container* container)
{
    std::string containerId = container->id;
    LOG_F(INFO, "Clean up container %s", containerId.c_str());
    try
    {
        if (container->buildImage)
            buildContainerImage(container);
        removeContainerDirectory(container);
        removeCGroupDirs(container);
        cleanUpContainerNetwork(container);
        destroyContainer(container);
        std::cout << "Container " << containerId << " destroyed" << std::endl;
        LOG_F(INFO, "Clean up container %s: SUCCESS", containerId.c_str());
        return true;
    }
    catch (std::exception& ex)
    {
        LOG_F(ERROR, "Clean up container %s: FAILED", containerId.c_str());
        LOG_F(ERROR, "%s", ex.what());
        return false;
    }
}
