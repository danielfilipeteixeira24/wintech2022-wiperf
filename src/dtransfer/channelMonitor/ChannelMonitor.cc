/**
 *
 * Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
 * Distributed under the GNU GPL v2. For full terms see the file LICENSE.
 *
 * This class contains the implementation of the Channel Monitor module.
 *
 * The model uses netlink messages and the nl80211 user-kernel interface. The code related to
 * netlink and nl80211 is based on the iw software.
 *
 * @see https://git.kernel.org/pub/scm/linux/kernel/git/jberg/iw.git/tree/
 * @see https://github.com/Alamot/code-snippets/tree/master/nl80211_info
 */

#include <cstdio>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <cerrno>
#include <netlink/netlink.h>    //netlink functions
#include <netlink/genl/genl.h>  //genl_connect, genlmsg_put
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>  //genl_ctrl_resolve
#include <linux/nl80211.h>      //NL80211 definitions
#include <net/if.h>
#include <fstream>
#include <thread>
#include <iostream>
#include <chrono>
#include <sstream>

#include "ChannelMonitor.hpp"

#define SAMPLING_INTERVAL_DEF 100
#define ETH_ALEN 6

//https://git.kernel.org/pub/scm/linux/kernel/git/jberg/iw.git/tree/interface.c
static std::string channel_type_name(enum nl80211_channel_type channel_type) {
    switch (channel_type) {
        case NL80211_CHAN_NO_HT:
            return "NO HT";
        case NL80211_CHAN_HT20:
            return "HT20";
        case NL80211_CHAN_HT40MINUS:
            return "HT40-";
        case NL80211_CHAN_HT40PLUS:
            return "HT40+";
        default:
            return "unknown";
    }
}

//https://git.kernel.org/pub/scm/linux/kernel/git/jberg/iw.git/tree/interface.c
static std::string channel_width_name(enum nl80211_chan_width width) {
    switch (width) {
        case NL80211_CHAN_WIDTH_20_NOHT:
            return "20 MHz (no HT)";
        case NL80211_CHAN_WIDTH_20:
            return "20 MHz";
        case NL80211_CHAN_WIDTH_40:
            return "40 MHz";
        case NL80211_CHAN_WIDTH_80:
            return "80 MHz";
        case NL80211_CHAN_WIDTH_80P80:
            return "80+80 MHz";
        case NL80211_CHAN_WIDTH_160:
            return "160 MHz";
        case NL80211_CHAN_WIDTH_5:
            return "5 MHz";
        case NL80211_CHAN_WIDTH_10:
            return "10 MHz";
        default:
            return "unknown";
    }
}

//https://github.com/retailnext/iw/blob/master/util.c
static int ieee80211_channel_to_frequency(uint32_t chan, enum nl80211_band band) {
    /* see 802.11 17.3.8.3.2 and Annex J
	 * there are overlapping channel numbers in 5GHz and 2GHz bands */
    if (chan <= 0)
        return 0; /* not supported */
    switch (band) {
        case NL80211_BAND_2GHZ:
            if (chan == 14)
                return 2484;
            else if (chan < 14)
                return 2407 + chan * 5;
            break;
        case NL80211_BAND_5GHZ:
            if (chan >= 182 && chan <= 196)
                return 4000 + chan * 5;
            else
                return 5000 + chan * 5;
            break;
        case NL80211_BAND_60GHZ:
            if (chan < 5)
                return 56160 + chan * 2160;
            break;
        default:;
    }
    return 0; /* not supported */
}

//https://github.com/retailnext/iw/blob/master/util.c
static int ieee80211_frequency_to_channel(uint32_t freq) {
    /* see 802.11-2007 17.3.8.3.2 and Annex J */
    if (freq == 2484)
        return 14;
    else if (freq < 2484)
        return (freq - 2407) / 5;
    else if (freq >= 4910 && freq <= 4980)
        return (freq - 4000) / 5;
    else if (freq <= 45000) /* DMG band lower limit */
        return (freq - 5000) / 5;
    else if (freq >= 58320 && freq <= 64800)
        return (freq - 56160) / 2160;
    else
        return 0;
}

//https://github.com/retailnext/iw/blob/master/util.c
static void mac_addr_n2a(char *mac_addr, unsigned char *arg) {
    int i, l;

    l = 0;
    for (i = 0; i < ETH_ALEN; i++) {
        if (i == 0) {
            sprintf(mac_addr + l, "%02x", arg[i]);
            l += 2;
        } else {
            sprintf(mac_addr + l, ":%02x", arg[i]);
            l += 3;
        }
    }
}

static int finish_handler(struct nl_msg *msg, void *arg) {
    int *ret = (int *) arg;
    *ret = 0;
    return NL_SKIP;
}

// https://git.kernel.org/pub/scm/linux/kernel/git/jberg/iw.git/tree/interface.c#n303
static int getInterfaceInfo_callback(struct nl_msg *msg, void *arg) {
    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    struct genlmsghdr *gnlh = (genlmsghdr *) nlmsg_data(nlmsg_hdr(msg));
    nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0), genlmsg_attrlen(gnlh, 0), NULL);

    WifiInfo *wifi = (WifiInfo *) arg;

    if (tb[NL80211_ATTR_WIPHY]) {
        wifi->iface_wiphy = nla_get_u32(tb[NL80211_ATTR_WIPHY]);
    }
    if (tb[NL80211_ATTR_WIPHY_FREQ]) {
        uint32_t frequency = nla_get_u32(tb[NL80211_ATTR_WIPHY_FREQ]);
        wifi->iface_frequency = frequency;
        wifi->iface_channel = ieee80211_frequency_to_channel(frequency);

        if (tb[NL80211_ATTR_CHANNEL_WIDTH]) {
            uint32_t channel_width = nla_get_u32(tb[NL80211_ATTR_CHANNEL_WIDTH]);
            wifi->iface_channel_width = channel_width;
            //std::string channel_width_name_string = channel_width_name((enum nl80211_chan_width) channel_width);
            //std::cout << "Channel Width: " << channel_width_name_string << std::endl;

            if (tb[NL80211_ATTR_CENTER_FREQ1]) {
                wifi->iface_center_freq1 = nla_get_u32(tb[NL80211_ATTR_CENTER_FREQ1]);
            }
            if (tb[NL80211_ATTR_CENTER_FREQ2]) {
                wifi->iface_center_freq2 = nla_get_u32(tb[NL80211_ATTR_CENTER_FREQ2]);
            }
        } else if (tb[NL80211_ATTR_WIPHY_CHANNEL_TYPE]) {
            uint32_t channel_type = nla_get_u32(tb[NL80211_ATTR_WIPHY_CHANNEL_TYPE]);
            wifi->iface_channel_type = channel_type;
            //std::string channel_type_name_string = channel_type_name((enum nl80211_channel_type) channel_type);
            //std::cout << "Channel Type: " << channel_type_name_string << std::endl;
        }
    }

    if (tb[NL80211_ATTR_WIPHY_TX_POWER_LEVEL]) {
        wifi->iface_tx_power = nla_get_u32(tb[NL80211_ATTR_WIPHY_TX_POWER_LEVEL]);
    }
//    if (tb[NL80211_ATTR_TXQ_STATS]) {
//        parse_txq_stats(tb[NL80211_ATTR_TXQ_STATS], NULL);
//    }

    return NL_SKIP;
}

