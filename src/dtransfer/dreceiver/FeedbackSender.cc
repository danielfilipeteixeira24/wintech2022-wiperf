/**
 *
 * Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
 * Distributed under the GNU GPL v2. For full terms see the file LICENSE.
 *
 */

#include <sys/socket.h>
#include <sys/types.h>
#include <fcntl.h>        // O_RDONLY, S_IRWXU, S_IRUSR, etc
#include <sstream>
#include <map>      // std::map
#include <string>   // std::string
#include <utility>  // std::pair
#include <chrono>
#include <thread>
#include <cstring>  // std::memcpy
#include <arpa/inet.h>
#include <iomanip>

#include "FeedbackSender.hpp"

FeedbackSender::FeedbackSender(DataReceiver *dataReceiver) :
    DataTransfer("FeedTx"), dreceiver(),
    feedbackInterval(FEEDBACK_INTERVAL_DEF), dataReceiverIfaces() {
    dreceiver = dataReceiver;
}

void FeedbackSender::readAndSetLogLevel(ConfigFile &cfile) {
    WiperfUtility::readAndSetLogLevel(cfile, std::string("feedback-sender"));
}

void FeedbackSender::readConfig(const std::string& configFname) {
    ConfigFile cfile(configFname);

    this->gpsShmPath = WiperfUtility::readGpsShmPath(cfile, GPS_SHM_PATH_DEF);

    this->portSrv = WiperfUtility::readPort(cfile, "feedback-receiver", PORT_FEED_SRV_DEF);
    WiperfUtility::readIfaces(cfile, "feedback-receiver", SERVER, this->ifaceMap);
    this->portCli = WiperfUtility::readPort(cfile, "feedback-sender", PORT_FEED_CLI_DEF);
    WiperfUtility::readIfaces(cfile, "feedback-sender", CLIENT, this->ifaceMap);

    WiperfUtility::readIfaces(cfile, "data-receiver", SERVER, this->dataReceiverIfaces);
    WiperfUtility::readIfaces(cfile, "data-sender", CLIENT, this->dataReceiverIfaces);

    this->dataReceiverIfnames = WiperfUtility::readIfnames(cfile, "data-receiver");

    // configure the feedback interval
    this->feedbackInterval = FEEDBACK_INTERVAL_DEF;
    try {
        this->feedbackInterval = std::stoi(cfile.Value("feedback-sender", "feedback-interval"));
    } catch (char const* str) {
        std::stringstream ss;
        ss << "Config exception: section=feedback-sender, value=feedback-interval " << str
        << " using default value " << this->feedbackInterval;
        LOG_ERR(ss.str().c_str());
    }

    // check that we have both cli and srv addresses for all ifaces
    // remove interfaces for which we don't have both addresses
    for (auto itr = this->ifaceMap.begin(); itr != this->ifaceMap.end();) {
        IfaceInfo& iinfo = itr->second;

        if (!iinfo.addrSrv.length() || !iinfo.addrCli.length()) {
            itr = this->ifaceMap.erase(itr); //it's missing an address
        }
        else {
            ++itr; //it's good
        }
    }

    // do we have at least one interface pair?
    if (this->ifaceMap.empty()) {
        std::stringstream ss;
        ss << "Config exception: section=feedback-receiver/feedback-sender, value=ifaces."
        << " Don't have any matching sender/receiver interface pairs.";
        LOG_FATAL_EXIT(ss.str().c_str());
    }
}

/**
 * Create and bind sockets for each interface in the IfaceInfoMap field.
 * Socket file descriptors are stored in IfaceInfoMap.
 */
