/**
 *
 * Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
 * Distributed under the GNU GPL v2. For full terms see the file LICENSE.
 *
 */

#include "DataTransfer.hpp"  // class DataTransfer

#include <arpa/inet.h>    // inet_pton
#include <pthread.h>      // pthread_mutex_init
#include <sys/eventfd.h>  // eventfd()
#include <unistd.h>       // close()

#include <csignal>       // SIGTERM, etc
#include <cstdlib>       // putenv
#include <cstring>       // memset()
#include <fstream>       // std::ifstream
#include <sstream>       // std::stringstream
#include <string>        // std::string
#include <system_error>  // std::system_error
#include <thread>        // std::thread
#include <utility>

void DataTransfer::stopThread() {  // what a killer method!
    LOG_MSG("program killed");
    endProgram_ = true;
    eventfd_write(wakefd_, 1);  // wakes up select
}

DataTransfer::DataTransfer(std::string printTag) : printTag(std::move(printTag)),
                                                   ifaceMap() {
    endProgram_ = false;
    wakefd_ = 0;

    // create a mutex to protect the access to ifaceInfoMap
    pthread_mutexattr_t mattr;
    if (pthread_mutexattr_init(&mattr))
        LOG_FATAL_PERROR_EXIT("DataTransfer() pthread_mutexattr_init()");

    if (pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED))
        LOG_FATAL_PERROR_EXIT("DataTransfer() pthread_condattr_setpshared()");

    if (pthread_mutex_init(&this->ifaceMapMutex, &mattr))
        LOG_FATAL_PERROR_EXIT("DataTransfer() pthread_mutex_init()");
}

std::string DataTransfer::getGpsShmPath() {
    return this->gpsShmPath;
}

IfaceInfoMap DataTransfer::getIfaceInfoMap() {
    return this->ifaceMap;
}

pthread_mutex_t DataTransfer::getIfaceInfoMutex() {
    return this->ifaceMapMutex;
}

void DataTransfer::createSockaddr(const std::string& addrStr, uint16_t port,
                                  struct sockaddr_in* sockaddr) {
    // prep address structure
    memset(sockaddr, '\0', sizeof(struct sockaddr_in));  // zero out struct
    sockaddr->sin_family = AF_INET;                      // Internet address family
    sockaddr->sin_port = htons(port);                    // port

    // try to convert address string to actual ip address
    int rv = inet_pton(AF_INET, addrStr.c_str(), &(sockaddr->sin_addr));

    if (rv != 1) {
        this->closeIfaceSocks();
        std::stringstream ss;
        ss << "rthread inet_pton() addr " << addrStr;
        std::string str(ss.str());

        if (rv == -1) {
            LOG_FATAL_PERROR_EXIT(str.c_str());
        }  // other error
        else {
            LOG_FATAL_EXIT(str.c_str());
        }  // invalid address
    }
}

/**
 * Close all of the interface sockets.
 */
void DataTransfer::closeIfaceSocks() {
    // iterate over map and call FD_SET on each socket fd
    for (auto & itr : this->ifaceMap) {
        int sockfd = (itr.second).sockfd;
        if (sockfd != UNINITIALIZED_FD) {
            close(sockfd);
        }
    }
}

/**
 * Prints amount of data sent or received on each interface during each individual second.
 * Printing is triggered by a notification from mygpsd, telling us that gps information has changed.
 */
void DataTransfer::printerThread() {
    GpsInfo* gpsInfoShm = WiperfUtility::getGpsInfo(this->gpsShmPath);

    // print header
    std::cout << "gpstime, ifaceName, nbytes" << this->printTag << std::endl;

    // loop variables
    uint64_t gpstime = 0, gpstimeOld = 0;
    while (!endProgram_) {  // for ever, and ever, and ever

        //if (pthread_mutex_lock(&gpsInfoShm->mutex))  // gain acess to gps info shm
        //    LOG_FATAL_PERROR_EXIT("pthread pthread_mutex_lock()");

        // note on behavior: call to cond_wait unlocks mutex then blocks
        //   on condition. when condition is signaled, mutex is auto-locked.
        //if (pthread_cond_wait(&gpsInfoShm->updateCond, &gpsInfoShm->mutex))
        //    LOG_FATAL_PERROR_EXIT("pthread pthread_cond_wait()");

        // read new gpstime
        gpstimeOld = gpstime;
        gpstime = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count(); //gpsInfoShm->gpstime;

        //if (pthread_mutex_unlock(&gpsInfoShm->mutex))  // release gps info shm
        //    LOG_FATAL_PERROR_EXIT("pthread pthread_mutex_unlock()");

        if (gpstimeOld == 0) continue;  // no point in continuing

        // print iface stats
        if (pthread_mutex_lock(&this->ifaceMapMutex))  // iface map exclusive access
            LOG_FATAL_PERROR_EXIT("pthread pthread_mutex_lock()");

        // iterate over interface map and print stats for each
        for (auto & itr : this->ifaceMap) {
            const std::string ifaceName = itr.first;
            const uint64_t ifaceNbytes = (itr.second).nbytesAcc;

            std::cout << gpstimeOld << ", " << ifaceName << ", " << ifaceNbytes << std::endl;

            (itr.second).nbytesAcc = 0;  // reset byte count for next round
        }

        if (pthread_mutex_unlock(&this->ifaceMapMutex))  // release iface map
            LOG_FATAL_PERROR_EXIT("pthread pthread_mutex_unlock()");

    }  // while(!endProgram_) end

    // nothing to clean up, just exit
}

/**
 * Launches the threads that do actual work.
 */
void DataTransfer::run() {
    // make sure we have the correct timezone set
    char timezone_[] = "TZ=Europe/Lisbon";
    putenv(timezone_);
    tzset();

    // create fd to wake up thread blocked on select when it's time to end
    if ((this->wakefd_ = eventfd(0, 0)) == -1) {
        LOG_FATAL_PERROR_EXIT("DataTransfer::run() eventfd()");
    }

    // install the signal handler
    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);
    signal(SIGHUP, sigHandler);

    // launch worker threads
    try {
        // launch
        // std::thread printerThread(&DataTransfer::printerThread, this);
        std::thread commThread(&DataTransfer::commThread, this);

        // wait for them to finish
        // printerThread.join();
        commThread.join();
    } catch (const std::system_error& e) {
        std::stringstream ss;
        ss << "DataTransfer::run() thread exception: " << e.what();
        LOG_FATAL_PERROR_EXIT(ss.str().c_str());
    }
}