static int getWifiInfo_callback(struct nl_msg *msg, void *arg) {
    struct nla_policy stats_policy[NL80211_STA_INFO_MAX + 1] = {0};
    stats_policy[NL80211_STA_INFO_INACTIVE_TIME] = {NLA_U32};
    stats_policy[NL80211_STA_INFO_RX_BYTES] = {NLA_U32};
    stats_policy[NL80211_STA_INFO_TX_BYTES] = {NLA_U32};
    stats_policy[NL80211_STA_INFO_RX_BYTES64] = {NLA_U64};
    stats_policy[NL80211_STA_INFO_TX_BYTES64] = {NLA_U64};
    stats_policy[NL80211_STA_INFO_SIGNAL] = {NLA_U8};
    stats_policy[NL80211_STA_INFO_TX_BITRATE] = {NLA_NESTED};
    stats_policy[NL80211_STA_INFO_RX_PACKETS] = {NLA_U32};
    stats_policy[NL80211_STA_INFO_TX_PACKETS] = {NLA_U32};
    stats_policy[NL80211_STA_INFO_TX_RETRIES] = {NLA_U32};
    stats_policy[NL80211_STA_INFO_TX_FAILED] = {NLA_U32};
    stats_policy[NL80211_STA_INFO_SIGNAL_AVG] = {NLA_U8};
    stats_policy[NL80211_STA_INFO_LLID] = {NLA_U16};
    stats_policy[NL80211_STA_INFO_PLID] = {NLA_U16};
    stats_policy[NL80211_STA_INFO_PLINK_STATE] = {NLA_U8};
    stats_policy[NL80211_STA_INFO_BSS_PARAM] = {NLA_NESTED};
    stats_policy[NL80211_STA_INFO_CONNECTED_TIME] = {NLA_U32};
    stats_policy[NL80211_STA_INFO_BEACON_LOSS] = {NLA_U32};
    stats_policy[NL80211_STA_INFO_T_OFFSET] = {NLA_S64};
    stats_policy[NL80211_STA_INFO_LOCAL_PM] = {NLA_U32};
    stats_policy[NL80211_STA_INFO_PEER_PM] = {NLA_U32};
    stats_policy[NL80211_STA_INFO_NONPEER_PM] = {NLA_U32};
    stats_policy[NL80211_STA_INFO_CHAIN_SIGNAL] = {NLA_U8};
    stats_policy[NL80211_STA_INFO_CHAIN_SIGNAL_AVG] = {NLA_U8};
    stats_policy[NL80211_STA_INFO_EXPECTED_THROUGHPUT] = {NLA_U32};
    stats_policy[NL80211_STA_INFO_RX_DROP_MISC] = {NLA_U64};
    stats_policy[NL80211_STA_INFO_BEACON_RX] = {NLA_U64};
    stats_policy[NL80211_STA_INFO_BEACON_SIGNAL_AVG] = {NLA_U8};
    stats_policy[NL80211_STA_INFO_TID_STATS] = {NLA_NESTED};
    stats_policy[NL80211_STA_INFO_RX_DURATION] = {NLA_U64};
    stats_policy[NL80211_STA_INFO_PAD] = {NLA_U64};

//    stats_policy[NL80211_STA_INFO_ACK_SIGNAL] = {NLA_U8};
//    stats_policy[NL80211_STA_INFO_ACK_SIGNAL_AVG] = {NLA_S8};
 //   stats_policy[NL80211_STA_INFO_RX_MPDUS] = {NLA_U32};
//    stats_policy[NL80211_STA_INFO_FCS_ERROR_COUNT] = {NLA_U32}; // can be inaccurate
//    stats_policy[NL80211_STA_INFO_CONNECTED_TO_GATE] = {NLA_U8};
//    stats_policy[NL80211_STA_INFO_TX_DURATION] = {NLA_U64};
//    stats_policy[NL80211_STA_INFO_AIRTIME_WEIGHT] = {NLA_U16};
//    stats_policy[NL80211_STA_INFO_AIRTIME_LINK_METRIC] = {NLA_U32};
//    stats_policy[NL80211_STA_INFO_ASSOC_AT_BOOTTIME] = {NLA_U32};

    struct nla_policy rate_policy[NL80211_RATE_INFO_MAX + 1] = {0};
    rate_policy[NL80211_RATE_INFO_BITRATE] = {NLA_U16}; //100 kbps
    rate_policy[NL80211_RATE_INFO_MCS] = {NLA_U8};
    rate_policy[NL80211_RATE_INFO_40_MHZ_WIDTH] = {NLA_FLAG};
    rate_policy[NL80211_RATE_INFO_SHORT_GI] = {NLA_FLAG};
    rate_policy[NL80211_RATE_INFO_BITRATE32] = {NLA_U32};
    rate_policy[NL80211_RATE_INFO_VHT_MCS] = {NLA_U8};
    rate_policy[NL80211_RATE_INFO_VHT_NSS] = {NLA_U8};
    rate_policy[NL80211_RATE_INFO_80_MHZ_WIDTH] = {NLA_FLAG};
    rate_policy[NL80211_RATE_INFO_160_MHZ_WIDTH] = {NLA_FLAG};
    rate_policy[NL80211_RATE_INFO_10_MHZ_WIDTH] = {NLA_FLAG};
    rate_policy[NL80211_RATE_INFO_5_MHZ_WIDTH] = {NLA_FLAG};
//    rate_policy[NL80211_RATE_INFO_HE_MCS] = {NLA_U8};
//    rate_policy[NL80211_RATE_INFO_HE_NSS] = {NLA_U8};
//    rate_policy[NL80211_RATE_INFO_HE_GI] = {NLA_U8};
//    rate_policy[NL80211_RATE_INFO_HE_DCM] = {NLA_U8};
//    rate_policy[NL80211_RATE_INFO_HE_RU_ALLOC] = {NLA_U8};

    struct nla_policy bss_policy[NL80211_STA_BSS_PARAM_MAX + 1] = {0};
    bss_policy[NL80211_STA_BSS_PARAM_CTS_PROT] = {NLA_FLAG};
    bss_policy[NL80211_STA_BSS_PARAM_SHORT_PREAMBLE] = {NLA_FLAG};
    bss_policy[NL80211_STA_BSS_PARAM_SHORT_SLOT_TIME] = {NLA_FLAG};
    bss_policy[NL80211_STA_BSS_PARAM_DTIM_PERIOD] = {NLA_U8};
    bss_policy[NL80211_STA_BSS_PARAM_BEACON_INTERVAL] = {NLA_U16};

    struct nla_policy tid_policy[NL80211_TID_STATS_MAX + 1] = {0};
    tid_policy[NL80211_TID_STATS_RX_MSDU] = {NLA_U64};
    tid_policy[NL80211_TID_STATS_TX_MSDU] = {NLA_U64};
    tid_policy[NL80211_TID_STATS_TX_MSDU_RETRIES] = {NLA_U64};
    tid_policy[NL80211_TID_STATS_TX_MSDU_FAILED] = {NLA_U64};
    tid_policy[NL80211_TID_STATS_PAD] = {NLA_FLAG};
//    tid_policy[NL80211_TID_STATS_TXQ_STATS] = {NLA_NESTED};
//
//    struct nla_policy txq_policy[NL80211_TXQ_STATS_MAX + 1] = {0};
//    txq_policy[NL80211_TXQ_STATS_BACKLOG_BYTES] = {NLA_U64};
//    txq_policy[NL80211_TXQ_STATS_BACKLOG_PACKETS] = {NLA_U64};
//    txq_policy[NL80211_TXQ_STATS_FLOWS] = {NLA_U64};
//    txq_policy[NL80211_TXQ_STATS_DROPS] = {NLA_U64};
//    txq_policy[NL80211_TXQ_STATS_ECN_MARKS] = {NLA_U64};
//    txq_policy[NL80211_TXQ_STATS_OVERLIMIT] = {NLA_U64};
//    txq_policy[NL80211_TXQ_STATS_OVERMEMORY] = {NLA_U64};
//    txq_policy[NL80211_TXQ_STATS_COLLISIONS] = {NLA_U64};
//    txq_policy[NL80211_TXQ_STATS_TX_BYTES] = {NLA_U64};
//    txq_policy[NL80211_TXQ_STATS_TX_PACKETS] = {NLA_U64};

    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    struct genlmsghdr *gnlh = (genlmsghdr *) nlmsg_data(nlmsg_hdr(msg));
    struct nlattr *sinfo[NL80211_STA_INFO_MAX + 1];
    struct nlattr *rinfo[NL80211_RATE_INFO_MAX + 1];
    struct nlattr *bss_info[NL80211_STA_BSS_PARAM_MAX + 1];
    struct nlattr *tid_info[NL80211_TID_STATS_MAX + 1];
//    struct nlattr *txq_info[NL80211_TXQ_STATS_MAX + 1];

	std::cout << "[DEBUG] ChannelMonitor::getWifiInfo_callback 1" << std::endl; 

    //nl_msg_dump(msg, stdout);
    nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0), genlmsg_attrlen(gnlh, 0), NULL);

    if (!tb[NL80211_ATTR_STA_INFO]) {
        std::cerr << "sta stats missing!" << std::endl;
        return NL_SKIP;
    }

    if (nla_parse_nested(sinfo, NL80211_STA_INFO_MAX,
                         tb[NL80211_ATTR_STA_INFO], stats_policy)) {
        std::cerr << "failed to parse nested attributes!" << std::endl;
        return NL_SKIP;
    }

    WifiInfo *wifi = (WifiInfo *) arg;

    if (sinfo[NL80211_STA_INFO_TX_BITRATE]) {
        if (nla_parse_nested(rinfo, NL80211_RATE_INFO_MAX, sinfo[NL80211_STA_INFO_TX_BITRATE], rate_policy)) {
            std::cerr << "failed to parse nested rate attributes!" << std::endl;
        } else {
            if (rinfo[NL80211_RATE_INFO_BITRATE])
                wifi->tx_bitrate = nla_get_u16(rinfo[NL80211_RATE_INFO_BITRATE]);
            if (rinfo[NL80211_RATE_INFO_BITRATE32])
                wifi->tx_bitrate32 = nla_get_u32(rinfo[NL80211_RATE_INFO_BITRATE32]);
            if (rinfo[NL80211_RATE_INFO_MCS])
                wifi->msc = nla_get_u8(rinfo[NL80211_RATE_INFO_MCS]);
            if (rinfo[NL80211_RATE_INFO_SHORT_GI])
                wifi->short_gi = nla_get_flag(rinfo[NL80211_RATE_INFO_SHORT_GI]);
            if (rinfo[NL80211_RATE_INFO_5_MHZ_WIDTH])
                wifi->f5_mhz_width = nla_get_flag(rinfo[NL80211_RATE_INFO_5_MHZ_WIDTH]);
            if (rinfo[NL80211_RATE_INFO_10_MHZ_WIDTH])
                wifi->f10_mhz_width = nla_get_flag(rinfo[NL80211_RATE_INFO_10_MHZ_WIDTH]);
            if (rinfo[NL80211_RATE_INFO_40_MHZ_WIDTH])
                wifi->f40_mhz_width = nla_get_flag(rinfo[NL80211_RATE_INFO_40_MHZ_WIDTH]);
            if (rinfo[NL80211_RATE_INFO_80_MHZ_WIDTH])
                wifi->f80_mhz_width = nla_get_flag(rinfo[NL80211_RATE_INFO_80_MHZ_WIDTH]);
            if (rinfo[NL80211_RATE_INFO_80P80_MHZ_WIDTH])
                wifi->f80p80_mhz_width = nla_get_flag(rinfo[NL80211_RATE_INFO_80P80_MHZ_WIDTH]);
            if (rinfo[NL80211_RATE_INFO_160_MHZ_WIDTH])
                wifi->f160_mhz_width = nla_get_flag(rinfo[NL80211_RATE_INFO_160_MHZ_WIDTH]);
            if (rinfo[NL80211_RATE_INFO_VHT_MCS])
                wifi->vht_mcs = nla_get_u8(rinfo[NL80211_RATE_INFO_VHT_MCS]);
            if (rinfo[NL80211_RATE_INFO_VHT_NSS])
                wifi->vht_nss = nla_get_u8(rinfo[NL80211_RATE_INFO_VHT_NSS]);
//            if (rinfo[NL80211_RATE_INFO_HE_MCS])
//                wifi->he_mcs = nla_get_u8(rinfo[NL80211_RATE_INFO_HE_MCS]);
//            if (rinfo[NL80211_RATE_INFO_HE_NSS])
//                wifi->he_nss = nla_get_u8(rinfo[NL80211_RATE_INFO_HE_NSS]);
//            if (rinfo[NL80211_RATE_INFO_HE_GI])
//                wifi->he_gi = nla_get_u8(rinfo[NL80211_RATE_INFO_HE_GI]);
//            if (rinfo[NL80211_RATE_INFO_HE_DCM])
//                wifi->he_dcm = nla_get_u8(rinfo[NL80211_RATE_INFO_HE_DCM]);
//            if (rinfo[NL80211_RATE_INFO_HE_RU_ALLOC])
//                wifi->he_ru_alloc = nla_get_u8(rinfo[NL80211_RATE_INFO_HE_RU_ALLOC]);
        }
    }

    if (sinfo[NL80211_STA_INFO_BSS_PARAM]) {
        if (nla_parse_nested(bss_info, NL80211_BSS_MAX, sinfo[NL80211_STA_INFO_BSS_PARAM], bss_policy)) {
            std::cerr << "failed to parse nested bss param attributes!" << std::endl;
        } else {
            if (bss_info[NL80211_STA_BSS_PARAM_CTS_PROT])
                wifi->cts_protection = nla_get_flag(bss_info[NL80211_STA_BSS_PARAM_CTS_PROT]);
            if (bss_info[NL80211_STA_BSS_PARAM_SHORT_PREAMBLE])
                wifi->short_preamble = nla_get_flag(bss_info[NL80211_STA_BSS_PARAM_SHORT_PREAMBLE]);
            if (bss_info[NL80211_STA_BSS_PARAM_SHORT_SLOT_TIME])
                wifi->short_slot_time = nla_get_flag(bss_info[NL80211_STA_BSS_PARAM_SHORT_SLOT_TIME]);
            if (bss_info[NL80211_STA_BSS_PARAM_DTIM_PERIOD])
                wifi->dtim_period = nla_get_u8(bss_info[NL80211_STA_BSS_PARAM_DTIM_PERIOD]);
            if (bss_info[NL80211_STA_BSS_PARAM_BEACON_INTERVAL])
                wifi->beacon_interval = nla_get_u16(bss_info[NL80211_STA_BSS_PARAM_BEACON_INTERVAL]);
        }
    }

    if (sinfo[NL80211_STA_INFO_TID_STATS]) {
        if (nla_parse_nested(tid_info, NL80211_TID_STATS_MAX, sinfo[NL80211_STA_INFO_TID_STATS], tid_policy)) {
            std::cerr << "failed to parse nested tid stats attributes!" << std::endl;
        } else {
//            if (tid_info[NL80211_TID_STATS_TXQ_STATS]) {
//                parse_txq_stats(tid_info[NL80211_TID_STATS_TXQ_STATS], wifi);
//            }
            if (tid_info[NL80211_TID_STATS_RX_MSDU])
                wifi->tid_rx_msdu = nla_get_u64(tid_info[NL80211_TID_STATS_RX_MSDU]);
            if (tid_info[NL80211_TID_STATS_TX_MSDU])
                wifi->tid_tx_msdu = nla_get_u64(tid_info[NL80211_TID_STATS_TX_MSDU]);
            if (tid_info[NL80211_TID_STATS_TX_MSDU_RETRIES])
                wifi->tid_tx_msdu_retries = nla_get_u64(tid_info[NL80211_TID_STATS_TX_MSDU_RETRIES]);
            if (tid_info[NL80211_TID_STATS_TX_MSDU_FAILED])
                wifi->tid_tx_msdu_failed = nla_get_u64(tid_info[NL80211_TID_STATS_TX_MSDU_FAILED]);
            if (tid_info[NL80211_TID_STATS_PAD])
                wifi->tid_pad = nla_get_flag(tid_info[NL80211_TID_STATS_PAD]);
        }
    }

    if (sinfo[NL80211_STA_INFO_INACTIVE_TIME])
        wifi->inactive_time = nla_get_u32(sinfo[NL80211_STA_INFO_INACTIVE_TIME]);
    if (sinfo[NL80211_STA_INFO_RX_BYTES])
        wifi->rx_bytes = nla_get_u32(sinfo[NL80211_STA_INFO_RX_BYTES]);
    if (sinfo[NL80211_STA_INFO_TX_BYTES])
        wifi->tx_bytes = nla_get_u32(sinfo[NL80211_STA_INFO_TX_BYTES]);
    if (sinfo[NL80211_STA_INFO_RX_BYTES64])
        wifi->rx_bytes_64 = nla_get_u64(sinfo[NL80211_STA_INFO_RX_BYTES64]);
    if (sinfo[NL80211_STA_INFO_TX_BYTES64])
        wifi->tx_bytes_64 = nla_get_u64(sinfo[NL80211_STA_INFO_TX_BYTES64]);
    if (sinfo[NL80211_STA_INFO_SIGNAL])
        wifi->signal = nla_get_u8(sinfo[NL80211_STA_INFO_SIGNAL]);
    if (sinfo[NL80211_STA_INFO_RX_PACKETS])
        wifi->rx_packets = nla_get_u32(sinfo[NL80211_STA_INFO_RX_PACKETS]);
    if (sinfo[NL80211_STA_INFO_TX_PACKETS])
        wifi->tx_packets = nla_get_u32(sinfo[NL80211_STA_INFO_TX_PACKETS]);
    if (sinfo[NL80211_STA_INFO_TX_RETRIES])
        wifi->tx_retries = nla_get_u32(sinfo[NL80211_STA_INFO_TX_RETRIES]);

    if (sinfo[NL80211_STA_INFO_TX_FAILED])
        wifi->tx_failed = nla_get_u32(sinfo[NL80211_STA_INFO_TX_FAILED]);
    if (sinfo[NL80211_STA_INFO_SIGNAL_AVG])
        wifi->signal_avg = nla_get_u8(sinfo[NL80211_STA_INFO_SIGNAL_AVG]);
    if (sinfo[NL80211_STA_INFO_LLID])
        wifi->llid = nla_get_u16(sinfo[NL80211_STA_INFO_LLID]);
    if (sinfo[NL80211_STA_INFO_PLID])
        wifi->plid = nla_get_u16(sinfo[NL80211_STA_INFO_PLID]);
    if (sinfo[NL80211_STA_INFO_PLINK_STATE])
        wifi->plink_state = nla_get_u8(sinfo[NL80211_STA_INFO_PLINK_STATE]);
    if (sinfo[NL80211_STA_INFO_CONNECTED_TIME])
        wifi->connected_time = nla_get_u32(sinfo[NL80211_STA_INFO_CONNECTED_TIME]);
    if (sinfo[NL80211_STA_INFO_BEACON_LOSS])
        wifi->beacon_loss = nla_get_u32(sinfo[NL80211_STA_INFO_BEACON_LOSS]);
    if (sinfo[NL80211_STA_INFO_T_OFFSET])
        wifi->t_offset = nla_get_u32(sinfo[NL80211_STA_INFO_T_OFFSET]);

    if (sinfo[NL80211_STA_INFO_LOCAL_PM])
        wifi->local_pm = nla_get_u32(sinfo[NL80211_STA_INFO_LOCAL_PM]);
    if (sinfo[NL80211_STA_INFO_PEER_PM])
        wifi->peer_pm = nla_get_u32(sinfo[NL80211_STA_INFO_PEER_PM]);
    if (sinfo[NL80211_STA_INFO_NONPEER_PM])
        wifi->non_peer_pm = nla_get_u32(sinfo[NL80211_STA_INFO_NONPEER_PM]);
    if (sinfo[NL80211_STA_INFO_CHAIN_SIGNAL])
        wifi->chain_signal = nla_get_u32(sinfo[NL80211_STA_INFO_CHAIN_SIGNAL]);
    if (sinfo[NL80211_STA_INFO_CHAIN_SIGNAL_AVG])
        wifi->chain_signal_avg = nla_get_u32(sinfo[NL80211_STA_INFO_CHAIN_SIGNAL_AVG]);

    if (sinfo[NL80211_STA_INFO_EXPECTED_THROUGHPUT])
        wifi->expected_throughput = nla_get_u32(sinfo[NL80211_STA_INFO_EXPECTED_THROUGHPUT]);
    if (sinfo[NL80211_STA_INFO_RX_DROP_MISC])
        wifi->rx_drop_misc = nla_get_u64(sinfo[NL80211_STA_INFO_RX_DROP_MISC]);
    if (sinfo[NL80211_STA_INFO_BEACON_RX])
        wifi->beacon_rx = nla_get_u64(sinfo[NL80211_STA_INFO_BEACON_RX]);
    if (sinfo[NL80211_STA_INFO_BEACON_SIGNAL_AVG])
        wifi->beacon_signal_avg = nla_get_u8(sinfo[NL80211_STA_INFO_BEACON_SIGNAL_AVG]);
    if (sinfo[NL80211_STA_INFO_RX_DURATION])
        wifi->rx_duration = nla_get_u64(sinfo[NL80211_STA_INFO_RX_DURATION]);
    if (sinfo[NL80211_STA_INFO_PAD])
        wifi->sta_pad = nla_get_u64(sinfo[NL80211_STA_INFO_PAD]);