void FeedbackSender::initializeInterfaceSockets() {
    // go over all interface addresses and create a socket for each of them
    IfaceInfoMap& ifaceMap = this->ifaceMap;
    for (auto & itr : ifaceMap) {
        std::string iname = itr.first;
        IfaceInfo& iinfo = itr.second;

        std::stringstream ss1;
        ss1 << "Attaching interface " << iname << " @ " << iinfo.addrCli << ":"
        << this->portCli;
        LOG_MSG(ss1.str().c_str());

        // create udp socket
        int sockfdCli;
        if ((sockfdCli = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
            LOG_FATAL_PERROR_EXIT("sthread socket()");
        }

        // build client (sender) address structure
        struct sockaddr_in sockaddrCli = { 0 };
        this->createSockaddr(iinfo.addrCli, this->portCli, &sockaddrCli);

        // bind to local address
        if (bind(sockfdCli,
                 (struct sockaddr*)&sockaddrCli, sizeof(sockaddrCli)) < 0) {
            this->closeIfaceSocks();
            std::stringstream ss2;
            ss2 << "sthread bind() addr " << iinfo.addrCli;
            LOG_FATAL_PERROR_EXIT(ss2.str().c_str());
        }

        // save new interface details
        iinfo.sockfd = sockfdCli;
        iinfo.nbytesAcc = 0; // Since we will send feedback, this is not needed

        // build server (receiver) address structure
        this->createSockaddr(iinfo.addrSrv, this->portSrv, &iinfo.sockaddrSrv);
    }  // iface sock creation loop end
}

void FeedbackSender::commThread() {
    this->initializeInterfaceSockets();
    //GpsInfo* gpsInfo = WiperfUtility::getGpsInfo(this->gpsShmPath);

    LOG_MSG("program up and running");

    /*
     * t-2, t-1 and t information.
     * These maps holds feedback message sent through each interface at
     * time t, t-1 and t-2.
     * The idea is that at time t, we always send the two previous readings also
     * to add some reliability to the feedback.
     */
    std::map<std::string, FeedbackMessageStruct> tm2Info;
    std::map<std::string, FeedbackMessageStruct> tm1Info;
    std::map<std::string, FeedbackMessageStruct> tInfo;

    //Needed to synchronize to only send on the 100 ms mark (instead of sending at 1320 ms,
    //send at 1400 ms).
    //GpsInfo currentInfo = WiperfUtility::getCurrentGps(gpsInfo);
    //uint64_t currentMillis = WiperfUtility::getCurrentMillis(gpsInfo);

    std::chrono::milliseconds currentMillis_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch());
    uint64_t currentMillis = currentMillis_ms.count();

    uint64_t sleepingInterval = this->feedbackInterval - (currentMillis % this->feedbackInterval);
    std::this_thread::sleep_for(std::chrono::milliseconds(sleepingInterval));

    while (!endProgram_) {
        //Get the GPS timestamp that corresponds to the throughput information
        uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        //uint64_t timestamp = WiperfUtility::getCurrentMillis(gpsInfo);
        timestamp = timestamp - (timestamp % this->feedbackInterval); // Do a little rounding to sync with receiver
        // receiver will add this to the same database entry as the mobility and channel information

        std::map<std::string, uint32_t> nbytesMap;

        // Read the throughput from the dreceiver pointer and add them to the nbytesMap
        for (auto & itr : dreceiver->getIfaceInfoMap()) {
            uint32_t nbytes = itr.second.nbytesAcc;
            nbytesMap.insert({itr.first, nbytes});
        }

        // Tell the dreceiver that we read the throughput when tput = x,
        // for them to reset their counters by decrementing by x
        for (auto & itr : dreceiver->nbytes_reset_value) {
            auto itr_tput = nbytesMap.find(itr.first);
            itr.second = itr_tput->second;
        }

        uint64_t netTimestamp = WiperfUtility::htonll(timestamp);

        // Update the maps for another iteration
        tm2Info = tm1Info;
        tm1Info = tInfo;
        tInfo.clear();

        // 4 Bytes - # of RATs
        // 4 Bytes - RAT ID
        // 12 Bytes - entry t
        // 12 Bytes - entry t-1
        // 12 Bytes - entry t-2
        // 40 Bytes per RAT + 4 bytes for # of RATs

        uint32_t numRats = static_cast<uint32_t>(this->dataReceiverIfnames.size());
        uint32_t netNumRats = htonl(numRats);
        uint32_t sizePerRat = 40;
        const int bufferSize = 4 + numRats * sizePerRat;

        // Buffer is a variable-length array, the "new" operator has to be used
        // to allocate dynamic memory
        uint8_t* buffer = new uint8_t[bufferSize];
        std::memset(buffer, 0, bufferSize);
        std::memcpy(&buffer[0], &netNumRats, sizeof(uint32_t));

        uint32_t i = 0;

        for (auto & ifname : this->dataReceiverIfnames) {
            uint32_t offset = 4 + sizePerRat * i;
            std::string ifaceName = ifname;

            uint32_t nbytes = nbytesMap.find(ifaceName)->second;

            uint32_t throughput = (nbytes * 8) / sleepingInterval;
            uint32_t netThroughput = htonl(throughput);

            uint8_t currInfo[12] = { 0 };
            std::memcpy(&(currInfo[0]), &netTimestamp, sizeof(uint64_t));
            std::memcpy(&(currInfo[8]), &netThroughput, sizeof(uint32_t));

            std::memcpy(&buffer[offset], &(i), 4);

            //Add entry t
            std::memcpy(&buffer[offset + 4], &currInfo[0], 12);

            if (!tm1Info.empty() && tm1Info.count(ifaceName)) {
                //Add entry t-1
                uint8_t *message1 = tm1Info[ifaceName].message;
                std::memcpy(&buffer[offset + 4 + 12], &message1[0],12);

                if (!tm2Info.empty() && tm2Info.count(ifaceName)) {
                    //Add entry t-2
                    uint8_t *message2 = tm2Info[ifaceName].message;
                    std::memcpy(&buffer[offset + 4 + 24], &message2[0], 12);
                }
            }

            //Add the current info (time t) to the tInfo map for future use.
            FeedbackMessageStruct feedbackMsg;
            feedbackMsg.ifaceName = ifaceName;
            feedbackMsg.throughput = throughput;
            feedbackMsg.timestamp = timestamp;

            std::memcpy(&(feedbackMsg.message[0]), &currInfo[0], 12);
            tInfo.insert(std::pair<std::string, FeedbackMessageStruct>(ifaceName, feedbackMsg));

            ++i;
        } // for() end


        auto itr = this->ifaceMap.begin();
        IfaceInfo ifaceInfo = itr->second;

        if (sendto(ifaceInfo.sockfd, buffer, bufferSize, 0,  /*flags*/
                   (const struct sockaddr*) &(ifaceInfo.sockaddrSrv), sizeof(ifaceInfo.sockaddrSrv)) == -1) {
            this->closeIfaceSocks();
            LOG_FATAL_PERROR_EXIT("rthread sendto()");
        }


        delete[] buffer; // delete dynamic memory allocation

        //Calculate the sleeping time and sleep
        //uint64_t endTimestamp = WiperfUtility::getCurrentMillis(gpsInfo);

        std::chrono::milliseconds endTimestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch());
        uint64_t endTimestamp = endTimestamp_ms.count();

        sleepingInterval = this->feedbackInterval - (endTimestamp % this->feedbackInterval);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepingInterval));
    } // while() end

    this->closeIfaceSocks();
}
