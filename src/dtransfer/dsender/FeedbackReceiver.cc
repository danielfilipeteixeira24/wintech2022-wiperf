/**
 *
 * Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
 * Distributed under the GNU GPL v2. For full terms see the file LICENSE.
 *
 */

#include <fcntl.h>        // O_RDONLY, S_IRWXU, S_IRUSR, etc
#include <sys/mman.h>     // mmap()
#include <sys/socket.h>   // sockaddr
#include <sys/types.h>
#include <map>      // std::map
#include <string>   // std::string
#include <iostream>
#include <cstring>  // std::memcpy
#include <unistd.h> // ftruncate
#include <algorithm> //std::fill
#include <iomanip> 	 //std::hex std::setfill std::setw

#include <linux/wireless.h>

#include "FeedbackReceiver.hpp"

FeedbackReceiver::FeedbackReceiver() : DataTransfer("FeedRx"), databaseManager(),
            feedbackInterval(FEEDBACK_INTERVAL_DEF), dataSenderIfaces() {}

void FeedbackReceiver::readAndSetLogLevel(ConfigFile &cfile) {
    WiperfUtility::readAndSetLogLevel(cfile, std::string("feedback-receiver"));
}

void FeedbackReceiver::readConfig(const std::string& configFname) {
    ConfigFile cfile(configFname);

    this->readAndSetLogLevel(cfile);

    this->portSrv = WiperfUtility::readPort(cfile, "feedback-receiver", PORT_FEED_SRV_DEF);
    WiperfUtility::readIfaces(cfile, "feedback-receiver", SERVER, this->ifaceMap);
    this->portCli = WiperfUtility::readPort(cfile, "feedback-sender", PORT_FEED_CLI_DEF);
    WiperfUtility::readIfaces(cfile, "feedback-sender", CLIENT, this->ifaceMap);

    WiperfUtility::readIfaces(cfile, "data-sender", CLIENT, this->dataSenderIfaces);
    WiperfUtility::readIfaces(cfile, "data-receiver", SERVER, this->dataSenderIfaces);

    this->dataSenderIfnames = WiperfUtility::readIfnames(cfile, "data-sender");

    this->gpsShmPath = WiperfUtility::readGpsShmPath(cfile, GPS_SHM_PATH_DEF);

    // Instantiate database manager
    this->databaseManager.configure(cfile);

    // configure the feedback interval
    this->feedbackInterval = FEEDBACK_INTERVAL_DEF;
    try {
        this->feedbackInterval = std::stoi(cfile.Value("feedback-receiver", "feedback-interval"));
    } catch (char const* str) {
        std::stringstream ss;
        ss << "Config exception: section=feedback-receiver, value=feedback-interval " << str
        << " using default value " << this->feedbackInterval;
        LOG_ERR(ss.str().c_str());
    }

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
        ss << "Config exception: section=feedback-receiver/feedback-sender, value=ifaces."
           << " Don't have any matching receiver/sender interface pairs.";
        LOG_FATAL_EXIT(ss.str().c_str());
    }
}

int FeedbackReceiver::initializeInterfaceSockets() {
    //Go over all interface addresses and create a socket for each of them
    int maxfd = wakefd_; // will hold largest fd value at the end of the loop
    IfaceInfoMap &ifaceMap = this->ifaceMap;

    for (auto &itr : ifaceMap) {
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
        if (fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK) == -1) {
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

        // do we have a new maxfd?
        if (sockfd > maxfd) maxfd = sockfd;

        // update interface details
        ifaceInfo.sockfd = sockfd;
        ifaceInfo.nbytesAcc = 0;

    } // iface sock creation loop end

    return maxfd;
}

std::vector<DatabaseInfo> FeedbackReceiver::readFeedbackMessage(uint8_t* buffer, GpsInfo* gpsInfo) {
    GpsInfo currentInfo = WiperfUtility::getCurrentGps(gpsInfo);

    double latitude = static_cast<double>(currentInfo.lat);
    double longitude = static_cast<double>(currentInfo.lon);
    double speed = static_cast<double>(currentInfo.speed);
    double orientation = static_cast<double>(currentInfo.head);
    //If vehicle is moving at less than 0.5 kmph, we consider that it stopped?
    int moving = speed > 0.5;
    //uint64_t timestamp = gpsInfo->systime;

    uint32_t netNumberOfRats = 0;
	std::memcpy(&netNumberOfRats, &buffer[0], 4);

    uint32_t numberOfRats = ntohl(netNumberOfRats);

    std::vector<DatabaseInfo> feedbackInformation;

    for (uint32_t i = 0; i < numberOfRats; i++) {
        uint32_t offset = 4 + i * 40;

        uint32_t netIfaceId;
        std::memcpy(&netIfaceId, &buffer[offset], sizeof(uint32_t));
        uint32_t ifaceId = ntohl(netIfaceId);

        std::string ifaceName = this->dataSenderIfnames.at(i);
        IfaceInfo ifaceInfo = this->ifaceMap[ifaceName];

        // iterate over the 3 entries (t, t-1, and t-2)
        for (int j = 0; j < 3; j++) {
            uint32_t entryOffset = offset + 4 + j * 12;

            uint64_t netTimestamp;
            std::memcpy(&netTimestamp, &buffer[entryOffset], sizeof(uint64_t));
            uint64_t timestamp = WiperfUtility::ntohll(netTimestamp);

            if (timestamp > 0) { //t-2 and t-1 may not be present
                uint32_t netThroughput;
                std::memcpy(&netThroughput, &buffer[entryOffset + sizeof(uint64_t)], sizeof(uint32_t));

                DatabaseInfo databaseInfo;

                databaseInfo.latitude = latitude;
                databaseInfo.longitude = longitude;
                databaseInfo.speed = speed;
                databaseInfo.orientation = orientation;
                databaseInfo.moving = moving;

                databaseInfo.throughput = ntohl(netThroughput);
                databaseInfo.numBits = databaseInfo.throughput * this->feedbackInterval;
                databaseInfo.channelInfo;
                databaseInfo.scanInfo;
                databaseInfo.rat = ifaceName;
                databaseInfo.timestamp = timestamp;

                databaseInfo.tx_bitrate = 0;
                databaseInfo.signal_strength = 0;

                feedbackInformation.push_back(databaseInfo);
            }
        }
    }

    return feedbackInformation;
}