//    if (sinfo[NL80211_STA_INFO_ACK_SIGNAL])
//        wifi->ack_signal = nla_get_u8(sinfo[NL80211_STA_INFO_ACK_SIGNAL]);
//    if (sinfo[NL80211_STA_INFO_ACK_SIGNAL_AVG])
//        wifi->ack_signal_avg = nla_get_s8(sinfo[NL80211_STA_INFO_ACK_SIGNAL_AVG]);
//    if (sinfo[NL80211_STA_INFO_RX_MPDUS])
//        wifi->rx_mpdus = nla_get_u32(sinfo[NL80211_STA_INFO_RX_MPDUS]);
//    if (sinfo[NL80211_STA_INFO_FCS_ERROR_COUNT])
//        wifi->fcs_error_count = nla_get_u32(sinfo[NL80211_STA_INFO_FCS_ERROR_COUNT]);
//    if (sinfo[NL80211_STA_INFO_CONNECTED_TO_GATE])
//        wifi->connected_to_gate = nla_get_u8(sinfo[NL80211_STA_INFO_CONNECTED_TO_GATE]);
//    if (sinfo[NL80211_STA_INFO_TX_DURATION])
//        wifi->tx_duration = nla_get_u64(sinfo[NL80211_STA_INFO_TX_DURATION]);
//    if (sinfo[NL80211_STA_INFO_AIRTIME_WEIGHT])
//        wifi->airtime_weight = nla_get_u16(sinfo[NL80211_STA_INFO_AIRTIME_WEIGHT]);
//    if (sinfo[NL80211_STA_INFO_AIRTIME_LINK_METRIC])
//        wifi->airtime_link_metric = nla_get_u32(sinfo[NL80211_STA_INFO_AIRTIME_LINK_METRIC]);
//    if (sinfo[NL80211_STA_INFO_ASSOC_AT_BOOTTIME])
//        wifi->assoc_at_boottime = nla_get_u32(sinfo[NL80211_STA_INFO_ASSOC_AT_BOOTTIME]);

    return NL_SKIP;
}

