/**
 *
 * Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
 * Distributed under the GNU GPL v2. For full terms see the file LICENSE.
 *
 * Utility functions and definitions for the Data Sender, Data Receiver and Channel Monitor.
 *
 */

#ifndef WIPERF_IMPL_UTILITY_H
#define WIPERF_IMPL_UTILITY_H

#include <string>       // std::string
#include <map>          // std::map
#include <utility>      // std::pair
#include <cstdint>     // uint*_t
#include <netinet/in.h> // struct sockaddr_in

#include "../util/configfile.hpp"        // class ConfigFile
#include "../mygpsd/gpsinfo.hpp"
#include "../util/logfile.hpp"

#define UNINITIALIZED_FD -1
#define GPS_SHM_PATH_DEF "/wiperf-gpsinfo"
#define SSIDS_STR_DEF "lo"
#define IFACES_STR_DEF "lo 127.0.0.1"
#define CONFIG_FNAME "/etc/wiperf.conf"
#define PORT_CLI_DEF 44443
#define PORT_SRV_DEF 44444
#define SND_BUF_LEN 65506
#define RCV_BUF_LEN 524288
#define RCV_BUF_NUM_PACKETS 64
#define PORT_FEED_CLI_DEF 44445
#define PORT_FEED_SRV_DEF 44446
//#define FEEDBACK_SND_BUF_LEN 512 <- This is dynamic and depends on the RATs per message
#define FEEDBACK_RCV_BUF_LEN 512
#define FEEDBACK_IFACE_DEF "lo"
#define FEEDBACK_INTERVAL_DEF 100
//in milliseconds

/**
 * Enum to define the Radio Access Technology.
 */
enum class RAT { invalid = -1,  loopback = 0, n80211 = 1, ac80211 = 2, ad80211 = 3, g5nr = 4};

/**
 * Structure of information regarding an interface.
 */
struct IfaceInfo { // interface information
    std::string addrSrv;
    std::string addrCli;
    struct sockaddr_in sockaddrSrv;
    int sockfd;
    uint64_t nbytesAcc;  //NOTE: This is only used by the data receiver, and not by the data sender
    int ifaceId;
};

// Auxiliary structs and types
typedef struct IfaceInfo IfaceInfo;
typedef std::map<std::string, IfaceInfo> IfaceInfoMap; // iname -> ifaceinfo
typedef IfaceInfoMap::iterator IfaceInfoMapItr;
typedef std::pair<std::string, IfaceInfo> IfaceInfoMapKvp;

enum AddrType {CLIENT, SERVER};
typedef enum AddrType AddrType;

class WiperfUtility {
public:
    //Configuration utility functions
    static void readAndSetLogLevel(ConfigFile& cfile, const std::string& secName);
    static uint16_t readPort(ConfigFile& cfile, const std::string& secName, uint16_t defPort);
    static void readIfaces(ConfigFile& cfile, const std::string& secName, AddrType addrType, IfaceInfoMap &ifaceMap);
    static std::vector<std::string> readIfnames(ConfigFile& cfile, const std::string& secName);
    static std::vector<std::string> readSsids(ConfigFile& cfile, const std::string& secName);
    static std::string readGpsShmPath(ConfigFile& cfile, const std::string& defGpsShmPath);

    // GPS utility functions
    static GpsInfo* getGpsInfo(const std::string& gpsShmPath);
    static uint64_t getCurrentMillis(GpsInfo* gpsInfo);
    static GpsInfo getCurrentGps(GpsInfo* gpsInfo);

    // Data types utility functions
    /**
     * Convert host uint64_t to network standard (big_endian).
     * @param value original
     * @return converted value
     * @see https://stackoverflow.com/questions/3022552/is-there-any-standard-htonl-like-function-for-64-bits-integers-in-c
     */
    static uint64_t htonll(uint64_t value);
    static uint64_t ntohll(uint64_t value);

    // RAT enum utility functions
    static RAT ifaceToRat(const std::string& ifaceName);
    static std::string ratToIface(RAT rat);

    //Functions associated with IfaceInfo
    static IfaceInfo deepCloneIfaceInfo(IfaceInfo &ifaceInfo);
    static IfaceInfoMapKvp deepCloneIfaceInfoMapKvp(IfaceInfoMapKvp &ifaceInfoMapKvp);
};

#endif //WIPERF_IMPL_UTILITY_H
