/**
 *
 * Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
 * Distributed under the GNU GPL v2. For full terms see the file LICENSE.
 *
 * Data Receiver main file/function.
 */

#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <chrono>

#include "DataReceiver.hpp"
#include "FeedbackSender.hpp"

#define LOG_FNAME "/var/log/dreceiver.log"

void *array[2];

/**
 * Handle signals to terminate the program by stopping
 * all threads.
 */
void sigHandler(int) {  // what a killer method!
    ((DataReceiver*) array[0])->stopThread();
    ((FeedbackSender*) array[1])->stopThread();
}

/**
 * Just creates data receiver object and calls run method.
 */
int main(int argc, char *argv[]) {
    LOG_INIT(LOG_FNAME)

    DataReceiver dreceiver;
    dreceiver.readConfig(CONFIG_FNAME);

    FeedbackSender feedbackSender(&dreceiver);
    feedbackSender.readConfig(CONFIG_FNAME);

    array[0] = &dreceiver;
    array[1] = &feedbackSender;

    //This is needed because dreceiver and feedback sender both
    // initiate two children threads and would block the other from running.
    std::thread dreceiverThread(&DataReceiver::run, &dreceiver);
    std::thread feedbackSenderThread(&FeedbackSender::run, &feedbackSender);

    feedbackSenderThread.join();
    dreceiverThread.join();

    LOG_CLOSE()

    return 0;
}
