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
        std::string& command)
{
    auto* container = new Container;
    container->distroName = distroName;
    container->id = containerId;
    container->rootDir = rootDir;
    container->command = command;

    // Retrieves the name of the current user
    char buffer[128];
    getlogin_r(buffer, 128);
    container->currentUser = std::string(buffer);

    return container;
}

/**
 * Sets up the root file system of the container as per the specified linux distro and the
 * root directory (the lower-dir in an overlay fs). This function performs the following actions:
 *
 * 1. Checks if the cache directory exists in the root directory. Creates it if it does not.
 * 2. Checks if the rootfs archive for the specified distro exists. Fetches it from the pre-defined
 * download URL if it is not present in the cache directory.
 * 3. If the 'rootfs' directory does not exist for the specified distro, creates said directory
 * and unpacks the file system of the distro in it.
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
            throw std::runtime_error("[ERROR] Failed to create cache directory " + cacheDir);
    }


    std::string downloadUrl = stringToDownloadUrl[container->distroName];

    std::string baseArchiveName(basename(downloadUrl.c_str()));
    std::string rootfsArchive = cacheDir + "/" + baseArchiveName;

    char buffer[256];

    // Downloads the file system archive if it is not present in the cache directory
    if (!std::filesystem::exists(rootfsArchive))
    {
        std::cout << "Rootfs for " + container->distroName + " does not exist." << std::endl;
        std::cout << "Downloading " + baseArchiveName + " from " + downloadUrl << std::endl;
        sprintf(buffer, "wget -O %s %s -q --show-progress", rootfsArchive.c_str(), downloadUrl.c_str());
        if (system(buffer) == -1)
            throw std::runtime_error("[ERROR] Download rootfs archive for " + distroName + ": FAILED");
    }

    // Creates the rootfs folder for the given distro and extracts the archive
    std::string imageRootDir = cacheDir + "/rootfs";
    if (!std::filesystem::exists(imageRootDir))
    {
        if (std::filesystem::create_directories(imageRootDir))
            std::cout << "Create directory " + imageRootDir + ": SUCCESS" << std::endl;
        else
            throw std::runtime_error("[ERROR] Create directory " + imageRootDir + ": FAILED");

        // Extracts the files
        std::cout << "Extracting rootfs from " << rootfsArchive << " to " << imageRootDir << std::endl;
        sprintf(buffer, "tar xvf %s -C %s > /dev/null", rootfsArchive.c_str(), imageRootDir.c_str());
        if (system(buffer) == -1)
            throw std::runtime_error("[ERROR] Extract " + rootfsArchive + " to " + imageRootDir + ": FAILED");
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
void setUpContainerDirectory(Container* container)
{
    char buffer[256];
    container->dir = container->rootDir + "/containers/" + container->id;
    std::string containerDir = container->dir;
    std::string currentUser = container->currentUser;

    // Creates the container directory and extracts the specified root file system
    if (!std::filesystem::exists(containerDir))
    {
        if (std::filesystem::create_directories(containerDir))
            std::cout << "Create container directory " + containerDir + ": SUCCESS" << std::endl;
        else
            throw std::runtime_error("[ERROR] Create container directory " + containerDir + ": FAILED");

        // Creates copy-on-write (upper), and work directory for the overlay fs
        std::string upperDir = containerDir + "/copy-on-write";
        std::string workDir = containerDir + "/work";
        container->rootfs = containerDir + "/rootfs";
        std::cout << "Setting up overlay fs directories in " << containerDir << std::endl;
        std::vector<std::string> dirs = { upperDir, workDir, container->rootfs };
        for (const auto& dir : dirs)
        {
            if (!std::filesystem::create_directories(dir))
                throw std::runtime_error("[ERROR] Create " + dir + ": FAILED");
        }
        std::cout << "Set up overlay fs directories in " << containerDir << ": SUCCESS" << std::endl;

        // Makes the current user the owner of the container directory
        sprintf(buffer, "chown -R %s %s", currentUser.c_str(), containerDir.c_str());
        if (system(buffer) == -1)
            throw std::runtime_error("[ERROR] Make" + currentUser + " the owner of " + containerDir + ": FAILED");
        else
            std::cout << "Setting the owner of " + containerDir + " to " + currentUser << std::endl;
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
        int connectionCount = std::stoi(systemWithOutput("brctl show " + BRIDGE_NAME + " | grep veth1 | wc -l")) + 1;
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
    std::cout << "Initializing container network environment" << std::endl;
    std::string newNetworkNs = container->id;
    // Takes the first 9 characters of the container's ID as the suffix
    // for the names of the veth pair since a valid interface name contains
    // less than 16 characters
    std::string suffix = container->id.substr(0, 9);
    auto vEthPair = std::make_pair("veth0@" + suffix, "veth1@" + suffix);
    container->vEthPair = vEthPair;
    std::string containerIp = getContainerVEthIp();
    container->ip = containerIp;
    std::cout << "[DEBUG] Container IP: " << containerIp << std::endl;
    std::vector<std::string> commands = {
            // Adds the container's ID as a new network namespace
            "ip netns add " + newNetworkNs,
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
            throw std::runtime_error("[ERROR] Execute command " + command + ": FAILED");
    }

    std::cout << "Initialize container network environment: SUCCESS" << std::endl;
}



/**
 * Prepares and sets up the environment required for the container to
 * run correctly. Performs the following actions:
 * 1. Downloads and extracts the rootfs to the specified folder.
 * 2. Initializes the overlay fs folders in the container's directory.
 * 3. Initializes the networking environment for the given container.
 *
 * @param container a struct representing the container whose file system will initialized.
 * @return true if set up succeeds, false otherwise.
 */
