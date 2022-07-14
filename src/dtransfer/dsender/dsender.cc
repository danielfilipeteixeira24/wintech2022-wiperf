/**
 *
 * Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
 * Distributed under the GNU GPL v2. For full terms see the file LICENSE.
 *
 * This program creates a UDP server and listens to incoming traffic, printing the amount of data being
 * received per unit of time, i.e., throughput.
 */

#include <thread>
#include <sys/mman.h>     // mmap()
#include <iostream>
#include <csignal>       // SIGTERM, etc

#include "DataSender.hpp"
#include "FeedbackReceiver.hpp"
#include "../../util/logfile.hpp"

#define LOG_FNAME "/var/log/dsender.log"

void *array[2];

/**
 * Handle signals to terminate the program by stopping
 * all threads.
 */
void sigHandler(int) {  // what a killer method!
    ((DataSender*) array[0])->stopThread();
    ((FeedbackReceiver*) array[1])->stopThread();
}

/**
 * Just creates data sender object and calls run method.
 */
int main(int argc, char* argv[]) {
    LOG_INIT(LOG_FNAME)

    DataSender dsender;
    dsender.readConfig(CONFIG_FNAME);

    FeedbackReceiver freceiver;
    freceiver.readConfig(CONFIG_FNAME);

    //To handle signals
    array[0] = &dsender;
    array[1] = &freceiver;

    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);
    signal(SIGHUP, sigHandler);

    //This is needed because dsender and feedback receiver both
    // initiate two children threads and would block the other from running.
    std::thread dsenderThread(&DataSender::run, &dsender);
    std::thread freceiverThread(&FeedbackReceiver::run, &freceiver);

    std::cout << "Threads start running" << std::endl;

    freceiverThread.join();
    dsenderThread.join();

    std::cout << "Threads finish running" << std::endl;

    LOG_CLOSE()

    return 0;
}
