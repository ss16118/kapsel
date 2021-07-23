//
// Created by siyuan on 07/07/2021.
//

#ifndef CONTAINER_CPP_CONSTANTS_H
#define CONTAINER_CPP_CONSTANTS_H
#include <map>
#include <set>

enum CommandType {
    Run
};

extern std::map<std::string, CommandType> stringToCommandType;

const std::string UBUNTU_IMAGE_URL = "http://cdimage.ubuntu.com/ubuntu-base/releases/18.04.4/release/ubuntu-base-18.04.5-base-amd64.tar.gz";
const std::string ALPINE_IMAGE_URL = "https://dl-cdn.alpinelinux.org/alpine/v3.14/releases/x86_64/alpine-minirootfs-3.14.0-x86_64.tar.gz";

enum Distro {
    Ubuntu,
    Alpine
};

const std::set<std::string> availableDistros = { "ubuntu", "alpine" };

extern std::map<std::string, Distro> stringToDistro;

#endif //CONTAINER_CPP_CONSTANTS_H
