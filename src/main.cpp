#include <iostream>
#include <vector>
#include <unistd.h>
#include <string>
#include <filesystem>
#include <cxxopts/cxxopts.hpp>
#include <wait.h>

#include "container.h"
#include "constants.h"
#include "utils.h"

std::map<std::string, CommandType> stringToCommandType = {
        { "run", Run }
};

std::map<std::string, Distro> stringToDistro = {
        { "ubuntu", Ubuntu },
        { "alpine", Alpine }
};

/**
 * Runs the given command in a containerized environment.
 * Forks a new process that will execute the given command whilst the
 * parent process waits for the child process to finish.
 */
void run(std::string containerId, std::string& distroName, std::string command)
{
    if (containerId.empty())
        containerId = generateContainerId();

    std::string rootDir = "../res";

    Container* container = createContainer(distroName, containerId, rootDir, command);
    if (setUpContainer(container))
    {
        startContainer(container);
    }
    cleanUpContainer(container);

//    // If container set up fails, returns immediately
//    if (!container.setUp())
//        return;
//
//    pid_t pid = fork();
//
//    if (pid == 0)
//    {
//        // The child process
//        try
//        {
//            // Initializes a container
//            container.start(command);
//        }
//        catch (std::exception& ex)
//        {
//            std::cout << "[ERROR] An error occurred while executing command: " << command << std::endl;
//            std::cout << ex.what() << std::endl;
//        }
//    }
//    else
//    {
//        // If it is the parent process, waits for the child to finish
//        wait(nullptr);
//        std::cout << "Exit from container: SUCCESS" << std::endl;
//        container.cleanUp();
//    }
}


int main(int argc, char* argv[])
{
    // Sets up argument parsing
    cxxopts::Options options(argv[0], "Linux container implemented in C++");
    options.positional_help("[cmd-type] [cmd]").show_positional_help();
    options.set_width(80).set_tab_expansion().add_options()
            ("t,rootfs",
             R"(The root file system for the container. Current options are {"ubuntu", "alpine"}.)",
             cxxopts::value<std::string>()->default_value("ubuntu"))

            ("i,container-id", "The ID that will be given to the container.",
                    cxxopts::value<std::string>())

            ("cmd-type", R"(Type of actions to perform. Available options are {"run"}.)",
             cxxopts::value<std::string>())

            ("cmd", "The command to be executed in a containerized environment.",
             cxxopts::value<std::vector<std::string>>()->default_value("/bin/sh"))

            ("h,help", "Print arguments and their descriptions")
            ;
    options.parse_positional({"cmd-type", "cmd"});

    try
    {
        auto parsedOptions = options.parse(argc, argv);
        if (parsedOptions.count("help"))
        {
            std::cout << options.help() << std::endl;
            return 0;
        }

        if(!parsedOptions.count("cmd-type"))
            throw std::invalid_argument(R"([ERROR] You have to enter a command type! (e.g. "run"))");

        std::string commandTypeString = parsedOptions["cmd-type"].as<std::string>();
        CommandType commandType = stringToCommandType[commandTypeString];

        auto& commandArgs = parsedOptions["cmd"].as<std::vector<std::string>>();

        std::ostringstream command;
        std::copy(commandArgs.begin(), commandArgs.end(), std::ostream_iterator<std::string>(command, " "));

        std::string distroName = parsedOptions["rootfs"].as<std::string>();
        if (!availableDistros.count(distroName))
            throw std::invalid_argument("[ERROR] Root file system " + distroName + " is not an option!");

        std::string containerId;
        if (parsedOptions.count("container-id"))
            containerId = parsedOptions["container-id"].as<std::string>();

        // Performs actions depending on the second argument. e.g. Run
        switch (commandType)
        {
            case Run:
                run(containerId, distroName, command.str());
                break;

            default:
                throw std::invalid_argument("[ERROR] Command " + commandTypeString + " not supported!");
        }
    }
    catch (std::exception& e)
    {
        std::cout << e.what() << std::endl;
        return 1;
    }
}
