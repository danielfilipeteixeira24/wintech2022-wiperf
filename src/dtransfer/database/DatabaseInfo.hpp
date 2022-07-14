/**
 *
 * Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
 * Distributed under the GNU GPL v2. For full terms see the file LICENSE.
 *
 * This header file defines the data structure for the local database.
 */

#ifndef WIPERF_IMPL_DATABASEINFO_HPP
#define WIPERF_IMPL_DATABASEINFO_HPP

#include <string>

struct DatabaseInfo {
    double latitude;
    double longitude;
    double speed;
    double orientation;
    int moving;

    uint32_t throughput;
    uint32_t numBits;
    std::string channelInfo;
    std::string scanInfo;
    std::string rat;

    uint64_t timestamp;

    // from channel info, to calculate statistics
    uint32_t tx_bitrate;
    int32_t signal_strength;
};

#endif //WIPERF_IMPL_DATABASEINFO_HPP
