//
// Created by siyuan on 07/07/2021.
//

#ifndef CONTAINER_CPP_COMMAND_TYPES_H
#define CONTAINER_CPP_COMMAND_TYPES_H
#include <map>

enum CommandType {
    Run
};

extern std::map<std::string, CommandType> stringToCommandType;

#endif //CONTAINER_CPP_COMMAND_TYPES_H
