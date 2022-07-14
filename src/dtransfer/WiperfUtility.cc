/**
 *
 * Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
 * Distributed under the GNU GPL v2. For full terms see the file LICENSE.
 *
 */

#include "WiperfUtility.hpp"

#include <sstream>     // std::stringstream
#include <arpa/inet.h> // inet_pton
#include <cassert>     // assert
#include <fcntl.h>     // O_RDONLY, S_IRWXU, S_IRUSR, etc
#include <sys/mman.h>  // mmap() and shm_open()

// ------- Configurations ------------

void WiperfUtility::readAndSetLogLevel(ConfigFile& cfile, const std::string& secName) {
    // log level
    LogLevel logLevel = LOG_LEVEL_DEF;
    try {
        const int logLevelInt = std::stoi(cfile.Value(secName, "log-level"));
        if (logLevelInt >= 0 && logLevelInt < NLOG_LEVELS)  // invalid
            logLevel = (LogLevel)logLevelInt;
        else {
            std::stringstream ss;
            ss << "Config exception: section=, " << secName << " value=log-level, "
            << "invalid value " << logLevelInt << ". Acceptable range is (0, "
            << NLOG_LEVELS << ". Reverting to default: " << logLevel;
            LOG_ERR(ss.str().c_str());
        }
    } catch (char const* str) {
        std::stringstream ss;
        ss << "Config exception: section=, " << secName << " value=log-level, "
        << str << " using default value " << logLevel;
        LOG_ERR(ss.str().c_str());
    }

    // done here so all messages appear, and in the correct order
    LOG_LEVEL_SET(logLevel);
    LOG_MSG("Starting program...");
}

uint16_t WiperfUtility::readPort(ConfigFile& cfile, const std::string& secName,
                                uint16_t defPort) {
    uint16_t port = defPort;
    try {
        const int portv = std::stoi(cfile.Value(secName, "port"));
        if (portv >= 1024 && portv <= 49151)  // non-reserved & non-private
            port = (uint16_t)portv;
        else {
            std::stringstream ss;
            ss << "Config exception: section=" << secName << ", value=port, "
            << "invalid value " << portv << ". Acceptable range is (1024, 49151)."
            << " Reverting to default: " << defPort;
            LOG_ERR(ss.str().c_str());
        }
    } catch (char const* str) {
        std::stringstream ss;
        ss << "Config exception: section=" << secName << ", value=port:"
        << str << " using default value " << defPort;
        LOG_ERR(ss.str().c_str());
    }

    return port;
}

void WiperfUtility::readIfaces(ConfigFile& cfile, const std::string& secName,
                                     AddrType addrType, IfaceInfoMap &ifaceMap) {
    // receiver interface names and addresses
    std::string ifacesStr = IFACES_STR_DEF;
    try {
        ifacesStr = cfile.Value(secName, "ifaces");
    } catch (char const* str) {
        std::stringstream ss;
        ss << "Config exception: section=" << secName << ", value=ifaces " << str << " using default value " << ifacesStr;
        LOG_ERR(ss.str().c_str());
    }

    // tokenize it, first by comma to separate the entries
    // note: ">> std::ws" is used to remove whitespace
    std::stringstream sstream(ifacesStr);
    int i = 0;

    for (std::string ientry; std::getline(sstream >> std::ws, ientry, ',');) {
        std::stringstream esstream(ientry);
        std::string iname, iaddr;

        // try to read interface name
        if (!std::getline(esstream >> std::ws, iname, ' ')) {
            std::stringstream ss;
            ss << "Config exception: section=" << secName << ", value=ifaces. "
            << "Invalid interface entry " << ientry << ". Ignoring.";
            LOG_ERR(ss.str().c_str());
            continue;  // nothing else to do here
        }

        // read address
        if (std::getline(esstream >> std::ws, iaddr, ' ')) {
            // check ip address validity
            struct sockaddr_in saddr{};
            if (inet_pton(AF_INET, iaddr.c_str(), &(saddr.sin_addr)) == 1) {  // ok

                // at this point we have a (iname, iaddress) pair, save it

                // if this is a new interface, insert it into the map
                if (ifaceMap.find(iname) == ifaceMap.end()) {
                    IfaceInfo ifaceInfo;
                    // mark sockfd as non-initialized, so we don't accidently close it
                    ifaceInfo.sockfd = UNINITIALIZED_FD;
                    ifaceInfo.ifaceId = i;
                    ifaceMap.insert(IfaceInfoMapKvp(iname, ifaceInfo));
                }

                // set address, server or client depending on type
                if (addrType == SERVER)
                    ifaceMap[iname].addrSrv = iaddr;
                else {
                    assert(addrType == CLIENT);
                    ifaceMap[iname].addrCli = iaddr;
                }

            } else {
                std::stringstream ss;
                ss << "Config exception: section=" << secName << ", value=ifaces. "
                << "Invalid IP address " << iaddr << " for interface " << iname << ". Ignoring entry.";
                LOG_ERR(ss.str().c_str());
            }
        } else {  // no addr to go with the interface name!
            std::stringstream ss;
            ss << "Config exception: section=" << secName << ", value=ifaces. "
            << "Missing address for interface " << iname << ". Ignoring entry.";
            LOG_ERR(ss.str().c_str());
        }

        ++i;
    }  // for each entry loop end

    if (ifaceMap.empty()) {  // do we have at least one?
        std::stringstream ss;
        ss << "Config exception: section=" << secName << ", value=ifaces. "
        << "No valid (interface name, address) pair provided";
        LOG_FATAL_EXIT(ss.str().c_str());
    }
}

