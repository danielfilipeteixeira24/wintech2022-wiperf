/**
 *
 * Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
 * Distributed under the GNU GPL v2. For full terms see the file LICENSE.
 *
 * This class contains the structures and the definition of the Channel Monitor module.
 */

#ifndef WIPERF_IMPL_CHANNELMONITOR_H
#define WIPERF_IMPL_CHANNELMONITOR_H

#include "../database/DatabaseManager.hpp"
#include "../WiperfUtility.hpp"

/**
 * Structure containing information to send netlink messages towards
 * the nl80211 user-kernel interface.
 */
typedef struct {
    int id;
    struct nl_sock *socket;
    struct nl_cb *cb1; //survey
    struct nl_cb *cb2; //station
    struct nl_cb *cb3; //interface
    struct nl_cb *cb4; //power save mode
    int result1, result2, result3, result4;
} NetlinkInfo;

/**
 * Structure containing all the information collected through the
 * nl80211 interface.
 */
typedef struct {
    std::string ifname;
    int ifindex;

    /**
     * WiFi statistics information.
     */
    uint32_t inactive_time;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint64_t rx_bytes_64;
    uint64_t tx_bytes_64;
    uint8_t signal;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t tx_retries;
    uint32_t tx_failed;
    uint8_t signal_avg;
    uint16_t llid;
    uint16_t plid;
    uint8_t plink_state;
    uint32_t connected_time;

    uint32_t beacon_loss;
    int64_t t_offset;
    uint32_t local_pm;
    uint32_t peer_pm;
    uint32_t non_peer_pm;
    uint8_t chain_signal;
    uint8_t chain_signal_avg;
    uint32_t expected_throughput;
    uint64_t rx_drop_misc;
    uint64_t beacon_rx;
    uint8_t beacon_signal_avg;
    uint64_t rx_duration;
    uint64_t sta_pad;
    uint8_t ack_signal;
    int8_t ack_signal_avg;
    uint32_t rx_mpdus; //MAC Protocol Data Units (MPDUs)
    uint32_t fcs_error_count;
    uint8_t connected_to_gate;
    uint64_t tx_duration;
    uint16_t airtime_weight;
    uint32_t airtime_link_metric;
    uint32_t assoc_at_boottime;

    /**
     * Parameters that characterize the radio channel:
     * - the modulation and coding schemes
     * - VHT (very high throughput) index
     * - HE (high efficiency) index
     * - GI (guard interval)
     * - NSS (number of spatial streams) (MIMO)
     * - DCM (dual carrier modulation)
     * - RU (resource unit) allocation
     */
    uint16_t tx_bitrate;
    uint32_t tx_bitrate32;
    uint8_t msc;
    int short_gi;
    int f5_mhz_width;
    int f10_mhz_width;
    int f40_mhz_width;
    int f80_mhz_width;
    int f80p80_mhz_width;
    int f160_mhz_width;
    uint8_t vht_mcs;
    uint8_t vht_nss;
    uint8_t he_mcs;
    uint8_t he_nss;
    uint8_t he_gi; //enum nl80211_he_gi
    uint8_t he_dcm;
    uint8_t he_ru_alloc; //enum nl80211_he_ru_alloc

    /**
     * Statistics based on TID (traffic identifiers)
     * MAC service data unit (MSDU) statistics (MAC layer).
     */
    uint64_t tid_rx_msdu;
    uint64_t tid_tx_msdu;
    uint64_t tid_tx_msdu_retries;
    uint64_t tid_tx_msdu_failed;
    int tid_pad;

    /**
     * Transmission Queues
     */
    uint64_t txq_backlog_bytes;
    uint64_t txq_backlog_packets;
    uint64_t txq_flows;
    uint64_t txq_drops;
    uint64_t txq_ecn_marks;
    uint64_t txq_overlimit;  //overflow of queue
    uint64_t txq_overmemory; //memory limit overflow
    uint64_t txq_collisions; //hash collisions
    uint64_t txq_tx_bytes;   //dequeued bytes
    uint64_t txq_tx_packets; //dequeued packets

    /**
     * Basic service sets (BSS)
     */
    int cts_protection;  //if clear to send (CTS) mode is on
    int short_preamble;  //if short preamble is used
    int short_slot_time; //the waiting time after collision
    // (short slot time = higher tput)
    uint8_t dtim_period; //interval between delivery traffic indication message (DTIM)
    uint16_t beacon_interval; //time between beacons

    /**
     * Survey info
     */
     uint64_t surv_frequency;
     uint8_t  surv_noise;
     uint64_t surv_in_use;
     uint64_t surv_time;
     uint64_t surv_time_busy;
     uint64_t surv_time_ext_busy;
     uint64_t surv_time_rx;
     uint64_t surv_time_tx;
     uint64_t surv_time_scan;
     uint64_t surv_time_bss_rx;

     /**
      * Interface info
      */
     uint32_t iface_wiphy;
     uint32_t iface_frequency;
     uint32_t iface_channel;
     uint32_t iface_channel_width;
     uint32_t iface_center_freq1;
     uint32_t iface_center_freq2;
     uint32_t iface_channel_type;
     uint32_t iface_tx_power; //in mBm

} WifiInfo;

/**
 *
 */
class ChannelMonitor {
private:
    //std::shared_ptr<DataSender> dataSender;
    DatabaseManager databaseManager;
    bool endProgram_;
    int samplingInterval;
    std::vector<std::string> ifnames;

    void configure(std::string const &configFname);
public:
    explicit ChannelMonitor(std::string const &configFname);
    void run();
    void stopThread();

    //code and decode functions
    static std::string codeWifiInfo(const WifiInfo& wifiInfo);
    static WifiInfo decodeWifiInfo(const std::string& info);
};

#endif //WIPERF_IMPL_CHANNELMONITOR_H
