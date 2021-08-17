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

// const std::string UBUNTU_IMAGE_URL = "http://cdimage.ubuntu.com/ubuntu-base/releases/18.04.4/release/ubuntu-base-18.04.5-base-amd64.tar.gz";
const std::string UBUNTU_IMAGE_URL = "http://cdimage.ubuntu.com/ubuntu-base/releases/20.04.2/release/ubuntu-base-20.04.1-base-amd64.tar.gz";
const std::string ALPINE_IMAGE_URL = "https://dl-cdn.alpinelinux.org/alpine/v3.14/releases/x86_64/alpine-minirootfs-3.14.0-x86_64.tar.gz";
const std::string CENTOS_IMAGE_URL = "https://github.com/Xiekers/rootfs/raw/master/centos-7-docker.tar.xz";
const std::string ARCH_IMAGE_URL = "https://github.com/Xiekers/rootfs/raw/master/archlinux.tar.xz";

const std::set<std::string> availableDistros = { "ubuntu", "alpine", "centos", "arch" };

extern std::map<std::string, std::string> stringToDownloadUrl;

#endif //CONTAINER_CPP_CONSTANTS_H