bool setUpContainer(Container* container)
{
    std::cout << "Set up container " << container->id << std::endl;
    try
    {
        setUpContainerImage(container);
        setUpContainerDirectory(container);
        initializeContainerNetwork(container);
        std::cout << "Set up container " << container->id << ": SUCCESS" << std::endl;
        return true;
    }
    catch (std::exception& ex)
    {
        std::cout << "[ERROR] Set up container " << container->id << ": FAILED" << std::endl;
        std::cout << ex.what() << std::endl;
        return false;
    }
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
        throw std::runtime_error("[ERROR] Allocate memory: FAILED");
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

    std::string pidDir = CGROUP_FOLDER + "/pids/" + container->id;
    if (!std::filesystem::create_directories(pidDir))
        throw std::runtime_error("[ERROR] Create directory " + pidDir + ": FAILED");


    if (!appendToFile(pidDir + "/pids.max", "20"))
        throw std::runtime_error("[ERROR] Write to file 'pids.max': FAILED");
    if (!appendToFile(pidDir + "/notify_on_release", "1"))
        throw std::runtime_error("[ERROR] Write to file 'notify_on_release': FAILED");
    if (!appendToFile(pidDir + "/cgroup.procs", std::to_string(container->pid)))
        throw std::runtime_error("[ERROR] Write to 'cgroup.procs': FAILED");
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
 *
 * Implementation from:
 * https://github.com/Fewbytes/rubber-docker/blob/master/levels/10_setuid/rd.py
 */
void setUpMemoryLimit(Container* container)
{
    std::string memoryDir = CGROUP_FOLDER + "/memory/" + container->id;
    if (!std::filesystem::create_directories(memoryDir))
        throw std::runtime_error("[ERROR] Create directory " + memoryDir + ": FAILED");

    // Adds the container's process to the 'tasks' file
    if (!appendToFile(memoryDir + "/tasks", std::to_string(container->pid)))
        throw std::runtime_error("[ERROR] Write to file 'tasks': FAILED");

    if (!appendToFile(memoryDir + "/memory.limit_in_bytes", "256m"))
        throw std::runtime_error("[ERROR] Write to file 'memory.limit_in_bytes': FAILED");

    if (!appendToFile(memoryDir + "/memory.memsw.limit_in_bytes", "256m"))
        throw std::runtime_error("[ERROR] Write to file 'memory.memsw.limit_in_bytes': FAILED");
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
    std::string cpuDir = CGROUP_FOLDER + "/cpu/" + container->id;
    if (!std::filesystem::create_directories(cpuDir))
        throw std::runtime_error("[ERROR] Create directory " + cpuDir + ": FAILED");

    // Adds the container's process to the 'tasks' file
    if (!appendToFile(cpuDir + "/tasks", std::to_string(container->pid)))
        throw std::runtime_error("[ERROR] Write to file 'tasks': FAILED");

    if (!appendToFile(cpuDir + "/cpu.shares", std::to_string(512)))
        throw std::runtime_error("[ERROR] Write to file 'cpu.shares': FAILED");
}


/**
 * Initializes the amount of computing resources to which the container has access
 * (e.g. memory, cpu, number of processes, etc).
 */
void setUpResourceLimits(Container* container)
{

    std::cout << "Setting up container resource limits" << std::endl;
    setUpProcessLimit(container);
    setUpCpuLimit(container);
    setUpMemoryLimit(container);
    std::cout << "Set up container resource limits: SUCCESS" << std::endl;
}


/**
 * Registers the network namespace of the container by bind-mounting /proc/<pid>/ns/net
 * to /var/run/netns/<container_id>.
 *
 * References:
 * - https://gist.github.com/cfra/39f4110366fa1ae9b1bddd1b47f586a3
 * - https://www.schutzwerk.com/en/43/posts/namespaces_03_pid_net/
 */
void setUpNetworkNamespace(Container* container)
{
    std::cout << "Setting up network namespace" << std::endl;

    // std::string procNetPath = "/proc/" + std::to_string(container->pid) + "/ns/net";
    std::string procNetPath = "/proc/self/ns/net";
    std::string networkNamespacePath = "/var/run/netns/" + container->id;
    if (mount(procNetPath.c_str(), networkNamespacePath.c_str(), nullptr, MS_BIND, nullptr) != 0)
        throw std::runtime_error("[ERROR] Register network namespace with mount: FAILED [Errno " + std::to_string(errno) + "]");

    std::cout << "Set up network namespace: SUCCESS" << std::endl;
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
    std::cout << "Mounting overlay fs " << container->rootfs << std::endl;
    std::string imageRootDir = container->rootDir + "/cache/" + container->distroName + "/rootfs";
    std::string upperDir = container->dir + "/copy-on-write";
    std::string workDir = container->dir + "/work";
    std::string mountData = "lowerdir=" + imageRootDir + ",upperdir=" + upperDir + ",workdir=" + workDir;
    if (mount("overlay", container->rootfs.c_str(), "overlay", MS_NODEV, mountData.c_str()) != 0)
        throw std::runtime_error("[ERROR] Mount overlay fs: FAILED");
    std::cout << "Mounting overlay fs " << container->rootfs << ": SUCCESS" << std::endl;
}

/**
 * Uses pivot_root() to make the container's rootfs directory the new root file system.
 * Implementation based on:
 * https://github.com/Fewbytes/rubber-docker/blob/master/levels/10_setuid/rd.py
 */
void pivotRoot(Container* container)
{
    std::cout << "Performing pivot root" << std::endl;

    std::string tempDir = container->rootfs + "/temp";
    if(!std::filesystem::create_directories(tempDir))
        throw std::runtime_error("[ERROR] Create temp directory " + tempDir + ": FAILED");

    if (pivot_root(container->rootfs.c_str(), tempDir.c_str()) != 0)
        throw std::runtime_error("[ERROR] Pivot root: FAILED");

    chdir("/");

    // Unmounts the temp directory
    if (umount2("/temp", MNT_DETACH))
        throw std::runtime_error("[ERROR] Unmount temp directory: FAILED [Errno " + std::to_string(errno) + "]");

    if (rmdir("/temp") != 0)
        throw std::runtime_error("[ERROR] Remove temp directory: FAILED [Errno " + std::to_string(errno) + "]");

    std::cout << "Perform pivot root: SUCCESS" << std::endl;
}

/**
 * Mounts the necessary directories (e.g. proc, sys, dev) after entering
 * the chroot jail.
 */
void mountDirectories(Container* container)
{
    std::string containerDir = container->dir;
    std::cout << "Mounting directories: proc, sys, dev" << std::endl;

    if (mount("proc", "/proc", "proc", 0, nullptr) != 0)
        throw std::runtime_error("[ERROR] Mount /proc: FAILED [Errno " + std::to_string(errno) + "]");

    if (mount("sysfs", "/sys", "sysfs", 0, nullptr) != 0)
        throw std::runtime_error("[ERROR] Mount /sys: FAILED [Errno " + std::to_string(errno) + "]");

    if (mount("tmpfs", "/dev", "tmpfs", MS_NOSUID | MS_STRICTATIME, nullptr) != 0)
        throw std::runtime_error("[ERROR] Mount /dev: FAILED [Errno " + std::to_string(errno) + "]");

    std::cout << "Mounting directories: SUCCESS" << std::endl;
}

/**
 * Creates and adds some basic devices inside the container.
 * Implementation from:
 * - https://github.com/Fewbytes/rubber-docker/blob/master/levels/10_setuid/rd.py
 * - https://github.com/dmitrievanthony/sprat/blob/master/src/container.c
 */
void setUpDev(Container* container)
{
    std::cout << "Creating basic devices" << std::endl;
    std::string devDir = "/dev/";
    // Creates and mounts /dev/pts
    std::string devPtsDir = devDir + "pts";
    if (!std::filesystem::exists(devPtsDir))
    {
        if (!std::filesystem::create_directories(devPtsDir))
            throw std::runtime_error("[ERROR] Create " + devPtsDir + ": FAILED");

        if (mount("devpts", devPtsDir.c_str(), "devpts", MS_NOEXEC | MS_NOSUID, "newinstance,ptmxmode=0666,mode=620,gid=5") < 0)
            throw std::runtime_error("[ERROR] Mount " + devPtsDir + ": FAILED [" + std::to_string(errno) + "]");
    }

    // Creates symlinks for stdin, stdout and stderr
    std::vector<std::string> streams = { "stdin", "stdout", "stderr" };
    if (symlink("/proc/self/fd", (devDir + "fd").c_str()) != 0)
        throw std::runtime_error("[ERROR] Create symlink for fd: FAILED [" + std::to_string(errno) + "]");

    for (std::size_t i = 0; i < streams.size(); i++)
    {
        std::string stream = streams.at(i);
        if (symlink(("/proc/self/fd/" + std::to_string(i)).c_str(), (devDir + stream).c_str()) != 0)
            throw std::runtime_error("[ERROR] Create symlink for " + stream + ": FAILED [" + std::to_string(errno) + "]");
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
            throw std::runtime_error("[ERROR] Create device " + dev.name + ": FAILED [" + std::to_string(errno) + "]");
    }
    std::cout << "Create basic devices: SUCCESS" << std::endl;
}


/**
 * Clears the environment variables and initializes new ones which will be
 * used in the container.
 */
void setUpVariables(Container* container)
{
    clearenv();

    setenv("HOME", "/", 0);
    setenv("DISPLAY", ":0.0", 0);
    setenv("TERM", "xterm-256color", 0);
    setenv("PATH", "/bin:/sbin:/usr/bin:/usr/sbin:/src:/usr/local/bin:/usr/local/sbin", 0);
}


/**
 * Initializes a containerized environment in which the given Container will be run.
 * Performs the following actions in order upon entering the execute() function:
 * 1. Initializes all the resource limits of the container (e.g. memory, process, etc).
 * 2. Sets up network namespace.
 * 3. Mounts the root mount as private and recursively so that the sub-mounts will
 * not be visible to the parent mount.
 * 4. Mounts the overlay file system.
 * 5. Changes the root file system uses pivot_root(). This step has the effect as
 * performing chroot, except for the fact that it is more secure.
 * 6. Mounts the a list of required directories to the root file system in the container.
 * 7. Creates and sets up basic devices in the container.
 * 8. Sets up the environment variables in the container.
 * 9. Changes the host name of the container
 * @return true if the all containment actions have been performed successfully, false otherwise.
 */
bool enterContainment(Container* container)
{
    std::cout << "Initializing container " << container->id << std::endl;
    try
    {

        setUpResourceLimits(container);
        setUpNetworkNamespace(container);
        // From:
        // https://github.com/swetland/mkbox/blob/master/mkbox.c
        // https://github.com/dmitrievanthony/sprat/blob/master/src/container.c
        // ensure that changes to our mount namespace do not "leak" to
        // outside namespaces (what mount --make-rprivate / does)
        if (mount("/", "/", nullptr, MS_PRIVATE | MS_REC, nullptr) != 0)
            throw std::runtime_error("[ERROR] Set MS_PRIVATE to fs: FAILED " + std::to_string(errno) + "]");

        mountOverlayFileSystem(container);
        pivotRoot(container);
        mountDirectories(container);
        setUpDev(container);
        setUpVariables(container);
        // Sets the new hostname to be the ID of the container
        sethostname(container->id.c_str(), container->id.length());
        return true;
    }
    catch (std::exception& ex)
    {
        std::cout << "[ERROR] Initialize container: FAILED" << std::endl;
        std::cout << ex.what() << std::endl;
        return false;
    }
}

/**
 * Unmounts the directories that have been mounted after entering the
 * chroot jail (e.g. proc, sys, dev).
 */
void unmountDirectories()
{
    std::cout << "Unmounting directories: proc, sys, dev" << std::endl;
    std::vector<std::string> dirs = { "/proc", "/sys", "/dev/pts", "/dev" };
    for (const auto& dir : dirs)
    {
        if (umount(dir.c_str()) != 0)
            throw std::runtime_error("[ERROR] Unmount " + dir + ": FAILED [Errno " + std::to_string(errno) + "]");
    }

    std::cout << "Unmounting directories: SUCCESS" << std::endl;
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
        std::cout << "[ERROR] Execute command " << command << ": FAILED [Errno " << errno << "]" << std::endl;
        return -1;
    }
    exitContainment(container);
    return 0;
}

