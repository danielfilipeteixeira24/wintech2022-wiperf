/**
 *
 * Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
 * Distributed under the GNU GPL v2. For full terms see the file LICENSE.
 *
 * Defines the class for the UDP client to transmit pseudo-random data and measure the
 * throughput.
 */

#ifndef DATASENDER_H
#define DATASENDER_H

#include <atomic>
#include <thread>
#include "../DataTransfer.hpp"
#include "../../mygpsd/gpsinfo.hpp"

class DataSender : public DataTransfer {
private:
    int decisionLevel;
    std::string decision;
    uint64_t decisionExpiresAt;

    std::vector<std::thread> workers;
    std::atomic<bool> stopFlag;

    /**
     * Send data only through one interface, chosen at random, or by
     * the decision making module, or in another fashion.
     */
    void sendOneInterface();

    /**
     * Send data through every interface to measure throughput.
     * It creates one thread for each interface, which sends traffic
     * inside a while loop.
     */
    void sendEveryInterface();

public:
    DataSender();
    void readConfig(const std::string& configFname) override;

    /**
     * Calls DataTransfer::stopThread(), but also runs code specific
     * to the DataSender.
     */
    void stopThread() override;

protected:
    void readAndSetLogLevel(ConfigFile &cfile) override;

    /**
     * Picks an interface uniformly at random.
     * More sophisticated strategies to be implemented at a later date.
     * @return random interface
     */
    std::string pickRandomIface();

    void commThread() override;
};

#endif //DATASENDER_H
