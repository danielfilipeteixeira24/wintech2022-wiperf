/**
 *
 * Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
 * Distributed under the GNU GPL v2. For full terms see the file LICENSE.
 *
 * This program reads the current GPS information stored in the shared memory segment and
 * prints it on the CLI. It can be used as a debugging tool or to simply store the GPS
 * data in a log file.
 */

#include <sys/mman.h> // shm_open, etc
#include <fcntl.h>    // S_RDWR, etc
#include <iostream>   // std::cout
#include <csignal>    // SIGTERM, etc
#include <cstring>    // strerror()
#include <fstream>    // std::ifstream
#include <sstream>    // std::stringstream
#include <string>     // std::string, std::stoi
#include <iostream>   // std::cout
#include <pthread.h>  // pthread_mutex_lock, etc

#include "../util/configfile.hpp" // class ConfigFile
#include "../util/logfile.hpp"    // class LogFile and LOG_* macros
#include "../mygpsd/gpsinfo.hpp"  // struct GpsInfo

#define LOG_FNAME "/var/log/gpsprinter.log"
#define CONFIG_FNAME "/etc/wiperf.conf"

#define GPS_SHM_PATH_DEF "/wiperf-gpsinfo"

struct Config {
    LogLevel logLevel;
    std::string gpsShmPath;
    unsigned long long nprints;
};
typedef struct Config Config;

bool endProgram_ = false; // global needed to end things cleanly on exit

void sigHandler(int) {
    LOG_MSG("gpsprinter killed");
    endProgram_ = true;
}

void readConfig(const char *fname, Config &config) {

    // check to see if the configuration file exists
    std::ifstream cfgFile(fname);
    if (not cfgFile.good()) {
        std::cerr << "Could not open config file \"" << fname <<
                  "\" will use defaults for everything" << std::endl;
    }

    ConfigFile cfile(fname);

    // log level
    config.logLevel = LOG_LEVEL_DEF;
    try {
        const int level = std::stoi(cfile.Value("gps-printer", "log-level"));
        if (level >= 0 && level < NLOG_LEVELS) // invalid
            config.logLevel = (LogLevel) level;
        else {
            std::stringstream ss;
            ss << "Config exception: section=gps-printer, value=log-level, invalid value "
               << level << ". Acceptable range is (0, " << NLOG_LEVELS
               << ". Reverting to default: " << config.logLevel;
            LOG_ERR(ss.str().c_str());
        }
    } catch (char const *str) {
        std::stringstream ss;
        ss << "Config exception: section=gps-printer, value=log-level " << str
           << " using default value " << config.logLevel;
        LOG_ERR(ss.str().c_str());
    }

    // done here so all messages appear, and in the correct order
    LOG_LEVEL_SET(config.logLevel);
    LOG_MSG("Starting gpsprinter...");

    // gps shared memory path
    try {
        config.gpsShmPath = cfile.Value("gpsinfo", "shm-path");
    } catch (char const *str) {
        config.gpsShmPath = std::string(GPS_SHM_PATH_DEF);
        std::stringstream ss;
        ss << "Config exception: section=gpsinfo, value=shm-path " << str
           << " using default value " << config.gpsShmPath;
        LOG_ERR(ss.str().c_str());
    }

}

