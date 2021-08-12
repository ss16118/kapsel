//
// Created by siyuan on 18/07/2021.
//

#ifndef CONTAINER_CPP_CONTAINER_H
#define CONTAINER_CPP_CONTAINER_H

#include <vector>

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
};

bool setUpContainer(Container* container);
bool cleanUpContainer(Container* container);
Container* createContainer(std::string& distroName, std::string& containerId, std::string& rootDir, std::string& command);
void startContainer(Container* container);

#endif //CONTAINER_CPP_CONTAINER_H