std::vector<std::string> WiperfUtility::readIfnames(ConfigFile &cfile, const std::string& secName) {
    // receiver interface names and addresses
    std::string ifacesStr = IFACES_STR_DEF;

    try {
        ifacesStr = cfile.Value(secName, "ifaces");
    } catch (char const* str) {
        std::stringstream ss;
        ss << "Config exception: section=" << secName << ", value=ifaces " << str << " using default value " << ifacesStr;
        LOG_ERR(ss.str().c_str());
    }

    std::vector<std::string> ifnames;

    // note: ">> std::ws" is used to remove whitespace
    std::stringstream sstream(ifacesStr);
    for (std::string ientry; std::getline(sstream >> std::ws, ientry, ',');) {

        std::stringstream esstream(ientry);
        std::string iname;

        // try to read interface name
        if (!std::getline(esstream >> std::ws, iname, ' ')) {
            std::stringstream ss;
            ss << "Config exception: section=" << secName << ", value=ifaces. "
               << "Invalid interface entry " << ientry << ". Ignoring.";
            LOG_ERR(ss.str().c_str());
            continue;  // nothing else to do here
        }

        ifnames.push_back(iname);
    }

    if (ifnames.empty()) {  // do we have at least one?
        std::stringstream ss;
        ss << "Config exception: section=" << secName << ", value=ifaces. "
           << "No valid interface name provided";
        LOG_FATAL_EXIT(ss.str().c_str());
    }

    return ifnames;
}

std::vector<std::string> WiperfUtility::readSsids(ConfigFile &cfile, const std::string& secName) {
    // receiver interface names and addresses
    std::string ssidsStr = SSIDS_STR_DEF;

    try {
        ssidsStr = cfile.Value(secName, "scan-ssids");
    } catch (char const* str) {
        std::stringstream ss;
        ss << "Config exception: section=" << secName << ", value=scan-ssids " << str << " using default value " << ssidsStr;
        LOG_ERR(ss.str().c_str());
    }

    std::vector<std::string> ssids;

    // note: ">> std::ws" is used to remove whitespace
    std::stringstream sstream(ssidsStr);
    for (std::string ientry; std::getline(sstream >> std::ws, ientry, ',');) {
        ssids.push_back(ientry);
    }

    if (ssids.empty()) {  // do we have at least one?
        std::stringstream ss;
        ss << "Config exception: section=" << secName << ", value=scan-ssids. "
        << "No valid interface name provided";
        LOG_FATAL_EXIT(ss.str().c_str());
    }

    return ssids;
}

std::string WiperfUtility::readGpsShmPath(ConfigFile &cfile, const std::string& defGpsShmPath) {
    std::string gpsShmPath = defGpsShmPath;

    try {
        gpsShmPath = cfile.Value("gpsinfo", "shm-path");
    }
    catch (char const* str) {
        gpsShmPath = std::string(GPS_SHM_PATH_DEF);
        std::stringstream ss;
        ss << "Config exception: section=gpsinfo, value=shm-path " << str
        << " using default value " << defGpsShmPath;
        LOG_ERR(ss.str().c_str());
    }

    return gpsShmPath;
}

// -------------- GPS --------------
GpsInfo* WiperfUtility::getGpsInfo(const std::string& gpsShmPath) {
    int gpsShmfd = 0;
    if ((gpsShmfd = shm_open(gpsShmPath.c_str(), O_RDWR, S_IRUSR | S_IRGRP)) < 0) {
        LOG_FATAL_PERROR_EXIT("pthread gpsInfo shm_open()");
    }

    // request memory mapping of gps shared segment
    GpsInfo *gpsInfoShm;
    if ((gpsInfoShm = (GpsInfo *) mmap(NULL, sizeof(GpsInfo),
                                       PROT_READ | PROT_WRITE, MAP_SHARED,
                                       gpsShmfd, 0)) == MAP_FAILED) {
        LOG_FATAL_PERROR_EXIT("pthread gpsInfo nmap()");
    }

    return gpsInfoShm;
}