static int getSurvey_callback(struct nl_msg *msg, void *arg) {
    struct nla_policy survey_policy[NL80211_SURVEY_INFO_MAX + 1] = {0};
    survey_policy[NL80211_SURVEY_INFO_FREQUENCY] = {NLA_U32};
    survey_policy[NL80211_SURVEY_INFO_NOISE] = {NLA_U8};
    survey_policy[NL80211_SURVEY_INFO_IN_USE] = {NLA_U64};
    survey_policy[NL80211_SURVEY_INFO_TIME] = {NLA_U64};
    survey_policy[NL80211_SURVEY_INFO_TIME_BUSY] = {NLA_U64};
    survey_policy[NL80211_SURVEY_INFO_TIME_EXT_BUSY] = {NLA_U64};
    survey_policy[NL80211_SURVEY_INFO_TIME_RX] = {NLA_U64};
    survey_policy[NL80211_SURVEY_INFO_TIME_TX] = {NLA_U64};
    survey_policy[NL80211_SURVEY_INFO_TIME_SCAN] = {NLA_U64};
//    survey_policy[NL80211_SURVEY_INFO_TIME_BSS_RX] = {NLA_U64};

    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    struct genlmsghdr *gnlh = (genlmsghdr *) nlmsg_data(nlmsg_hdr(msg));
    struct nlattr *survey[NL80211_SURVEY_INFO_MAX + 1];
    nla_parse(tb, NL80211_SURVEY_INFO_MAX, genlmsg_attrdata(gnlh, 0), genlmsg_attrlen(gnlh, 0), NULL);

    if (!tb[NL80211_ATTR_SURVEY_INFO]) {
        std::cerr << "survey stats missing!" << std::endl;
        return NL_SKIP;
    }

    if (nla_parse_nested(survey, NL80211_SURVEY_INFO_MAX,
                         tb[NL80211_ATTR_SURVEY_INFO], survey_policy)) {
        std::cerr << "failed to parse survey nested attributes!" << std::endl;
        return NL_SKIP;
    }

    WifiInfo *wifi = (WifiInfo *) arg;

    if (survey[NL80211_SURVEY_INFO_FREQUENCY]) {
        //Frequency in MHz
        wifi->surv_frequency = nla_get_u32(survey[NL80211_SURVEY_INFO_FREQUENCY]);
    }
    if (survey[NL80211_SURVEY_INFO_NOISE]) {
        //Noise in dBm
        wifi->surv_noise = (int8_t) nla_get_u8(survey[NL80211_SURVEY_INFO_NOISE]);
    }
    if (survey[NL80211_SURVEY_INFO_IN_USE]) {
        //If the frequency is in use
        wifi->surv_in_use = survey[NL80211_SURVEY_INFO_IN_USE] ? 1 : 0;
    }
    if (survey[NL80211_SURVEY_INFO_TIME]) {
        //channel active time in ms
        wifi->surv_time = nla_get_u64(survey[NL80211_SURVEY_INFO_TIME]);
    }
    if (survey[NL80211_SURVEY_INFO_TIME_BUSY]) {
        //channel busy time in ms
        wifi->surv_time_busy = nla_get_u64(survey[NL80211_SURVEY_INFO_TIME_BUSY]);
    }
    if (survey[NL80211_SURVEY_INFO_TIME_EXT_BUSY]) {
        //extension channel busy time in ms
        wifi->surv_time_ext_busy = nla_get_u64(survey[NL80211_SURVEY_INFO_TIME_EXT_BUSY]);
    }
    if (survey[NL80211_SURVEY_INFO_TIME_RX]) {
        //channel receive time in ms
        wifi->surv_time_rx = nla_get_u64(survey[NL80211_SURVEY_INFO_TIME_RX]);
    }
    if (survey[NL80211_SURVEY_INFO_TIME_TX]) {
        //channel transmit time in ms
        wifi->surv_time_tx = nla_get_u64(survey[NL80211_SURVEY_INFO_TIME_TX]);
    }
    if (survey[NL80211_SURVEY_INFO_TIME_SCAN]) {
        wifi->surv_time_scan = nla_get_u64(survey[NL80211_SURVEY_INFO_TIME_SCAN]);
    }
 //   if (survey[NL80211_SURVEY_INFO_TIME_BSS_RX]) {
 //       wifi->surv_time_bss_rx = nla_get_u64(survey[NL80211_SURVEY_INFO_TIME_BSS_RX]);
 //   }

    return NL_SKIP;
}