/**
 *
 * Implementations from:
 * - https://cesarvr.github.io/post/2018-05-22-create-containers/
 * - https://github1s.com/7aske/ccont/blob/master/source/jail.c
 */
void startContainer(Container* container)
{
    std::cout << "Starting container with pid " << getpid() << std::endl;
    int flags =  SIGCHLD | CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS | CLONE_NEWNET;
    char* childStack = createStack();
    int pid = clone(execute, childStack, flags, (void*) container);
    if (pid < 0)
    {
        std::cout << "[ERROR] Start container " << container->id
                  << ": FAILED [Unable to create child process " << pid << "]" << std::endl;
        return;
    }
    int exitStatus;
    // Waits for the Container to finish executing the given command.
    if (waitpid(pid, &exitStatus, 0) == -1)
        std::cout << "[ERROR] waitpid() failed for child process " << pid << std::endl;
    if (WIFEXITED(exitStatus))
        std::cout << "Container " << container->id << " exit status: " << WEXITSTATUS(exitStatus) << std::endl;
}


/**
 * Removes the cgroup limitations imposed on the container
 * for pids, CPU and memory by deleting the corresponding directories
 * in /sys/fs/cgroup.
 */
void removeCGroupDirs(Container* container)
{
    std::cout << "Removing CGroup folders of container" << container->id << std::endl;
    std::vector<std::string> resources = {
            "pids", "memory", "cpu"
    };
    for (const auto& resource : resources)
    {
        std::string dir = CGROUP_FOLDER + "/" + resource + "/" + container->id;
        // FIXME Only rmdir can remove the directory in cgroup, yet it always has an errno of 21
        if (rmdir(dir.c_str()) < 0 && errno != 21)
        {
            throw std::runtime_error(
                    "[ERROR] Remove directory " + dir + ": FAILED [Errno " + std::to_string(errno) + "]");
        }
    }
    std::cout << "Remove CGroup folders: SUCCESS"<< std::endl;
}


