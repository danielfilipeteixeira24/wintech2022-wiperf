/**
 *
 * Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
 * Distributed under the GNU GPL v2. For full terms see the file LICENSE.
 *
 */

#include <thread>
#include <sys/mman.h>     // mmap()
#include <iostream>
#include <csignal>       // SIGTERM, etc

#include "../../util/logfile.hpp"
#include "../../util/configfile.hpp"
#include "ChannelMonitor.hpp"

#define CONFIG_FNAME "/etc/wiperf.conf"
#define LOG_FNAME "/var/log/dsender.log"

void *array[1];

/**
 * Handle signals to terminate the program by stopping
 * all threads.
 */
void sigHandler(int) {  // what a killer method!
    ((ChannelMonitor*) array[0])->stopThread();
}

/**
 * Just creates data sender object and calls run method.
 */
int main(int argc, char* argv[]) {
    LOG_INIT(LOG_FNAME)

    std::cout << "[INFO] Init" << std::endl;
    
    ChannelMonitor channelMonitor(CONFIG_FNAME);

    //To handle signals
    array[0] = &channelMonitor;

    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);
    signal(SIGHUP, sigHandler);

    std::cout << "[INFO] Set up the resources" << std::endl;
    
    std::thread channelMonitorThread(&ChannelMonitor::run, &channelMonitor);

    std::cout << "[INFO] Threads start running" << std::endl;

    channelMonitorThread.join();

    std::cout << "[INFO] Threads finish running" << std::endl;

    LOG_CLOSE()

    return 0;
}
