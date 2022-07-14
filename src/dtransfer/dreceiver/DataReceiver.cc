/**
 *
 * Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
 * Distributed under the GNU GPL v2. For full terms see the file LICENSE.
 *
 */

#include "DataReceiver.hpp"

#include <sys/socket.h>  // socket(), bind(), recv()// , etc
#include <sys/types.h>
#include <sys/select.h>  // select()
#include <netinet/in.h>  // sockaddr_in
#include <arpa/inet.h>   // socklen_t, inet_pton
#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>       // O_RDONLY, S_IRWXU, S_IRUSR, etc
#include <cstdint>      // uint*_t
#include <cstring>       // memset()
#include <unistd.h>      // close()
#include <pthread.h>     // pthread_mutex_lock()
#include <sstream>       // std::stringstream
#include <string>        // std::string
#include <map>           // std::map
#include <algorithm>

#include "../../util/logfile.hpp"    // class LogFile and LOG_* macros

DataReceiver::DataReceiver() : DataTransfer("Rx"), stopFlag(false), nbytes_reset_value() {
    // create a mutex to protect the access to nbytes_reset_value
    pthread_mutexattr_t mattr;
    if (pthread_mutexattr_init(&mattr))
        LOG_FATAL_PERROR_EXIT("DataReceiver() pthread_mutexattr_init()");

    if (pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED))
        LOG_FATAL_PERROR_EXIT("DataReceiver() pthread_condattr_setpshared()");

}

void DataReceiver::stopThread() {
    //Call parent function
    DataTransfer::stopThread();

    //Clean the worker threads
    this->stopFlag = true;
}

void DataReceiver::readAndSetLogLevel(ConfigFile &cfile) {
    WiperfUtility::readAndSetLogLevel(cfile, std::string("data-receiver"));
}

void DataReceiver::readConfig(const std::string &configFname) {
    ConfigFile cfile(configFname);

    this->readAndSetLogLevel(cfile);

    this->portSrv = WiperfUtility::readPort(cfile, "data-receiver", PORT_SRV_DEF);
    WiperfUtility::readIfaces(cfile, "data-receiver", SERVER, ifaceMap);
    this->portCli = WiperfUtility::readPort(cfile, "data-sender", PORT_CLI_DEF);
    WiperfUtility::readIfaces(cfile, "data-sender", CLIENT, ifaceMap);

    // check that we have both cli and srv addresses for all ifaces
    // remove interfaces for which we don't have both addresses
    for (auto itr = this->ifaceMap.begin(); itr != this->ifaceMap.end();) {
        IfaceInfo &iinfo = itr->second;

        if (!iinfo.addrSrv.length() || !iinfo.addrCli.length())  // bad boy
            itr = this->ifaceMap.erase(itr);
        else
            ++itr;  // all good in the neighborhood
    }

    // do we have at least one interface pair?
    if (this->ifaceMap.empty()) {
        std::stringstream ss;
        ss << "Config exception: section=data-receiver/data-sender, value=ifaces."
           << " Don't have any matching receiver/sender interface pairs.";
        LOG_FATAL_EXIT(ss.str().c_str());
    }

    for (auto &itr: this->ifaceMap) {
        this->nbytes_reset_value.insert({itr.first, 0});
    }
}

void DataReceiver::addIfaceSocksToFdSet(fd_set *fdset) {
    // iterate over map and call FD_SET on each socket fd
    for (auto &itr: this->ifaceMap) {
        const int sockfd = (itr.second).sockfd;
        FD_SET(sockfd, fdset);
    }
}

void DataReceiver::commThread() {
    //Go over all interface addresses and create a socket for each of them

    for (auto &itr: this->ifaceMap) {
        std::string iname = itr.first;
        IfaceInfo &ifaceInfo = itr.second;
        std::string iaddrStr = ifaceInfo.addrSrv;

        // tell the world about what we're doing
        std::stringstream ss;
        ss << "Attaching interface " << iname << " @ " << iaddrStr << ":"
           << this->portSrv;
        LOG_MSG(ss.str().c_str());

        // create udp socket
        int sockfd;
        if ((sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
            LOG_FATAL_PERROR_EXIT("rthread socket()");
        }

        // make socket non blocking
        if (fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
            this->closeIfaceSocks();
            LOG_FATAL_PERROR_EXIT("rthread fcntl()");
        }

        // construct local address structure
        struct sockaddr_in sockaddr{}; // local address
        this->createSockaddr(iaddrStr, this->portSrv, &sockaddr);

        // bind to local address
        if (bind(sockfd, (struct sockaddr *) &sockaddr, sizeof(sockaddr)) < 0) {
            this->closeIfaceSocks();
            std::stringstream ss0;
            ss0 << "rthread bind() addr " << iaddrStr;
            LOG_FATAL_PERROR_EXIT(ss0.str().c_str());
        }

        // update interface details
        ifaceInfo.sockfd = sockfd;
        ifaceInfo.nbytesAcc = 0;

    } // iface sock creation loop end

    LOG_MSG("program up and running");

    for (auto &itr: this->ifaceMap) {
        IfaceInfo &iinfo = itr.second;
        std::string ifname = itr.first;

        this->workers.push_back(std::thread([&iinfo, ifname, this]() {
            char *rcvBuf = new char[RCV_BUF_LEN];
            ssize_t nbytes;

            fd_set fdset;
            int sockfd = iinfo.sockfd;
            int wakedf_ = this->wakefd_;

            int maxfd = std::max(sockfd, wakedf_) + 1;

            struct timeval timeout{};

            while(!this->stopFlag.load()) {
                FD_SET(sockfd, &fdset);
                FD_SET(wakedf_, &fdset);

                timeout.tv_sec = 0;
                timeout.tv_usec = 10;

                if (select(maxfd, &fdset, NULL, NULL, &timeout) == -1) {
                    this->closeIfaceSocks();
                    LOG_FATAL_PERROR_EXIT("rthread select()");
                }

                auto itr_nbytes = this->nbytes_reset_value.find(ifname);

                if (itr_nbytes->second > 0) {
                    iinfo.nbytesAcc = iinfo.nbytesAcc - itr_nbytes->second;
                    itr_nbytes->second = 0;
                }

                if (FD_ISSET(sockfd, &fdset)) {
                    while ((nbytes = recv(sockfd, rcvBuf, RCV_BUF_LEN, MSG_DONTWAIT)) > 0 && !this->stopFlag.load()) {
                        iinfo.nbytesAcc += nbytes; // add read bytes to stats
                    }
                }
            }

            delete [] rcvBuf;
        }));
    }

    for (auto & itr : workers) {
        itr.join();
    }

    // clean up and be done
    this->closeIfaceSocks();
}