static int getWifiStatus(NetlinkInfo *nl, WifiInfo *wifi) {
    nl->result1 = 1;
    nl->result2 = 1;
    nl->result3 = 1;

    if (wifi->ifindex < 0) {
        return -1;
    }

std::cout << "[DEBUG] ChannelMonitor::getWifiStatus 0" << std::endl;

    struct nl_msg *msg2 = nlmsg_alloc();
    if (!msg2) {
        std::cerr << "[ERROR] Failed to allocate netlink message." << std::endl;
        return -2;
    }
    //This will fetch the interface signal strength and transmit bitrate
    genlmsg_put(msg2, NL_AUTO_PORT, NL_AUTO_SEQ, nl->id, 0, NLM_F_DUMP, NL80211_CMD_GET_STATION, 0);
    nla_put_u32(msg2, NL80211_ATTR_IFINDEX, wifi->ifindex);
    nl_send_auto(nl->socket, msg2);
    while (nl->result2 > 0) {
        nl_recvmsgs(nl->socket, nl->cb2);
    }
    nlmsg_free(msg2);
	
std::cout << "[DEBUG] ChannelMonitor::getWifiStatus 1" << std::endl;

    //SURVEY
    struct nl_msg *msg1 = nlmsg_alloc();
    if (!msg1) {
        std::cerr << "[ERROR] Failed to allocate netlink message." << std::endl;
        return -2;
    }
    genlmsg_put(msg1, NL_AUTO_PORT, NL_AUTO_SEQ, nl->id, 0, NLM_F_DUMP, NL80211_CMD_GET_SURVEY, 0);
    nla_put_u32(msg1, NL80211_ATTR_IFINDEX, wifi->ifindex);
    nl_send_auto(nl->socket, msg1);
    while (nl->result1 > 0) {
        nl_recvmsgs(nl->socket, nl->cb1);
    }
    nlmsg_free(msg1);

std::cout << "[DEBUG] ChannelMonitor::getWifiStatus 2" << std::endl;
    //INTERFACE
    struct nl_msg *msg3 = nlmsg_alloc();
    if (!msg3) {
        std::cerr << "[ERROR] Failed to allocate netlink message." << std::endl;
        return -2;
    }
    genlmsg_put(msg3, NL_AUTO_PORT, NL_AUTO_SEQ, nl->id, 0, NLM_F_DUMP, NL80211_CMD_GET_INTERFACE, 0);
    nla_put_u32(msg3, NL80211_ATTR_IFINDEX, wifi->ifindex);
    nl_send_auto(nl->socket, msg3);
    while (nl->result3 > 0) {
        nl_recvmsgs(nl->socket, nl->cb3);
    }
    nlmsg_free(msg3);

std::cout << "[DEBUG] ChannelMonitor::getWifiStatus 3" << std::endl;
    return 0;
}

