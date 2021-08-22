//
// Created by siyuan on 17/07/2021.
//

#ifndef CONTAINER_CPP_UTILS_H
#define CONTAINER_CPP_UTILS_H
#include <cmath>

/**
 * Converts file size to human readable form
 * Implementation from:
 * https://en.cppreference.com/w/cpp/filesystem/file_size
 */
struct HumanReadableFileSize {
    std::uintmax_t size{};
private: friend
    std::ostream& operator<<(std::ostream& os, HumanReadableFileSize hr) {
        int i{};
        double mantissa = hr.size;
        for (; mantissa >= 1024.; mantissa /= 1024., ++i) { }
        mantissa = std::ceil(mantissa * 10.) / 10.;
        os << mantissa << "BKMGTPE"[i];
//        return i == 0 ? os : os << "B (" << hr.size << ')';
        return i == 0 ? os : os << "B";
    }
};

bool endsWith(const std::string& fullString, const std::string& suffix);
std::string generateContainerId(size_t length = 12);
bool appendToFile(std::string filePath, std::string text);
std::string getNextIp(std::string baseIp, int iter);
std::string systemWithOutput(const std::string& command);
std::vector<std::string> split(std::string text, std::string delimiter);
std::string getHumanReadableFileSize(std::uintmax_t size);
std::string trimEnd(std::string text);
#endif //CONTAINER_CPP_UTILS_H
