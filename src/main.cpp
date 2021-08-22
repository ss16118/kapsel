#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <loguru/loguru.hpp>
#include <cxxopts/cxxopts.hpp>

#include "container.h"
#include "constants.h"
#include "utils.h"

std::map<std::string, CommandType> stringToCommandType = {
        { "run", Run }
};

/**
 * Runs the given command in a containerized environment.
 */
void run(std::string rootDir, std::string containerId, std::string& distroName, std::string command)
{
    Container* container = createContainer(distroName, containerId, rootDir, command);
    if (setUpContainer(container))
    {
        startContainer(container);
    }
    cleanUpContainer(container);
}


int main(int argc, char* argv[])
{
    // Sets up argument parsing
    cxxopts::Options options(argv[0], "Linux container implemented in C++");
    options.positional_help("[cmd-type] [cmd]").show_positional_help();
    options.set_width(80).set_tab_expansion().add_options()
            // Container parameters
            ("t,rootfs",
             R"(The root file system for the container. Current options are {"ubuntu", "alpine"}.)",
             cxxopts::value<std::string>()->default_value("ubuntu"))
            ("i,container-id", "The ID that will be given to the container.",
                    cxxopts::value<std::string>())
            ("r,root-dir", "The directory where all Kapsel related files will be stored",
                    cxxopts::value<std::string>()->default_value("../res"))
            // Logging
            ("l,logging", "Enable logging to a log file", cxxopts::value<bool>()->default_value("true"))


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

        std::string rootDir = parsedOptions["root-dir"].as<std::string>();

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
        else
            containerId = generateContainerId();

        // Enables logging
        loguru::g_stderr_verbosity = loguru::Verbosity_ERROR;
        if (parsedOptions["logging"].as<bool>())
        {
            std::string logFile = rootDir + "/logs/" + containerId + ".log";
            loguru::add_file(logFile.c_str(), loguru::Truncate, loguru::Verbosity_MAX);
        }

        // Performs actions depending on the argument 'cmd-type'. e.g. Run
        switch (commandType)
        {
            case Run:
                run(rootDir, containerId, distroName, command.str());
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