GpsInfo WiperfUtility::getCurrentGps(GpsInfo* gpsInfo) {
    if (pthread_mutex_lock(&(gpsInfo->mutex))) {
        LOG_FATAL_PERROR_EXIT("pthread gpsInfo pthread_mutex_lock")
    }

    GpsInfo currInfo;
    currInfo.speed = gpsInfo->speed;
    currInfo.systime = gpsInfo->systime;
    currInfo.fix = gpsInfo->fix;
    currInfo.head = gpsInfo->head;
    currInfo.lon = gpsInfo->lon;
    currInfo.lat = gpsInfo->lat;
    currInfo.gpstime = gpsInfo->gpstime;
    currInfo.alt = gpsInfo->alt;
    currInfo.hdop = gpsInfo->hdop;
    currInfo.nsats = gpsInfo->nsats;

    if (pthread_mutex_unlock(&(gpsInfo->mutex))) {
        LOG_FATAL_PERROR_EXIT("pthread gpsInfo pthread_mutex_unlock")
    }

    return currInfo;
}

uint64_t WiperfUtility::getCurrentMillis(GpsInfo* gpsInfo) {
    //I commented the mutexes out because this is a read-only operation!

//    if (pthread_mutex_lock(&(gpsInfo->mutex))) {
//        LOG_FATAL_PERROR_EXIT("pthread gpsInfo pthread_mutex_lock")
//    }

    uint64_t millis = gpsInfo->systime;

//    if (pthread_mutex_unlock(&(gpsInfo->mutex))) {
//        LOG_FATAL_PERROR_EXIT("pthread gpsInfo pthread_mutex_unlock")
//    }

    return millis;
}

// ---------- DATA TYPES ------------
uint64_t WiperfUtility::ntohll(uint64_t value) {
    static const int num = 42;

    if (*reinterpret_cast<const char*>(&num) == num) { // If Little Endian
        const uint32_t high_part = ntohl(static_cast<uint32_t>(value >> 32));
        const uint32_t low_part = ntohl(static_cast<uint32_t>(value & 0xFFFFFFFFLL));

        return (static_cast<uint64_t>(low_part) << 32) | high_part;
    }
    else {
        return value;
    }
}

/**
 * Convert host uint64_t to network standard (big_endian).
 * @param value original
 * @return converted value
 * @see https://stackoverflow.com/questions/3022552/is-there-any-standard-htonl-like-function-for-64-bits-integers-in-c
 */
uint64_t WiperfUtility::htonll(uint64_t value) {
    static const int num = 42;

    // Check the endianness (If Little Endian)
    if (*reinterpret_cast<const char*>(&num) == num) {
        const uint32_t high_part = htonl(static_cast<uint32_t>(value >> 32));
        const uint32_t low_part = htonl(static_cast<uint32_t>(value & 0xFFFFFFFFLL));

        return (static_cast<uint64_t>(low_part) << 32) | high_part;
    }
    else { // If Big Endian
        return value;
    }
}

// ------------ RAT ENUM ------------

RAT WiperfUtility::ifaceToRat(const std::string& ifaceName) {
    if ( ifaceName == "lo" ) {
        return RAT::loopback;
    }
    else if ( ifaceName == "802.11n" ) {
        return RAT::n80211;
    }
    else if ( ifaceName == "802.11ad" ) {
        return RAT::ad80211;
    }
    else if ( ifaceName == "802.11ac" ) {
        return RAT::ac80211;
    }
    else {
        return RAT::invalid;
    }
}

std::string WiperfUtility::ratToIface(RAT rat) {
    if ( rat == RAT::invalid ) {
        return "invalid";
    }
    else if ( rat == RAT::loopback ) {
        return "lo";
    }
    else if ( rat == RAT::n80211 ) {
        return "802.11n";
    }
    else if ( rat == RAT::ac80211) {
        return "802.11ac";
    }
    else if ( rat == RAT::ad80211) {
        return "802.11ad";
    }
    else {
        return "invalid";
    }
}

// ----------------- IFACE INFO FUNCTIONS ------------------

IfaceInfo WiperfUtility::deepCloneIfaceInfo(IfaceInfo &ifaceInfo) {
    IfaceInfo newEntry{};

    newEntry.nbytesAcc = ifaceInfo.nbytesAcc;
    newEntry.sockaddrSrv = ifaceInfo.sockaddrSrv;
    newEntry.sockfd = ifaceInfo.sockfd;
    newEntry.addrSrv = ifaceInfo.addrSrv;
    newEntry.addrCli = ifaceInfo.addrCli;
    newEntry.ifaceId = ifaceInfo.ifaceId;

    return newEntry;
}

IfaceInfoMapKvp WiperfUtility::deepCloneIfaceInfoMapKvp(IfaceInfoMapKvp &ifaceInfoMapKvp) {
    IfaceInfoMapKvp newEntry{};
    newEntry.second = deepCloneIfaceInfo(ifaceInfoMapKvp.second);
    newEntry.first = ifaceInfoMapKvp.first;

    return newEntry;
}