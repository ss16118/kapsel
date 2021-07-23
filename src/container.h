//
// Created by siyuan on 18/07/2021.
//

#ifndef CONTAINER_CPP_CONTAINER_H
#define CONTAINER_CPP_CONTAINER_H

#include <vector>

class Container
{
private:
    const std::string& distroName;
    const std::string& containerId;
    const std::string& rootDir;
    std::string containerDir;
    std::string currentUser;

    // Set up functions
    void enterChrootJail();
    void mountDirectories();
    void setupContainerImage();

    // Clean up functions
    void unmountDirectories();
    void removeContainerDirectory();

    void contain();
public:
    explicit Container(const std::string& distroName, const std::string& containerId, const std::string& rootDir);
    void execute(std::string& command);
    bool setUp();
    bool cleanUp();
};

#endif //CONTAINER_CPP_CONTAINER_H
