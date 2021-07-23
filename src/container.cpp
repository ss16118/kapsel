//
// Created by siyuan on 18/07/2021.
//
#include <string>
#include <iostream>
#include <filesystem>
#include <unistd.h>
#include <cstring>

#include "constants.h"
#include "container.h"
#include "utils.h"

Container::Container(
    const std::string &distroName,
    const std::string &containerId,
    const std::string &rootDir
): distroName(distroName), containerId(containerId), rootDir(rootDir)
{
    char buffer[128];
    // Retrieves the name of the current user
    getlogin_r(buffer, 128);
    currentUser = std::string(buffer);
}

/**
 * Prepares and sets up the environment required for the container to
 * run correctly. Performs the following actions:
 * 1. Downloads and extracts the rootfs to the specified folder.
 * 2. Mounts the needed directories.
 * Returns true if set up succeeds, false otherwise.
 */
bool Container::setUp()
{
    try
    {
        setupContainerImage();
        mountDirectories();
        return true;
    }
    catch (std::exception& ex)
    {
        std::cout << "[ERROR] Failed to setup container: " << containerId << std::endl;
        std::cout << ex.what() << std::endl;
        return false;
    }
}

/**
 * Sets up the root file system of the container as per the specified linux distro and the
 * root directory. This function performs the following actions in order:
 * 1. Checks if the cache directory exists in the root directory. Creates it if it does not.
 * 2. Checks if the rootfs archive for the specified distro exists. Fetches it from the pre-defined
 * download URL if it is not present in the cache directory.
 * 3. Checks if there is a directory present for the container in rootDir/containers/. Creates one
 * for this container with the container ID as the directory name if it has not been created.
 * 4. Extracts the file system of the distro to the container's directory.
 */
void Container::setupContainerImage()
{
    std::string cacheDir = rootDir + "/cache/" + distroName;

    // Creates a cache directory to store the downloaded file systems if it does not exist
    if (!std::filesystem::exists(cacheDir))
    {
        if (std::filesystem::create_directories(cacheDir))
            std::cout << "Create cache directory " + cacheDir << ": SUCCESS" << std::endl;
        else
            throw std::runtime_error("[ERROR] Failed to create cache directory " + cacheDir);
    }

    Distro distro = stringToDistro[distroName];
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
        std::cout << "Rootfs for " + distroName + " does not exist." << std::endl;
        std::cout << "Downloading " + baseArchiveName + " from " + downloadUrl << std::endl;
        sprintf(buffer, "wget -O %s %s -q --show-progress", rootfsArchive.c_str(), downloadUrl.c_str());
        system(buffer);
    }

    containerDir = rootDir + "/containers/" + containerId;

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
 * Mounts the necessary directories (e.g. proc, sys, dev) before entering
 * the chroot jail. The commands to run are based on Arch Linux Wiki's guide
 * on using chroot.
 * https://wiki.archlinux.org/title/Chroot#Using_chroot
 */
void Container::mountDirectories()
{
    std::cout << "Mounting directories: proc, sys, dev" << std::endl;
    std::string mountProc = "mount -t proc /proc " + containerDir + "/proc";
    std::string mountSys = "mount -t sysfs /sys " + containerDir + "/sys";
    std::string mountDev = "mount --rbind /dev " + containerDir + "/dev";
    // If only --rbind is called the directory might be umountable
    // --make-rslave makes sure that any changes made in the container's dev will not propagate back
    std::string mountDevMakeRSlave = "mount --make-rslave " + containerDir + "/dev";
    std::string mountCommands[] = {
            mountProc,
            mountSys,
            mountDev,
            mountDevMakeRSlave
    };

    for (const auto& cmd : mountCommands)
    {
        int status = system(cmd.c_str());
        if (status != 0)
            throw std::runtime_error("[ERROR] Execute command '" + cmd + "': FAILED [" + std::to_string(status) + "]");
    }
    std::cout << "Mounting directories: SUCCESS" << std::endl;
}

/**
 * Starts a chroot jail for the container and sets up the mounts for
 * the current container.
 */
void Container::enterChrootJail()
{
    std::cout << "Entering chroot jail" << std::endl;

    int status = chroot(containerDir.c_str());
    if (status != 0)
        throw std::runtime_error("[ERROR] chroot " + containerDir + ": FAILED [" + std::to_string(status) + "]");
    std::cout << "Entering chroot jail: SUCCESS" << std::endl;
}


/**
 * Actions which are performed so as to create an isolated environment
 * for the container. The steps it takes are as follows:
 * 1. Enters chroot jail
 */
void Container::contain()
{
    enterChrootJail();
}

/**
 * Executes the given command string in the container with popen(), redirects
 * stderr and stdout, and captures the status after execution.
 * Implementation from:
 * https://stackoverflow.com/questions/52164723/how-to-execute-a-command-and-get-return-code-stdout-and-stderr-of-command-in-c
 * @param command: the command to be executed.
 */
void Container::execute(std::string& command)
{
    contain();

    std::array<char, 128> buffer{};

    std::cout << "Executing command: " << command << std::endl;

    // Directs the output of stderr to stdout
    // FIXME: the output from stderr and stdout might be unpredictably mixed
    if (!endsWith(command, "2>&1"))
        command += "2>&1";

    auto pipe = popen(command.c_str(), "r");

    if (!pipe) throw std::runtime_error("[ERROR] popen() failed!");
    std::cout << "Execution output:" << std::endl;
    while (!feof(pipe))
    {
        if (fgets(buffer.data(), 128, pipe) != nullptr)
            // result += buffer.data();
            std::cout << buffer.data();

        // std::cout << result;
    }

    int returnCode = pclose(pipe);

    std::cout << "Return code: " << returnCode << std::endl;
    exit(0);
}

/**
 * Unmounts the directories that have been mounted before entering the
 * chroot jail (e.g. proc, sys, dev).
 */
void Container::unmountDirectories()
{
    std::cout << "Unmounting directories: proc, sys, dev" << std::endl;
    std::string unmountProc = "umount -f " + containerDir + "/proc";
    std::string unmountSys = "umount -f " + containerDir + "/sys";
    // FIXME There has to be a better solution than using the -l flag
    std::string unmountDev = "umount -l " + containerDir + "/dev";
    std::string unmountCommands[] = {
            unmountProc,
            unmountSys,
            unmountDev
    };

    for (const auto& cmd : unmountCommands)
    {
        int status = system(cmd.c_str());
        if (status != 0)
            throw std::runtime_error("[ERROR] Execute command '" + cmd + "': FAILED [" + std::to_string(status) + "]");
    }
    std::cout << "Unmounting directories: SUCCESS" << std::endl;
}

/**
 * Removes the directory that contains the file system of the container.
 */
void Container::removeContainerDirectory()
{
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
 * Returns true if clean up succeeds, false otherwise.
 */
bool Container::cleanUp()
{
    try
    {
        unmountDirectories();
        removeContainerDirectory();
        return true;
    }
    catch (std::exception& ex)
    {
        std::cout << "[ERROR] Failed to clean up container: " << containerId << std::endl;
        std::cout << ex.what() << std::endl;
        return false;
    }
}
