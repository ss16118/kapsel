#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <time.h>
#include <sys/stat.h>
#include <loguru/loguru.hpp>
#include <cxxopts/cxxopts.hpp>

#include "container.h"
#include "constants.h"
#include "utils.h"

std::map<std::string, CommandType> stringToCommandType = {
        { "run", Run },
        { "ls", List },
        { "list", List },
        { "rm", Delete },
        { "remove", Delete },
        { "delete", Delete }
};


/**
 * Fetches a list of container images from the directory <root-dir>/images/.
 * Returns the images as a vector of 'Image' structs, which contain the
 * relevant data.
 */
std::vector<Image> getContainerImages(const std::string& rootDir)
{
    std::vector<Image> images;
    for (const auto& archive : std::filesystem::recursive_directory_iterator(rootDir + "/images"))
    {
        std::string imageId = split(archive.path().filename(), ".")[0];
        // Obtains the time at which the file was last modified
        struct stat attr{};
        stat(archive.path().c_str(), &attr);
        std::string lastModified(trimEnd(ctime(&attr.st_mtime)));

        uintmax_t fileSize = std::filesystem::file_size(archive);
        images.emplace_back(Image { imageId, fileSize, lastModified });
    }
    return images;
}



/**
 * Executes the given command in a containerized environment as per the specified parameters.
 * If 'buildImage' is true, a
 */
void run(std::string rootDir,
         std::string containerId,
         std::string& distroName,
         std::string command,
         ResourceLimits* resourceLimits,
         bool buildImage)
{
    Container* container =
            createContainer(distroName, containerId, rootDir, command, resourceLimits, buildImage);
    if (setUpContainer(container))
    {
        startContainer(container);
    }
    cleanUpContainer(container);
}

/**
 * Displays the all the container images which have been built
 * and other relevant information.
 */
void list(const std::string& rootDir)
{
    printf("%4s  %20s  %10s  %30s\n", "#", "Image ID", "Size", "Last Modified");
    int count = 0;
    for (const auto& image : getContainerImages(rootDir))
    {
        std::string fileSize = getHumanReadableFileSize(image.fileSize);
        printf("%4d  %20s  %10s  %30s\n", count, image.id.c_str(), fileSize.c_str(), image.lastModified.c_str());
        count++;
    }
}

/**
 * Removes the container images (tarballs) specified by the vector of containerIds
 * from the directory <root-dir>/images/.
 */
void remove(const std::string& rootDir, const std::vector<std::string>& imageIds)
{
    std::filesystem::path imageDir(rootDir + "/images");
    for (const auto& imageId : imageIds)
    {
        auto imagePath = imageDir / (imageId + ".tar.gz");
        if (!std::filesystem::exists(imagePath))
        {
            std::cout << "Image with ID " << imageId << " does not exist" << std::endl;
        }
        else
        {
            if (std::filesystem::remove(imagePath))
                std::cout << "Removed image with ID " << imageId << std::endl;
            else
                std::cout << "Failed to remove image with ID " << imageId << ": [Errno " << errno << "]" << std::endl;
        }
    }
}


int main(int argc, char* argv[])
{
    // Sets up argument parsing
    cxxopts::Options options(argv[0], "Linux container implemented in C++");
    options.positional_help("[cmd-type] [args]").show_positional_help();
    options.set_width(80).set_tab_expansion().add_options()
            // Container parameters
            ("t,rootfs",
             R"(The root file system for the container. Current options are {"ubuntu", "alpine", "arch", "centos"}.)",
             cxxopts::value<std::string>()->default_value("ubuntu"))
            ("i,container-id", "Specify the ID that will be given to the container.", cxxopts::value<std::string>())
            ("r,root-dir", "The directory where all Kapsel related files will be stored.",
                    cxxopts::value<std::string>()->default_value("../res"))
            ("b,build", "Build an image of the container after exiting.")

            // Resource limits
            ("p,process-number", R"(The maximum number of processes can be created in the container. Use "max" to remove limit.)",
                    cxxopts::value<std::string>()->default_value("20"))
            ("c,cpu-share", "The relative share of CPU time available for the container.",
                    cxxopts::value<int>()->default_value("512"))
            ("m,memory", "The user memory limit of the container. Use -1 to remove limit.",
                    cxxopts::value<std::string>()->default_value("256m"))
            ("s,memory-swap", "The maximum amount for the sum of memory and swap usage in the container. Use -1 to remove limit.",
                    cxxopts::value<std::string>()->default_value("512m"))

            // Logging
            ("l,logging", "Enable logging to a log file.")

            ("cmd-type", "Type of actions to perform. Available options are {'run', 'list', 'delete'}.\n"
                         "run   : executes the preceding command inside a container.\n"
                         "list  : lists the container images which have been built.\n"
                         "delete: remove the container images which have the preceding list of IDs.",
             cxxopts::value<std::string>())

            ("args", "The arguments that will passed to command type <cmd-type>. "
                     "For instance, when <cmd-type> is 'run', args will function as the command to be executed "
                     "in the container; when <cmd-type> is 'delete', args will be a list of image IDs of the images"
                     "to be deleted.",
                     cxxopts::value<std::vector<std::string>>()->default_value(""))

            ("h,help", "Print arguments and their descriptions.")
            ;
    options.parse_positional({"cmd-type", "args"});

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

        auto& args = parsedOptions["args"].as<std::vector<std::string>>();

        std::string distroName = parsedOptions["rootfs"].as<std::string>();
        if (!availableDistros.count(distroName))
            throw std::invalid_argument("[ERROR] Root file system " + distroName + " is not an option!");

        std::string containerId;
        if (parsedOptions.count("container-id"))
            containerId = parsedOptions["container-id"].as<std::string>();
        else
            containerId = generateContainerId();

        // Resource constraints
        auto* resourceLimits = new ResourceLimits;
        resourceLimits->processNumber = parsedOptions["process-number"].as<std::string>();
        resourceLimits->cpuShare = parsedOptions["cpu-share"].as<int>();
        resourceLimits->memory = parsedOptions["memory"].as<std::string>();
        resourceLimits->swapMemory = parsedOptions["memory-swap"].as<std::string>();

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
            {
                std::ostringstream command;
                std::copy(args.begin(), args.end(), std::ostream_iterator<std::string>(command, " "));
                run(rootDir, containerId, distroName, command.str(), resourceLimits,
                    parsedOptions["build"].as<bool>());
                break;
            }
            case List:
                list(rootDir);
                break;

            case Delete:
                remove(rootDir, args);
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
