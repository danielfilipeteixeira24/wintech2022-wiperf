/**
 *
 * Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
 * Distributed under the GNU GPL v2. For full terms see the file LICENSE.
 *
 * This header file defines the Feedback Sender. It accesses the Data Receiver to extract the number of bytes
 * received and uses those measurements to calculate the throughput. The throughput information is sent back
 * to the Feedback Receiver as a Throughput Feedback Message.
 *
 */

#ifndef FEEDBACKSENDER_HPP
#define FEEDBACKSENDER_HPP

#include <string>   // std::string
#include "../DataTransfer.hpp"
#include "DataReceiver.hpp"

/**
 * Structure useful for the throughput feedback messages.
 */
struct FeedbackMessageStruct {
    uint64_t timestamp;
    uint32_t throughput;
    std::string ifaceName;
    uint8_t message[12];
};

/**
 * This class defines a thread that must read the number of bytes that were transmitted
 * to each interface and send it through each interface, accompanied by a timestamp from
 * the GPS sensor.
 */
class FeedbackSender : public DataTransfer {
private:
    /**
     * Pointer to the Data Receiver to access the received bytes counters.
     */
    DataReceiver* dreceiver;

    /**
     * Milliseconds between measurements.
     */
    int feedbackInterval;

    IfaceInfoMap dataReceiverIfaces;
    std::vector<std::string> dataReceiverIfnames;

    void initializeInterfaceSockets();

protected:
    void readAndSetLogLevel(ConfigFile& cfile) override;
    void commThread() override;

public:
    explicit FeedbackSender(DataReceiver *dataReceiver);
    void readConfig(const std::string& configFname) override;
};

#endif //FEEDBACKSENDER_HPP
