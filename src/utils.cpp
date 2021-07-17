//
// Created by siyuan on 17/07/2021.
//

#include <string>

/**
 * Checks if a string ends with the given suffix. Returns true if it does, false otherwise.
 * Implementation from:
 * https://stackoverflow.com/questions/874134/find-out-if-string-ends-with-another-string-in-c
 *
 * @param fullString: the string whose ending will be checked.
 * @param suffix: the potential suffix of the full string.
 */
bool endsWith(const std::string& fullString, const std::string& suffix)
{
    if (fullString.length() >= suffix.length())
    {
        return (0 == fullString.compare (fullString.length() - suffix.length(), suffix.length(), suffix));
    }
    return false;
}