static int initNl80211(NetlinkInfo &nl, WifiInfo &wifi) {
    nl.socket = nl_socket_alloc();
    if (!nl.socket) {
        std::cout << "[ERROR] Failed to allocate netlink socket." << std::endl;
        return -ENOMEM;
    }

    //nl_socket_set_buffer_size(nl.socket, 8192, 8192);

    if (genl_connect(nl.socket)) {
        std::cout << "[ERROR] Failed to connect to netlink socket." << std::endl;
        nl_close(nl.socket);
        nl_socket_free(nl.socket);
        return -ENOLINK;
    }

    nl.id = genl_ctrl_resolve(nl.socket, "nl80211");
    if (nl.id < 0) {
        std::cout << "[ERROR] Nl80211 interface not found." << std::endl;
        nl_close(nl.socket);
        nl_socket_free(nl.socket);
        return -ENOENT;
    }

    nl.cb1 = nl_cb_alloc(NL_CB_DEFAULT);
    nl.cb2 = nl_cb_alloc(NL_CB_DEFAULT);
    nl.cb3 = nl_cb_alloc(NL_CB_DEFAULT);

    if ((!nl.cb2) || (!nl.cb1) || (!nl.cb3)) {
        std::cout << "[ERROR] Failed to allocate netlink callback." << std::endl;
        nl_close(nl.socket);
        nl_socket_free(nl.socket);
        return -ENOMEM;
    }

    nl_cb_set(nl.cb1, NL_CB_VALID, NL_CB_CUSTOM, getSurvey_callback, &wifi);
    nl_cb_set(nl.cb1, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &(nl.result1));

    nl_cb_set(nl.cb2, NL_CB_VALID, NL_CB_CUSTOM, getWifiInfo_callback, &wifi);
    nl_cb_set(nl.cb2, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &(nl.result2));

    nl_cb_set(nl.cb3, NL_CB_VALID, NL_CB_CUSTOM, getInterfaceInfo_callback, &wifi);
    nl_cb_set(nl.cb3, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &(nl.result3));

    return nl.id;
}

ChannelMonitor::ChannelMonitor(
        std::string const &configFname)
        : databaseManager(), endProgram_(false), samplingInterval(SAMPLING_INTERVAL_DEF), ifnames() {
    this->configure(configFname);
}

void ChannelMonitor::configure(std::string const &configFname) {
    ConfigFile configFile(configFname);
    this->databaseManager.configure(configFile);

    // configure iface to receive feedback messages
    this->samplingInterval = SAMPLING_INTERVAL_DEF;
    try {
        this->samplingInterval = std::stoi(configFile.Value("channel-monitor", "sampling-interval").c_str());
    } catch (char const *str) {
        std::stringstream ss;
        ss << "Config exception: section=channel-monitor, value=sampling-interval " << str
        << " using default value " << this->samplingInterval;
        LOG_ERR(ss.str().c_str());
    }

    std::vector<std::string> aux_ifnames = WiperfUtility::readIfnames(configFile, "channel-monitor");
    this->ifnames.insert(this->ifnames.end(), aux_ifnames.begin(), aux_ifnames.end());
}

void ChannelMonitor::stopThread() {
    std::cout << "[INFO] Stopping ChannelMonitor thread!" << std::endl;
    endProgram_ = true;
}

void ChannelMonitor::run() {
    std::string gpsShmPath = dataSender->getGpsShmPath();
    //Use GPS tp get timestamp!
    GpsInfo *gpsInfo = WiperfUtility::getGpsInfo(gpsShmPath);

    std::vector<NetlinkInfo> netlinkVector;
    std::vector<WifiInfo> wifiVector;

	for (auto & it : this->ifnames) {
        WifiInfo wifiEntry{};
        NetlinkInfo netlinkEntry{};

        wifiEntry.ifname = it;
        wifiEntry.ifindex = if_nametoindex(it.c_str());
		
        wifiVector.push_back(wifiEntry);
        netlinkVector.push_back(netlinkEntry);
	}

	for (int i = 0; i < netlinkVector.size(); ++i) {
		NetlinkInfo &netlinkEntry = netlinkVector.at(i);
		WifiInfo &wifiEntry = wifiVector.at(i);

        netlinkEntry.id = initNl80211(netlinkEntry, wifiEntry);
        if (netlinkEntry.id < 0) {
            std::cout << "[ERROR] Error initializing netlink 802.11." << std::endl;
            return;
        }
    }

    //Needed to synchronize to only send on the 100 ms mark (instead of sending at 1320 ms,
    // send at 1400 ms).
    //uint64_t now0 = WiperfUtility::getCurrentMillis(gpsInfo);
    std::chrono::milliseconds now0_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch());
    uint64_t now0 = now0_ms.count();

    uint64_t sleepingInterval = this->samplingInterval - (now0 % this->samplingInterval);
    std::this_thread::sleep_for(std::chrono::milliseconds(sleepingInterval));

    while (!endProgram_) {

        //1. Compute the signal information
        //2. Construct database information object
        //3. Update database

		//std::cout << "[DEBUG] ChannelMonitor::run while 20" << std::endl;
        for (int i = 0; i < wifiVector.size(); ++i) {
        	NetlinkInfo &netlinkInfo = netlinkVector.at(i);
            WifiInfo &wifiInfo = wifiVector.at(i);

            //Gather information
            getWifiStatus(&netlinkInfo, &wifiInfo);
        }

        //Get the timestamp and the position that corresponds to the
        // collected RAN data.
        GpsInfo currentInfo = WiperfUtility::getCurrentGps(gpsInfo);
        std::chrono::milliseconds currentTime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch());
        uint64_t timestamp = currentTime_ms.count();//currentInfo.systime;

        //Round the timestamp to the nearest 10 ms
        uint64_t timestampRound = (uint64_t) (timestamp / this->samplingInterval);
        if ((int) timestamp % this->samplingInterval > 0)
            timestampRound++;

        timestamp = timestampRound * this->samplingInterval;

        //double latitude = static_cast<double>(currentInfo.lat);
        double latitude = 0;
        //double longitude = static_cast<double>(currentInfo.lon);
        double longitude = 0;
        //double speed = static_cast<double>(currentInfo.speed);
        double speed = 0;
        //double orientation = static_cast<double>(currentInfo.head);
        double orientation = 0;
        //If vehicle is moving at less than 0.5 kmph, we consider that it stopped?
        int moving = speed > 0.5;

        std::vector<DatabaseInfo> databaseInfoVector;

        for (auto & wifiInfo : wifiVector) {
            std::string codedWifiInfo = codeWifiInfo(wifiInfo);

            DatabaseInfo databaseInfo{};

            uint64_t timestamp = timestamp - (timestamp % 10);

            databaseInfo.timestamp = timestamp;
            databaseInfo.rat = wifiInfo.ifname;//wifiInfo.ifname;
            databaseInfo.channelInfo = codedWifiInfo;
            databaseInfo.latitude = latitude;
            databaseInfo.longitude = longitude;
            databaseInfo.speed = speed;
            databaseInfo.orientation = orientation;
            databaseInfo.moving = moving;

            databaseInfo.tx_bitrate = wifiInfo.tx_bitrate;
            databaseInfo.signal_strength = wifiInfo.signal - 256;  //convert positive to negative

            databaseInfoVector.push_back(databaseInfo);
        }

        this->databaseManager.createAll(databaseInfoVector);

        //uint64_t now1 = WiperfUtility::getCurrentMillis(gpsInfo);
        std::chrono::milliseconds now1_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch());
        uint64_t now1 = now1_ms.count();
        sleepingInterval = this->samplingInterval - (now1 % this->samplingInterval);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepingInterval));
    }
}