/**
 * Removes the directory that contains the file system of the given Container.
 */
void removeContainerDirectory(Container* container)
{
    std::string containerDir = container->dir;
    std::cout << "Removing " << containerDir << std::endl;
    std::error_code errorCode;
    if (!std::filesystem::remove_all(containerDir, errorCode))
    {
        throw std::runtime_error("[ERROR] Remove directory " + containerDir + ": FAILED " + errorCode.message());
    }
    std::cout << "Removing " << containerDir << ": SUCCESS" << std::endl;
}


/**
 * Cleans up the networking environment by performing the following actions:
 * 1. Unmounts /var/run/netns/<container_id>.
 * 2. Deletes the network namespace by running 'ip netns del'.
 * 3. Deletes the veth pair.
 */
void cleanUpContainerNetwork(Container* container)
{
    std::cout << "Cleaning up container network environment" << std::endl;

    std::string networkNamespacePath = "/var/run/netns/" + container->id;
    if (umount(networkNamespacePath.c_str()) != 0)
        throw std::runtime_error("[ERROR] Unmount " + networkNamespacePath + ": FAILED [Errno" + std::to_string(errno) + "]");

    std::vector<std::string> commands = {
            "ip netns del " + container->id,
            "ip link delete " + container->vEthPair.second
    };
    for (const auto& command : commands)
    {
        if (system(command.c_str()) == -1)
            throw std::runtime_error("[ERROR] Execute command " + command + ": FAILED");
    }

    std::cout << "Clean up container network environment: SUCCESS" << std::endl;
}


/**
 * Cleans up the system after a container finishes running.
 * Performs the following actions:
 * 1. Deletes the container rootfs directory.
 * 2. Removes the container-associated folders created in the cgroup folder.
 * 3. Cleans up the networking environment of the container.
 * 4. Deallocates the memory taken up by the given Container struct.
 * @return true if clean up succeeds, false otherwise.
 */
bool cleanUpContainer(Container* container)
{
    std::string containerId = container->id;
    std::cout << "Clean up container " << containerId << std::endl;
    try
    {
        removeContainerDirectory(container);
        removeCGroupDirs(container);
        cleanUpContainerNetwork(container);
        delete container;
        std::cout << "Clean up container " << containerId << ": SUCCESS" << std::endl;
        return true;
    }
    catch (std::exception& ex)
    {
        std::cout << "[ERROR] Clean up container " << containerId << ": FAILED" << std::endl;
        std::cout << ex.what() << std::endl;
        return false;
    }
}
