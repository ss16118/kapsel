#include <iostream>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <string>
#include "command_types.h"
#include "utils.h"

std::map<std::string, CommandType> stringToCommandType = {
        { "run", Run }
};

/**
 * Executes the given command string with popen(), and captures
 * Implementation from:
 * https://stackoverflow.com/questions/52164723/how-to-execute-a-command-and-get-return-code-stdout-and-stderr-of-command-in-c
 * @param command: the command to be executed.
 */
void execute(std::string& command)
{
    std::array<char, 128> buffer{};
    std::string result;

    std::cout << "Executing command: " << command << "..." << std::endl;

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
}



/**
 * Runs the given command in a containerized environment.
 * Forks a new process that will execute the given command whilst the
 * parent process waits for the child process to finish.
 */
void run(std::string& command)
{
    pid_t pid = fork();

    if (pid == 0)
    {
        // The child process
        try
        {
            execute(command);
        }
        catch (std::exception& ex)
        {
            std::cout << "[ERROR] An error occurred while executing command: " << std::endl;
        }
    }
    else
    {
        // If it is the parent process, waits for the child to finish
        wait(nullptr);
        std::cout << "Exit from container: SUCCESS" << std::endl;
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
        throw std::invalid_argument("[ERROR] There should be at least two arguments!");

    std::string commandTypeString(argv[1]);
    CommandType commandType = stringToCommandType[commandTypeString];

    //
    std::string commandComplete;
    for (int i = 2; i < argc; i ++)
    {
        commandComplete += argv[i];
        commandComplete += " ";
    }

    // Performs actions depending on the second argument. e.g. Run
    switch (commandType)
    {
        case Run:
            run(commandComplete);
            break;

        default:
            throw std::invalid_argument("[ERROR] Command " + commandTypeString + " not supported!");
    }
}
