//
// Created by siyuan on 18/07/2021.
//
#include <string>
#include <iostream>
#include <sys/mount.h>
#include <filesystem>
#include <unistd.h>
#include <cstring>
#include <csignal>
#include <sched.h>
#include <sys/wait.h>

#include "constants.h"
#include "container.h"
#include "utils.h"

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
 * root directory. This function performs the following actions in order:
 *
 * 1. Checks if the cache directory exists in the root directory. Creates it if it does not.
 * 2. Checks if the rootfs archive for the specified distro exists. Fetches it from the pre-defined
 * download URL if it is not present in the cache directory.
 * 3. Checks if there is a directory present for the container in rootDir/containers/. Creates one
 * for this container with the container ID as the directory name if it has not been created.
 * 4. Extracts the file system of the distro to the container's directory.
 *
 * @param container: the container whose file system will be created.
 */
void setupContainerImage(Container* container)
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

    Distro distro = stringToDistro[container->distroName];
    std::string downloadUrl;
    switch (distro)
    {
        case Ubuntu:
            downloadUrl = UBUNTU_IMAGE_URL;
            break;

        case Alpine:
            downloadUrl = ALPINE_IMAGE_URL;
            break;
    }
    std::string baseArchiveName(basename(downloadUrl.c_str()));
    std::string rootfsArchive = cacheDir + "/" + baseArchiveName;

    char buffer[256];

    // Downloads the file system archive if it is not present in the cache directory
    if (!std::filesystem::exists(rootfsArchive))
    {
        std::cout << "Rootfs for " + container->distroName + " does not exist." << std::endl;
        std::cout << "Downloading " + baseArchiveName + " from " + downloadUrl << std::endl;
        sprintf(buffer, "wget -O %s %s -q --show-progress", rootfsArchive.c_str(), downloadUrl.c_str());
        system(buffer);
    }

    container->dir = container->rootDir + "/containers/" + container->id;
    std::string containerDir = container->dir;
    std::string currentUser = container->currentUser;

    // Creates the container directory and extracts the specified root file system
    if (!std::filesystem::exists(containerDir))
    {
        if (std::filesystem::create_directories(containerDir))
            std::cout << "Create container directory " + containerDir + ": SUCCESS" << std::endl;
        else
            throw std::runtime_error("[ERROR] Failed to create container directory " + containerDir);

        sprintf(buffer, "tar xzvf %s -C %s > /dev/null", rootfsArchive.c_str(), containerDir.c_str());
        if (system(buffer) != 0)
            throw std::runtime_error("[ERROR] Failed to extract " + rootfsArchive + " to " + containerDir);
        else
            std::cout << "Extract rootfs " + rootfsArchive + " to " + containerDir + ": SUCCESS" << std::endl;

        // Makes the current user the owner of the container directory
        sprintf(buffer, "chown -R %s %s", currentUser.c_str(), containerDir.c_str());
        if (system(buffer) != 0)
            throw std::runtime_error("[ERROR] Failed to make " + currentUser + " the owner of " + containerDir);
        else
            std::cout << "Setting the owner of " + containerDir + " to " + currentUser << std::endl;
    }
}

/**
 * Prepares and sets up the environment required for the container to
 * run correctly. Performs the following actions:
 *
 * 1. Downloads and extracts the rootfs to the specified folder.
 * @return true if set up succeeds, false otherwise.
 */
