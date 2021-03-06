//
// Created by siyuan on 07/07/2021.
//

#ifndef CONTAINER_CPP_CONSTANTS_H
#define CONTAINER_CPP_CONSTANTS_H
#include <map>
#include <set>

#define NETWORK_NS_SEM_NAME "/networkNsSemaphore"
#define NETWORK_INIT_SEM_NAME "/networkInitSemaphore"

enum CommandType {
    Run, List, Delete
};

extern std::map<std::string, CommandType> stringToCommandType;

// const std::string UBUNTU_IMAGE_URL = "http://cdimage.ubuntu.com/ubuntu-base/releases/18.04.4/release/ubuntu-base-18.04.5-base-amd64.tar.gz";
const std::string UBUNTU_IMAGE_URL = "http://cdimage.ubuntu.com/ubuntu-base/releases/20.04.2/release/ubuntu-base-20.04.1-base-amd64.tar.gz";
const std::string ALPINE_IMAGE_URL = "https://dl-cdn.alpinelinux.org/alpine/v3.14/releases/x86_64/alpine-minirootfs-3.14.0-x86_64.tar.gz";
const std::string CENTOS_IMAGE_URL = "https://github.com/Xiekers/rootfs/raw/master/centos-7-docker.tar.xz";
const std::string ARCH_IMAGE_URL = "https://github.com/Xiekers/rootfs/raw/master/archlinux.tar.xz";

const std::set<std::string> availableDistros = { "ubuntu", "alpine", "centos", "arch" };

extern std::map<std::string, std::string> stringToDownloadUrl;

const std::string CGROUP_FOLDER = "/sys/fs/cgroup";

// Networking related constants
const std::string BRIDGE_NAME = "kapsel";
const std::string BRIDGE_IP = "107.17.0.1";
const std::string DEFAULT_NAMESERVER = "8.8.8.8";

#endif //CONTAINER_CPP_CONSTANTS_H