std::string ChannelMonitor::codeWifiInfo(const WifiInfo& wifi) {
    std::string str;

    str.append(wifi.ifname);
    str.append(",");
    str.append(std::to_string(wifi.ifindex));
    str.append(",");
    str.append(std::to_string(wifi.inactive_time));
    str.append(",");
    str.append(std::to_string(wifi.rx_bytes));
    str.append(",");
    str.append(std::to_string(wifi.tx_bytes));
    str.append(",");
    str.append(std::to_string(wifi.rx_bytes_64));
    str.append(",");
    str.append(std::to_string(wifi.tx_bytes_64));
    str.append(",");
    str.append(std::to_string(wifi.signal));
    str.append(",");
    str.append(std::to_string(wifi.rx_packets));
    str.append(",");
    str.append(std::to_string(wifi.tx_packets));
    str.append(",");
    str.append(std::to_string(wifi.tx_retries));
    str.append(",");
    str.append(std::to_string(wifi.tx_failed));
    str.append(",");
    str.append(std::to_string(wifi.signal_avg));
    str.append(",");
    str.append(std::to_string(wifi.llid));
    str.append(",");
    str.append(std::to_string(wifi.plid));
    str.append(",");
    str.append(std::to_string(wifi.plink_state));
    str.append(",");
    str.append(std::to_string(wifi.connected_time));
    str.append(",");

    str.append(std::to_string(wifi.beacon_loss));
    str.append(",");
    str.append(std::to_string(wifi.t_offset));
    str.append(",");
    str.append(std::to_string(wifi.local_pm));
    str.append(",");
    str.append(std::to_string(wifi.peer_pm));
    str.append(",");
    str.append(std::to_string(wifi.non_peer_pm));
    str.append(",");
    str.append(std::to_string(wifi.chain_signal));
    str.append(",");
    str.append(std::to_string(wifi.chain_signal_avg));
    str.append(",");
    str.append(std::to_string(wifi.expected_throughput));
    str.append(",");
    str.append(std::to_string(wifi.rx_drop_misc));
    str.append(",");
    str.append(std::to_string(wifi.beacon_rx));
    str.append(",");
    str.append(std::to_string(wifi.beacon_signal_avg));
    str.append(",");
    str.append(std::to_string(wifi.rx_duration));
    str.append(",");
    str.append(std::to_string(wifi.sta_pad));
    str.append(",");
    str.append(std::to_string(wifi.ack_signal));
    str.append(",");
    str.append(std::to_string(wifi.ack_signal_avg));
    str.append(",");
    str.append(std::to_string(wifi.rx_mpdus));
    str.append(",");
    str.append(std::to_string(wifi.fcs_error_count));
    str.append(",");
    str.append(std::to_string(wifi.connected_to_gate));
    str.append(",");
    str.append(std::to_string(wifi.tx_duration));
    str.append(",");

    str.append(std::to_string(wifi.airtime_weight));
    str.append(",");
    str.append(std::to_string(wifi.airtime_link_metric));
    str.append(",");
    str.append(std::to_string(wifi.assoc_at_boottime));
    str.append(",");

    str.append(std::to_string(wifi.tx_bitrate));
    str.append(",");
    str.append(std::to_string(wifi.tx_bitrate32));
    str.append(",");
    str.append(std::to_string(wifi.msc));
    str.append(",");
    str.append(std::to_string(wifi.short_gi));
    str.append(",");
    str.append(std::to_string(wifi.f5_mhz_width));
    str.append(",");
    str.append(std::to_string(wifi.f10_mhz_width));
    str.append(",");
    str.append(std::to_string(wifi.f40_mhz_width));
    str.append(",");
    str.append(std::to_string(wifi.f80_mhz_width));
    str.append(",");
    str.append(std::to_string(wifi.f80p80_mhz_width));
    str.append(",");
    str.append(std::to_string(wifi.f160_mhz_width));
    str.append(",");
    str.append(std::to_string(wifi.vht_mcs));
    str.append(",");
    str.append(std::to_string(wifi.vht_nss));
    str.append(",");
    str.append(std::to_string(wifi.he_mcs));
    str.append(",");
    str.append(std::to_string(wifi.he_nss));
    str.append(",");
    str.append(std::to_string(wifi.he_gi));
    str.append(",");
    str.append(std::to_string(wifi.he_dcm));
    str.append(",");
    str.append(std::to_string(wifi.he_ru_alloc));
    str.append(",");

    str.append(std::to_string(wifi.tid_rx_msdu));
    str.append(",");
    str.append(std::to_string(wifi.tid_tx_msdu));
    str.append(",");
    str.append(std::to_string(wifi.tid_tx_msdu_retries));
    str.append(",");
    str.append(std::to_string(wifi.tid_tx_msdu_failed));
    str.append(",");
    str.append(std::to_string(wifi.tid_pad));
    str.append(",");

    str.append(std::to_string(wifi.txq_backlog_bytes));
    str.append(",");
    str.append(std::to_string(wifi.txq_backlog_packets));
    str.append(",");
    str.append(std::to_string(wifi.txq_flows));
    str.append(",");
    str.append(std::to_string(wifi.txq_drops));
    str.append(",");
    str.append(std::to_string(wifi.txq_ecn_marks));
    str.append(",");
    str.append(std::to_string(wifi.txq_overlimit));
    str.append(",");
    str.append(std::to_string(wifi.txq_overmemory));
    str.append(",");
    str.append(std::to_string(wifi.txq_collisions));
    str.append(",");
    str.append(std::to_string(wifi.txq_tx_bytes));
    str.append(",");
    str.append(std::to_string(wifi.txq_tx_packets));
    str.append(",");

    str.append(std::to_string(wifi.cts_protection));
    str.append(",");
    str.append(std::to_string(wifi.short_preamble));
    str.append(",");
    str.append(std::to_string(wifi.short_slot_time));
    str.append(",");
    str.append(std::to_string(wifi.dtim_period));
    str.append(",");
    str.append(std::to_string(wifi.beacon_interval));
    str.append(",");

    str.append(std::to_string(wifi.surv_frequency));
    str.append(",");
    str.append(std::to_string(wifi.surv_noise));
    str.append(",");
    str.append(std::to_string(wifi.surv_in_use));
    str.append(",");
    str.append(std::to_string(wifi.surv_time));
    str.append(",");
    str.append(std::to_string(wifi.surv_time_busy));
    str.append(",");
    str.append(std::to_string(wifi.surv_time_ext_busy));
    str.append(",");
    str.append(std::to_string(wifi.surv_time_rx));
    str.append(",");
    str.append(std::to_string(wifi.surv_time_tx));
    str.append(",");
    str.append(std::to_string(wifi.surv_time_scan));
    str.append(",");
    str.append(std::to_string(wifi.surv_time_bss_rx));
    str.append(",");

    str.append(std::to_string(wifi.iface_wiphy));
    str.append(",");
    str.append(std::to_string(wifi.iface_frequency));
    str.append(",");
    str.append(std::to_string(wifi.iface_channel));
    str.append(",");
    str.append(std::to_string(wifi.iface_channel_width));
    str.append(",");
    str.append(std::to_string(wifi.iface_center_freq1));
    str.append(",");
    str.append(std::to_string(wifi.iface_center_freq2));
    str.append(",");
    str.append(std::to_string(wifi.iface_channel_type));
    str.append(",");
    str.append(std::to_string(wifi.iface_tx_power));

    return str;
}