void FeedbackReceiver::commThread() {
    //int maxfd = this->initializeInterfaceSockets();
    GpsInfo *gpsInfo = WiperfUtility::getGpsInfo(this->gpsShmPath);

    //Go over all interface addresses and create a socket for each of them
    int maxfd = wakefd_; // will hold largest fd value at the end of the loop

    for (auto & itr : this->ifaceMap) {
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
        if (fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK) == -1) {
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

        // do we have a new maxfd?
        if (sockfd > maxfd) maxfd = sockfd;

        // update interface details
        ifaceInfo.sockfd = sockfd;
        ifaceInfo.nbytesAcc = 0;

    } // iface sock creation loop end

    LOG_MSG("program up and running");

    // main server loop
    uint8_t rcvBuf[FEEDBACK_RCV_BUF_LEN];
    fd_set sockfdSet; // file descriptor set for select
    maxfd++; // maxfd (needed for select) must be one more than actual max

    while (!endProgram_) { // for ever, and ever, and ever
        std::fill(std::begin(rcvBuf), std::end(rcvBuf), 0);

        // set all file descriptors of interest
        // iterate over map and call FD_SET on each socket fd
//        for (IfaceInfoMapItr itr = this->ifaceMap.begin(); itr != this->ifaceMap.end(); ++itr) {
//            const uint64_t sockfd = (itr->second).sockfd;
//            FD_SET(sockfd, &sockfdSet);
//        }

        auto itr = this->ifaceMap.begin();
        IfaceInfo ifaceInfo = itr->second;

        const int sockfd = ifaceInfo.sockfd;
        FD_SET(sockfd, &sockfdSet);
        FD_SET(wakefd_, &sockfdSet);

std::cout << "[DEBUG] sockfd :: " << sockfd << std::endl;

        // wait for there to be something for us to read
        // returns # of ready fds, but we don't need it
        if (select(maxfd, &sockfdSet /*readfds*/, NULL /*writefds*/, NULL /*exceptfds*/, NULL /*timeout*/) == -1) {
            this->closeIfaceSocks();
            LOG_FATAL_PERROR_EXIT("rthread select()");
        }

        /*if (pthread_mutex_lock(&(this->ifaceMapMutex))) { // iface map exclusive access
            this->closeIfaceSocks();
            LOG_FATAL_PERROR_EXIT("rthread pthread_mutex_lock()");
        }*/

        std::vector<DatabaseInfo> databaseInfoVector;

        // iterate over ifaces and check whether we've received anything
        //IfaceInfo ifaceInfo = this->ifaceMap[this->feedbackIface];
        //const uint64_t sockfd = ifaceInfo.sockfd;
        //const std::string ifaceName = this->feedbackIface;

        if (FD_ISSET(sockfd, &sockfdSet)) {
            int size = FEEDBACK_RCV_BUF_LEN;
			
            size_t total = 0; 
			int nbytes = 0;
            
			while((nbytes = recv(sockfd, rcvBuf, size-total-1, 0)) > 0) {
                total += nbytes;
            }

            /*if (nbytes < 0) { 
                this->closeIfaceSocks();
                LOG_FATAL_PERROR_EXIT("rthread recv()");
            }*/

            rcvBuf[total] = 0;

//std::cout << "[DEBUG] :: ";
//for (int i = 0; i < total; ++i) {
	//std::cout  << std::setfill('0') << std::setw(2) << std::hex << (int) rcvBuf[i];
//}
//std::cout << std::endl;

			if (total > 0) { //If any message was received in this interface
std::cout << "[DEBUG] feedback :: bytes received" << std::endl;
                databaseInfoVector = this->readFeedbackMessage(rcvBuf, gpsInfo);
            }
            else {
std::cout << "[DEBUG] feedback :: no bytes received" << std::endl;
            }
        }

        /*if (pthread_mutex_unlock(&(this->ifaceMapMutex))) { // release iface map
            this->closeIfaceSocks();
            LOG_FATAL_PERROR_EXIT("rthread pthread_mutex_unlock()");
        }*/

        //Send all data to the database
        this->databaseManager.createAll(databaseInfoVector);
    } // while() end

    // clean up and be done
    this->closeIfaceSocks();
}

