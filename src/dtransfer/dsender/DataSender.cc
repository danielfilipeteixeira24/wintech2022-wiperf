/**
 *
 * Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
 * Distributed under the GNU GPL v2. For full terms see the file LICENSE.
 *
 */

#include <arpa/inet.h>   // socklen_t, inet_pton
#include <fcntl.h>       // O_RDONLY, S_IRWXU, S_IRUSR, etc
#include <pthread.h>     // pthread_mutex_lock()
#include <sys/mman.h>    // mmap()
#include <cstdlib>  // std::rand()
#include <map>      // std::map
#include <sstream>  // std::stringstream
#include <string>   // std::string
#include <chrono>   // getting the current millis
#include <random>   // c++11 random library
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../../util/configfile.hpp"         // class ConfigFile
#include "../../util/logfile.hpp"    // class LogFile and LOG_* macros
#include "DataSender.hpp"

DataSender::DataSender() : DataTransfer("Tx"), decisionLevel(0),
                           decision(), decisionExpiresAt(0), /*decisionMaker(),*/ stopFlag(false) {}

void DataSender::stopThread() {
    DataTransfer::stopThread();

    this->stopFlag = true;
}

void DataSender::readConfig(const std::string &configFname) {
    ConfigFile cfile(configFname);

    this->readAndSetLogLevel(cfile);

    // read server port and interfaces
    this->portSrv = WiperfUtility::readPort(cfile, "data-receiver", PORT_SRV_DEF);
    WiperfUtility::readIfaces(cfile, "data-receiver", SERVER, ifaceMap);

    // read client port and interfaces
    this->portCli = WiperfUtility::readPort(cfile, "data-sender", PORT_CLI_DEF);
    WiperfUtility::readIfaces(cfile, "data-sender", CLIENT, ifaceMap);

    this->gpsShmPath = WiperfUtility::readGpsShmPath(cfile, GPS_SHM_PATH_DEF);
    // configure the decision maker and its database

    // set the decision level
    this->decisionLevel = std::stoi(cfile.Value("data-sender", "decision-level"));

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
           << " Don't have any matching sender/receiver interface pairs.";
        LOG_FATAL_EXIT(ss.str().c_str());
    }
}

void DataSender::readAndSetLogLevel(ConfigFile &cfile) {
    // call generic method in superclass
    WiperfUtility::readAndSetLogLevel(cfile, "data-sender");
}

std::string DataSender::pickRandomIface() {
    std::default_random_engine defEngine{123123123};
    std::uniform_int_distribution<int> intDistro(0, (int) this->ifaceMap.size());
    int idx = intDistro(defEngine);

    auto itr = this->ifaceMap.begin();
    for (int i = 0; i < idx; ++itr, i++);  // advance iterator as needed

    return itr->first;
}

void DataSender::sendEveryInterface() {
    for (auto &entry: this->ifaceMap) {
        std::string ifname = entry.first;
        IfaceInfo &iinfo = entry.second;

        this->workers.push_back(std::thread([&iinfo, ifname, this]() {
            char sndBuf[SND_BUF_LEN];
            int sndBufSize = sizeof(sndBuf);

            while(!this->stopFlag.load()) {
                //If the decision has expired, make a new one
                int ret = sendto(iinfo.sockfd, sndBuf, sndBufSize, MSG_DONTWAIT | MSG_DONTROUTE, /*MSG_DONTROUTE,*/
                       (const struct sockaddr *) &iinfo.sockaddrSrv, sizeof(iinfo.sockaddrSrv));

                if (ret < 0 && errno != 11) {
                    std::stringstream ss;
                    ss << "Error: " << errno << " :: " << std::strerror(errno);
                    std::cout << ss << std::endl;

                    LOG_ERR(ss.str().c_str());
                }
            }
        }));
    }

    for (auto &itr : this->workers) {
        itr.join();
    }

    this->closeIfaceSocks();
}

void DataSender::sendOneInterface() {
    uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

    char sndBuf[SND_BUF_LEN];
    int sndBufSize = sizeof(sndBuf);

    this->decision = this->pickRandomIface();
    this->decisionExpiresAt = 0;
    IfaceInfo &iinfo = this->ifaceMap[this->decision];

    while (!endProgram_) {
        //If the decision has expired, make a new one
        if (now > this->decisionExpiresAt) {
            this->decision = this->pickRandomIface();

            iinfo = this->ifaceMap[this->decision];
            this->decisionExpiresAt = now + 333; //TODO: pass this to the configuration or to the header
        }

        if (sendto(iinfo.sockfd, sndBuf, sndBufSize, MSG_DONTWAIT | MSG_DONTROUTE, /*MSG_DONTROUTE,*/
                   (const struct sockaddr *) &iinfo.sockaddrSrv, sizeof(iinfo.sockaddrSrv)) == -1) {
            //this->closeIfaceSocks();
            //LOG_FATAL_PERROR_EXIT("rthread sendto()");
            //break;
        }
    }
}

/**
* Receives data, and keeps track of how much it receives.<???>
*/
void DataSender::commThread() {
    // go over all interface addresses and create a socket for each of them
    IfaceInfoMap &ifaceMap = this->ifaceMap;
    for (auto &itr: ifaceMap) {
        std::string iname = itr.first;
        IfaceInfo &iinfo = itr.second;

        // tell the world about what we're doing
        std::stringstream ss;
        ss << "Attaching interface " << iname << " @ " << iinfo.addrCli << ":"
           << this->portCli;
        LOG_MSG(ss.str().c_str());

        // create udp socket
        int sockfdCli;
        if ((sockfdCli = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
            LOG_FATAL_PERROR_EXIT("sthread socket()");
        }

        int enable = 1;
        if (setsockopt(sockfdCli, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
            LOG_FATAL_PERROR_EXIT("sthread setsockopt()");
        }

        if (setsockopt(sockfdCli, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) < 0) {
            LOG_FATAL_PERROR_EXIT("sthread setsockopt()");
        }

        // build client (sender) address structure
        struct sockaddr_in sockaddrCli{};
        this->createSockaddr(iinfo.addrCli, this->portCli, &sockaddrCli);

        // bind to local address
        if (bind(sockfdCli, (struct sockaddr *) &sockaddrCli, sizeof(sockaddrCli)) < 0) {
            this->closeIfaceSocks();
            std::stringstream ss0;
            ss0 << "sthread bind() addr " << iinfo.addrCli;
            LOG_FATAL_PERROR_EXIT(ss0.str().c_str());
        }

        if (connect(sockfdCli, (struct sockaddr *) &sockaddrCli, sizeof(sockaddrCli)) < 0) {
            /* is non-blocking, so we don't get error at that point yet */
            if (EINPROGRESS != errno) {
                this->closeIfaceSocks();
                LOG_FATAL_PERROR_EXIT("sthread connect()");
            }
        }

        // save new interface details
        iinfo.sockfd = sockfdCli;
        iinfo.nbytesAcc = 0;

        // build server (receiver) address structure
        this->createSockaddr(iinfo.addrSrv, this->portSrv, &iinfo.sockaddrSrv);

    }  // iface sock creation loop end

    LOG_MSG("program up and running");

    // main server loop

    if (this->decisionLevel == 0) {
        sendEveryInterface();
    } else {
        sendOneInterface();
    }

    // clean up and be done
    this->closeIfaceSocks();
}