WifiInfo ChannelMonitor::decodeWifiInfo(const std::string& info) {
    std::stringstream ss(info);
    std::vector<std::string> array;

    while (ss.good()) {
        std::string substr;
        getline(ss, substr, ',');
        array.push_back(substr);
    }

    WifiInfo wifi;

    wifi.ifname = array[0];
    wifi.ifindex = std::stoi(array[1]);
    wifi.inactive_time = std::stoi(array[2]);
    wifi.rx_bytes = std::stoi(array[3]);
    wifi.tx_bytes = std::stoi(array[4]);
    wifi.rx_bytes_64 = std::stol(array[5]);
    wifi.tx_bytes_64 = std::stol(array[6]);
    wifi.signal = std::stoi(array[7]);
    wifi.rx_packets = std::stoi(array[8]);
    wifi.tx_packets = std::stoi(array[9]);
    wifi.tx_retries = std::stoi(array[10]);
    wifi.tx_failed = std::stoi(array[11]);
    wifi.signal_avg = std::stoi(array[12]);
    wifi.llid = std::stoi(array[13]);
    wifi.plid = std::stoi(array[14]);
    wifi.plink_state = std::stoi(array[15]);
    wifi.connected_time = std::stoi(array[16]);
    wifi.beacon_loss = std::stoi(array[17]);
    wifi.t_offset = std::stol(array[18]);
    wifi.local_pm = std::stoi(array[19]);
    wifi.peer_pm = std::stoi(array[20]);
    wifi.non_peer_pm = std::stoi(array[21]);
    wifi.chain_signal = std::stoi(array[22]);
    wifi.chain_signal_avg = std::stoi(array[23]);
    wifi.expected_throughput = std::stoi(array[24]);
    wifi.rx_drop_misc = std::stol(array[25]);
    wifi.beacon_rx = std::stol(array[26]);
    wifi.beacon_signal_avg = std::stoi(array[27]);
    wifi.rx_duration = std::stol(array[28]);
    wifi.sta_pad = std::stol(array[29]);
    wifi.ack_signal = std::stoi(array[30]);
    wifi.ack_signal_avg =(int8_t) std::stoi(array[31]);
    wifi.rx_mpdus = std::stoi(array[32]);
    wifi.fcs_error_count = std::stoi(array[33]);
    wifi.connected_to_gate = std::stoi(array[34]);
    wifi.tx_duration = std::stol(array[35]);
    wifi.airtime_weight = std::stoi(array[36]);
    wifi.airtime_link_metric = std::stoi(array[37]);
    wifi.assoc_at_boottime = std::stoi(array[38]);
    wifi.tx_bitrate = std::stoi(array[39]);
    wifi.tx_bitrate32 = std::stoi(array[40]);
    wifi.msc = std::stoi(array[41]);
    wifi.short_gi = std::stoi(array[42]);
    wifi.f5_mhz_width = std::stoi(array[43]);
    wifi.f10_mhz_width = std::stoi(array[44]);
    wifi.f40_mhz_width = std::stoi(array[45]);
    wifi.f80_mhz_width = std::stoi(array[46]);
    wifi.f80p80_mhz_width = std::stoi(array[47]);
    wifi.f160_mhz_width = std::stoi(array[48]);
    wifi.vht_mcs = std::stoi(array[49]);
    wifi.vht_nss = std::stoi(array[50]);
    wifi.he_mcs = std::stoi(array[51]);
    wifi.he_nss = std::stoi(array[52]);
    wifi.he_gi = std::stoi(array[53]);
    wifi.he_dcm = std::stoi(array[54]);
    wifi.he_ru_alloc = std::stoi(array[55]);
    wifi.tid_rx_msdu = std::stol(array[56]);
    wifi.tid_tx_msdu = std::stol(array[57]);
    wifi.tid_tx_msdu_retries = std::stol(array[58]);
    wifi.tid_tx_msdu_failed = std::stol(array[59]);
    wifi.tid_pad = std::stoi(array[60]);
    wifi.txq_backlog_bytes = std::stol(array[61]);
    wifi.txq_backlog_packets = std::stol(array[62]);
    wifi.txq_flows = std::stol(array[63]);
    wifi.txq_drops = std::stol(array[64]);
    wifi.txq_ecn_marks = std::stol(array[65]);
    wifi.txq_overlimit = std::stol(array[66]);
    wifi.txq_overmemory = std::stol(array[67]);
    wifi.txq_collisions = std::stol(array[68]);
    wifi.txq_tx_bytes = std::stol(array[69]);
    wifi.txq_tx_packets = std::stol(array[70]);
    wifi.cts_protection = std::stoi(array[71]);
    wifi.short_preamble = std::stoi(array[72]);
    wifi.short_slot_time = std::stoi(array[73]);
    wifi.dtim_period = std::stoi(array[74]);
    wifi.beacon_interval = std::stoi(array[75]);

    wifi.surv_frequency = std::stol(array[76]);
    wifi.surv_noise = std::stoi(array[77]);
    wifi.surv_in_use = std::stol(array[78]);
    wifi.surv_time = std::stol(array[79]);
    wifi.surv_time_busy = std::stol(array[80]);
    wifi.surv_time_ext_busy = std::stol(array[81]);
    wifi.surv_time_rx = std::stol(array[82]);
    wifi.surv_time_tx = std::stol(array[83]);
    wifi.surv_time_scan = std::stol(array[84]);
    wifi.surv_time_bss_rx = std::stol(array[85]);

    wifi.iface_wiphy = std::stoi(array[86]);
    wifi.iface_frequency = std::stoi(array[87]);
    wifi.iface_channel = std::stoi(array[88]);
    wifi.iface_channel_width = std::stoi(array[89]);
    wifi.iface_center_freq1 = std::stoi(array[90]);
    wifi.iface_center_freq2 = std::stoi(array[91]);
    wifi.iface_channel_type = std::stoi(array[92]);
    wifi.iface_tx_power = std::stoi(array[93]); //in mBm

    return wifi;
}
