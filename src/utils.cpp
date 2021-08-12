//
// Created by siyuan on 17/07/2021.
//

#include <string>
#include <random>
#include <unistd.h>
#include <sstream>
#include <fstream>

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