void *printerThread(void *arg) {
    Config *config = (Config *) arg;

    // open the gps shared memory
    int gpsShmfd = 0;
    if ((gpsShmfd =
                 shm_open(config->gpsShmPath.c_str(), O_RDWR, S_IRUSR | S_IRGRP)) < 0)
        LOG_FATAL_PERROR_EXIT("gpsInfo shm_open()");

    // request the gps shared segment
    GpsInfo *gpsInfoShm = 0;
    if ((gpsInfoShm = (GpsInfo *) mmap(NULL, sizeof(GpsInfo),
                                       PROT_READ | PROT_WRITE, MAP_SHARED,
                                       gpsShmfd, 0)) == MAP_FAILED)
        LOG_FATAL_PERROR_EXIT("gpsInfo nmap()");

    LOG_MSG("gpsprinter up and running");

    // print header line with format
    std::cout << "gpstime, systime, lat, lon, alt, speed, head, head_mag, fix, nsats, qual, hdop, vdop, pdop"
              << std::endl;

    // main loop
    unsigned long long niters = 0;
    while (!endProgram_) {

        // check limit. recall we want nprints == -1 to mean limitless printing
        if (niters == config->nprints) break; // stop if limit hit, but not if over
        else niters++;

        if (pthread_mutex_lock(&gpsInfoShm->mutex)) // try to access region
            LOG_FATAL_PERROR_EXIT("printerThread pthread_mutex_lock()");

        /* note on behavior: call to cond_wait unlocks mutex then blocks
           on condition. when condition is signaled, mutex is auto-locked. */
        if (pthread_cond_wait(&gpsInfoShm->updateCond, &gpsInfoShm->mutex))
            LOG_FATAL_PERROR_EXIT("printerThread pthread_cond_wait()");

        if (gpsInfoShm->daemonOn) { // daemon is live, print the info
            std::cout << gpsInfoShm->gpstime << ", " <<
                      gpsInfoShm->systime << ", " <<
                      gpsInfoShm->lat << ", " <<
                      gpsInfoShm->lon << ", " <<
                      gpsInfoShm->alt << ", " <<
                      gpsInfoShm->speed << ", " <<
                      gpsInfoShm->head << ", " <<
                      gpsInfoShm->head_mag << ", " <<
                      (unsigned) gpsInfoShm->fix << ", " <<
                      (unsigned) gpsInfoShm->nsats << ", " <<
                      (unsigned) gpsInfoShm->qual << ", " <<
                      gpsInfoShm->hdop << ", " <<
                      gpsInfoShm->vdop << ", " <<
                      gpsInfoShm->pdop << std::endl;
        } else endProgram_ = true; // daemon is gone, we'd better leave too

        if (pthread_mutex_unlock(&gpsInfoShm->mutex)) // let others go
            LOG_FATAL_PERROR_EXIT("printerThread pthread_mutex_unlock()");
    }

    // no cleanup needed

    return 0;
}

int main(int argc, char *argv[]) {

    /* make sure we have the correct timezone set */
    char timezone[] = "TZ=Europe/Lisbon";
    putenv(timezone);
    tzset();

    LOG_INIT(LOG_FNAME);

    Config config;
    readConfig(CONFIG_FNAME, config);

    /* we support the number of desired print operations to be passed as
     an unsigned int. It is the only supported program argument. */
    config.nprints = -1; // default, leads to infinite printing :)
    if (argc >= 2) {
        std::string arg = argv[1];
        try {
            std::size_t pos;
            unsigned long long nprints = std::stoull(arg, &pos);
            if (pos < arg.size())
                std::cerr << "Ignoring trailing characters after #iters: " << arg <<
                          std::endl;
            config.nprints = nprints;
        } catch (std::invalid_argument const &ex) {
            std::cerr << "Invalid #iters: " << arg << ". Running limitless." <<
                      std::endl;
        } catch (std::out_of_range const &ex) {
            std::cerr << "#iters out of range: " << arg << ". Running limitless." <<
                      std::endl;
        }
    }

    // launch printer thread
    pthread_t ptid;
    if (pthread_create(&ptid, NULL, printerThread, (void *) &config))
        LOG_FATAL_PERROR_EXIT("main pthread_create() printerThread");

    // install the signal handler
    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);
    signal(SIGHUP, sigHandler);

    // calling join prevents the main thread from ending
    // and killing the sender thread along with it
    if (pthread_join(ptid, NULL))
        LOG_FATAL_PERROR_EXIT("main pthread_join() printerThread");

    LOG_CLOSE();

    return 0;
}
