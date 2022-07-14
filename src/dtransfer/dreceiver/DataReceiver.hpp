/**
 *
 * Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
 * Distributed under the GNU GPL v2. For full terms see the file LICENSE.
 *
 * This header file defines the Data Receiver class. The Data Receiver listens to pseudo-random
 * packets sent by the Data Sender and keeps track of the amount of bytes received.
 */

#ifndef DATARECEIVER_HPP
#define DATARECEIVER_HPP

#include <sys/socket.h>
#include <sys/types.h>
#include <atomic>
#include <thread>

#include "../DataTransfer.hpp"
#include "../../util/configfile.hpp"
#include "../../mygpsd/gpsinfo.hpp"

class DataReceiver : public DataTransfer {
private:
    /**
     * Vector of worker threads that listen for data in each interface.
     */
    std::vector<std::thread> workers;

    /**
     * Flag to stop the worker threads.
     */
    std::atomic<bool> stopFlag;

protected:
    void readAndSetLogLevel(ConfigFile &cfile) override;

    /**
     * Adds all the interface sockets on the map to the fd set.
     * Doesn't really need to use a mutex because we'll be reading information that isn't going
     * to be changed anywhere else.
     */
    void addIfaceSocksToFdSet(fd_set *fdset);

    /**
     * Receives data, and keeps track of how much it receives.
     */
    void commThread() override;

public:

    /**
     * Used to reset the byte counters based on the last read value.
     */
    std::map<std::string, uint32_t> nbytes_reset_value;

    DataReceiver();

    void readConfig(const std::string &configFname) override;

    /**
     * Calls DataTransfer::stopThread(), but also runs code specific
     * to the DataReceiver.
     */
    void stopThread() override;
};

#endif //DATARECEIVER_HPP
