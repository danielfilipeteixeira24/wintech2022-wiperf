/**
 *
 * Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
 * Distributed under the GNU GPL v2. For full terms see the file LICENSE.
 *
 */

#ifndef FEEDBACKRECEIVER_HPP
#define FEEDBACKRECEIVER_HPP

#include <string>   // std::string

#include "../DataTransfer.hpp"
#include "../database/DatabaseManager.hpp"
#include "../../mygpsd/gpsinfo.hpp"

/**
 * This class defines a thread that must receive the throughput data and
 * send it to the shared memory.
 */

class FeedbackReceiver : public DataTransfer {
private:
    int feedbackInterval;
    DatabaseManager databaseManager;

    IfaceInfoMap dataSenderIfaces;
    std::vector<std::string> dataSenderIfnames;

    /**
     * Create and bind sockets for each interface in the IfaceInfoMap field.
     * Socket file descriptors are stored in IfaceInfoMap.
     * @return Largest FD value at the end of the loop (maxfd)
     */
    int initializeInterfaceSockets();

    std::vector<DatabaseInfo> readFeedbackMessage(uint8_t* buffer, GpsInfo* gpsInfo);

protected:
    void readAndSetLogLevel(ConfigFile& cfile) override;
    void commThread() override;

public:
    FeedbackReceiver();
    void readConfig(const std::string& configFname) override;
};

#endif //FEEDBACKRECEIVER_HPP