bool setUpContainer(Container* container)
{
    std::cout << "Set up container " << container->id << std::endl;
    try
    {
        setupContainerImage(container);
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
 * Starts a chroot jail for the container and sets up the mounts for
 * the current container.
 */
void enterChrootJail(Container* container)
{
    std::cout << "Entering chroot jail" << std::endl;

    if (chroot(container->dir.c_str()) != 0)
        throw std::runtime_error("[ERROR] chroot " + container->dir + ": FAILED");
    chdir("/");
    std::cout << "Entering chroot jail: SUCCESS" << std::endl;
}

/**
 * Mounts the necessary directories (e.g. proc, sys, dev) after entering
 * the chroot jail.
 */
void mountDirectories(Container* container)
{
    std::string containerDir = container->dir;
    std::cout << "Mounting directories: proc" << std::endl;

    if (mount("proc", "/proc", "proc", 0, nullptr) != 0)
        throw std::runtime_error("[ERROR] Mount /proc: FAILED [Errno " + std::to_string(errno) + "]");

    std::cout << "Mounting directories: SUCCESS" << std::endl;
}

/**
 * Clears the environment variables and initializes new ones which will be
 * used in the container.
 */
void setUpVariables(Container* container)
{
    clearenv();

    // Sets the new hostname to be the ID of the container
//    char buffer[256];
//    sprintf(buffer, "hostname %s", container->id.c_str());
//    system(buffer);
    sethostname(container->id.c_str(), container->id.length());

    setenv("HOME", "/", 0);
    setenv("DISPLAY", ":0.0", 0);
    setenv("TERM", "xterm-256color", 0);
    setenv("PATH", "/bin:/sbin:/usr/bin:/usr/sbin:/src:/usr/local/bin:/usr/local/sbin", 0);
}

/**
 * Creates new name spaces for the container.
 */
void createNamespace()
{
    std::cout << "Creating namespace";
    if (unshare(CLONE_NEWNS) != 0)
        throw std::runtime_error("[ERROR] Create namespace: FAILED");
    std::cout << "Create namespace: SUCCESS" << std::endl;
}


/**
 * Initializes a containerized environment in which the given Container will be run.
 * Performs the following actions in order:
 * 1. Enters the chroot jail.
 * 2. Mounts the a list of required directories to the root file system in the container.
 * 3. Sets up the environment variables in the container.
 * @return true if the all containment actions have been performed successfully, false otherwise.
 */
bool contain(Container* container)
{
    std::cout << "Initializing container " << container->id << std::endl;
    try
    {
        // From:
        // https://github.com/swetland/mkbox/blob/master/mkbox.c
        // https://github.com/dmitrievanthony/sprat/blob/master/src/container.c
        // ensure that changes to our mount namespace do not "leak" to
        // outside namespaces (what mount --make-rprivate / does)
        if (mount("/", "/", nullptr, MS_PRIVATE, nullptr) != 0)
            throw std::runtime_error("[ERROR] Set MS_PRIVATE to fs: FAILED " + std::to_string(errno) + "]");
        enterChrootJail(container);
        mountDirectories(container);
        setUpVariables(container);
        // createNamespace();
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
 * @param arg: a pointer to the Container struct which represents the container will be run.
 */
int execute(void* arg)
{
    auto* container = (Container*) arg;
    if (!contain(container))
        return -1;

    std::string command = container->command;
    std::cout << "Executing command: " << command << std::endl;
    if (system(command.c_str()) == -1)
    {
        std::cout << "[ERROR] Execute command " << command << ": FAILED [Errno " << errno << "]" << std::endl;
        return -1;
    }
    if (umount("proc") != 0)
        throw std::runtime_error("[ERROR] Unmount /proc: FAILED [Errno " + std::to_string(errno) + "]");

    return 0;
//    char* args[] = { "/bin/bash", nullptr };
//    if (execv(args[0], &args[0]) == -1)
//    {
//        std::cout << "[ERROR] Execute command " << command << ": FAILED [Errno " << errno << "]" << std::endl;
//        return -1;
//    }
//    return 0;
}

/**
 *
 * Implementations from:
 * - https://cesarvr.github.io/post/2018-05-22-create-containers/
 * - https://github1s.com/7aske/ccont/blob/master/source/jail.c
 */
void startContainer(Container* container)
{
    int flags = CLONE_NEWPID | CLONE_NEWUTS | SIGCHLD | CLONE_NEWNS;
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
 * Unmounts the directories that have been mounted after entering the
 * chroot jail (e.g. proc, sys, dev).
 */
void unmountDirectories(Container* container)
{
    std::string containerDir = container->dir;
    std::cout << "Unmounting directories: proc" << std::endl;
    if (umount((container->dir + "/proc").c_str()) != 0)
        throw std::runtime_error("[ERROR] Unmount /proc: FAILED [Errno " + std::to_string(errno) + "]");
    std::cout << "Unmounting directories: SUCCESS" << std::endl;
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
        throw std::runtime_error("[ERROR] Failed to remove directory " + containerDir + ": " + errorCode.message());
    }
    std::cout << "Removing " << containerDir << ": SUCCESS" << std::endl;
}

/**
 * Cleans up the system after a container finishes running.
 * Performs the following actions:
 * 1. Unmounts the mounted directories.
 * 2. Deletes the container rootfs directory.
 * 3. Deallocates the memory taken up by the given Container struct.
 * @return true if clean up succeeds, false otherwise.
 */
bool cleanUpContainer(Container* container)
{
    std::string containerId = container->id;
    std::cout << "Clean up container " << containerId << std::endl;
    try
    {
        // unmountDirectories(container);
        removeContainerDirectory(container);
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
