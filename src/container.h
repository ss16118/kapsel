//
// Created by siyuan on 18/07/2021.
//

#ifndef CONTAINER_CPP_CONTAINER_H
#define CONTAINER_CPP_CONTAINER_H

#include <vector>
#include <utility>
#include <semaphore.h>

/**
 * A struct representing the resource constraints
 * inside the container.
 */
struct ResourceLimits
{
    std::string processNumber;
    int cpuShare;
    std::string memory;
    std::string swapMemory;
};


/**
 * A struct representing an individual container.
 */
struct Container
{
    pid_t pid;
    std::string distroName;
    std::string id;
    std::string rootDir;
    std::string dir;
    std::string rootfs;
    std::string currentUser;
    std::string command;
    std::pair<std::string, std::string> vEthPair;
    ResourceLimits* resourceLimits;
    sem_t* networkNsSemaphore;
    sem_t* networkInitSemaphore;
};

bool setUpContainer(Container* container);
bool cleanUpContainer(Container* container);
Container* createContainer(std::string& distroName,
                           std::string& containerId,
                           std::string& rootDir,
                           std::string& command,
                           ResourceLimits* resourceLimits);
void startContainer(Container* container);

#endif //CONTAINER_CPP_CONTAINER_H
