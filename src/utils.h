//
// Created by siyuan on 17/07/2021.
//

#ifndef CONTAINER_CPP_UTILS_H
#define CONTAINER_CPP_UTILS_H

bool endsWith(const std::string& fullString, const std::string& suffix);
std::string generateContainerId(size_t length = 12);
bool appendToFile(std::string filePath, std::string text);
std::string getNextIp(std::string baseIp, int iter);
std::string systemWithOutput(const std::string& command);

#endif //CONTAINER_CPP_UTILS_H
