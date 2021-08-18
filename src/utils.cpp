//
// Created by siyuan on 17/07/2021.
//

#include <string>
#include <random>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <arpa/inet.h>
#include <array>
#include <utility>

/**
 * Checks if a string ends with the given suffix. Returns true if it does, false otherwise.
 * Implementation from:
 * https://stackoverflow.com/questions/874134/find-out-if-string-ends-with-another-string-in-c
 *
 * @param fullString the string whose ending will be checked.
 * @param suffix the potential suffix of the full string.
 */
bool endsWith(const std::string& fullString, const std::string& suffix)
{
    if (fullString.length() >= suffix.length())
    {
        return (0 == fullString.compare (fullString.length() - suffix.length(), suffix.length(), suffix));
    }
    return false;
}

/**
 * Generates a random alphanumeric ID for a container. The ID is
 * a string whose default length is 12.
 * Implementation based on:
 * https://stackoverflow.com/questions/440133/how-do-i-create-a-random-alpha-numeric-string-in-c
 *
 * @return a randomly generated container ID with the given length;
 */
std::string generateContainerId(size_t length)
{
    std::string randomId;
    randomId.reserve(length);

    static const char charset[] =
            "0123456789"
            "abcdefghijklmnopqrstuvwxyz";

    std::random_device rd;  // Will be used to obtain a seed for the random number engine
    std::mt19937 gen(rd()); // Standard mersenne_twister_engine seeded with rd()
    std::uniform_int_distribution<> pick(0, sizeof(charset) - 2);

    for (int i = 0; i < length; i++)
        randomId += charset[pick(gen)];

    return randomId;
}


/**
 * Appends the given text to the file specified by the given file path.
 * @return if action has been successful, returns true. Otherwise, returns false.
 */
bool appendToFile(const std::string filePath, const std::string text)
{
    std::ofstream file;
    file.open(filePath, std::ios::out | std::ios::app);
    if (file.fail())
        return false;

    file << text;
    file.close();
    return true;
}


/**
 * Increments the given IPv4 address represented as a C++ string by
 * a number that equals to the value of 'iter'.
 *
 * Implementation from:
 * https://stackoverflow.com/questions/1505676/how-do-i-increment-an-ip-address-represented-as-a-string
 * @return the incremented IPv4 address.
 */
std::string getNextIp(std::string baseIp, const int iter = 1)
{
    std::string nextIp = std::move(baseIp);
    for (int i = 0; i < iter; i ++)
    {
        // convert the input IP address to an integer
        in_addr_t address = inet_addr(nextIp.c_str());

        // add one to the value (making sure to get the correct byte orders)
        address = ntohl(address);
        address += 1;
        address = htonl(address);

        // pack the address into the struct inet_ntoa expects
        struct in_addr address_struct;
        address_struct.s_addr = address;

        // convert back to a string
        nextIp = std::string(inet_ntoa(address_struct));
    }
    return nextIp;
}

/**
 * A wrapper around the 'system()' function so that the stdout is captured
 * and returned.
 * Implementation from:
 * https://stackoverflow.com/questions/52164723/how-to-execute-a-command-and-get-return-code-stdout-and-stderr-of-command-in-c
 *
 * @param command the command to be execute.
 * @throw runtime error if execution of the given command fails.
 * @return the stdout from running the given command in the form of a C++ string.
 */
std::string systemWithOutput(const std::string& command)
{
    std::array<char, 128> buffer{};
    std::string result;

    auto pipe = popen(command.c_str(), "r"); // get rid of shared_ptr

    if (!pipe) throw std::runtime_error("popen() failed!");

    while (!feof(pipe)) {
        if (fgets(buffer.data(), 128, pipe) != nullptr)
            result += buffer.data();
    }

    auto status = pclose(pipe);

    if (status == EXIT_SUCCESS)
        return std::string(result);
    else
        throw std::runtime_error("[ERROR] Execution of command " + command + " exited with " + std::to_string(status));
}