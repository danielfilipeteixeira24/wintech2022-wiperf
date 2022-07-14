/**
 *
 * Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
 * Distributed under the GNU GPL v2. For full terms see the file LICENSE.
 *
 * This program polls a serial GPS device, parses the NMEA sentences and puts the data it reads in a
 * shared memory region for other applications to read.
 *
 */

#define _XOPEN_SOURCE 700 /* required for strptime */

#include <stdio.h>
#include <cstdlib>
#include <cstring> // memset()
#include <unistd.h> // for the read syscall
#include <fcntl.h> // O_RDONLY, etc
#include <csignal> // SIGTERM, etc
#include <time.h>
#include <sys/time.h> // gettimeofday()
#include <sys/mman.h> // mmap()
#include <stdint.h> // uint*_t typedefs
#include <errno.h>
#include <inttypes.h> // PRIu64
#include <pthread.h> // pthread_mutex_t, pthread_cond_t

#include <fstream> // file read(), write()
#include <sstream> // std::stringstream
#include <string> // std::stoi

#include "gpsinfo.hpp" // GpsInfo struct
#include "../util/configfile.hpp" // class ConfigFile
#include "../util/logfile.hpp" // class LogFile and LOG_* macros

#define LOG_FNAME "/var/log/mygpsd.log"
#define CONFIG_FNAME "/etc/wiperf.conf"

#define SERIAL_DEVICE_DEF "/dev/ttyACM0"
#define SHM_PATH_DEF "/wiperf-gpsinfo"

#define NMEA_MAX_BUFLEN  100
#define NMEA_MAX_WORDS  30
#define NMEA_WORD_SIZE  30

// relevant NMEA sentences
enum NmeaType {
    Rmc, Gga, Gsa, Gsv, Vtg, Other
};
typedef enum NmeaType NmeaType;


// values to be read from config file
struct Config {
    LogLevel logLevel;
    std::string serialDevice;
    std::string shmPath;
};
typedef struct Config Config;

bool endProgram_ = false; // global needed to end things cleanly on exit

void sigHandler(int) {
    LOG_MSG("mygpsd killed");
    endProgram_ = true;
}

/* aux functions start (first to obviate prototypes) */

/**
 * Load configuration from file.
 */
void readConfig(const char *fname, Config &config) {

    // check to see if configuration file exists
    std::ifstream cfgFile(fname);
    if (not cfgFile.good()) {
        std::stringstream ss;
        ss << "Could not open config file \"" << fname <<
           "\" will use defaults for everything";
        LOG_ERR(ss.str().c_str());
    }

    ConfigFile cfile(fname);

    // log level
    config.logLevel = LOG_LEVEL_DEF;
    try {
        const int level = std::stoi(cfile.Value("mygpsd", "log-level"));
        if (level >= 0 && level < NLOG_LEVELS) /* invalid */
            config.logLevel = (LogLevel) level;
        else {
            std::stringstream ss;
            ss << "Config exception: section=mygpsd, value=log-level, invalid value "
               << level << ". Acceptable range is (0, " << NLOG_LEVELS
               << ". Reverting to default: " << config.logLevel;
            LOG_ERR(ss.str().c_str());
        }
    } catch (char const *str) {
        std::stringstream ss;
        ss << "Config exception: section=mygpsd, value=log-level " << str
           << " using default value " << config.logLevel;
        LOG_ERR(ss.str().c_str());
    }

    // done here so all messages appear, and in the correct order
    LOG_LEVEL_SET(config.logLevel);
    LOG_MSG("Starting mygpsd...");

    // gps shared memory path
    try {
        config.shmPath = cfile.Value("gpsinfo", "shm-path");
    } catch (char const *str) {
        config.shmPath = std::string(SHM_PATH_DEF);
        std::stringstream ss;
        ss << "Config exception: section=gpsinfo, value=shm-path " << str
           << " using default value " << config.shmPath;
        LOG_ERR(ss.str().c_str());
    }

    // serial device file
    try {
        config.serialDevice = cfile.Value("mygpsd", "serial-device");
    } catch (char const *str) {
        config.serialDevice = std::string(SERIAL_DEVICE_DEF);
        std::stringstream ss;
        ss << "Config exception: section=mygpsd, value=serial-device " << str
           << " using default value " << config.serialDevice;
        LOG_ERR(ss.str().c_str());
    }

}

/**
 * Print gps info from the argument passed as a reference.
 */
void printGpsInfo(GpsInfo *gpsinfo) {

    char logbuf[512];
    sprintf(logbuf,
            "GPS systime=%" PRIu64 ", gpstime=%d, pos=(%f,%f,%f), heading %f deg at %f Km/h, hdop=%f, nsats=%d, fix=%d",
            gpsinfo->systime, gpsinfo->gpstime, gpsinfo->lat, gpsinfo->lon,
            gpsinfo->alt, gpsinfo->head, gpsinfo->speed, gpsinfo->hdop,
            gpsinfo->nsats, gpsinfo->fix);
    LOG_VERBOSE(logbuf);
}

/**
 * Read a NMEA sentence into a string array that is easy to process.
 *
 * Returns 0 on success, other on Checksum fail.
 */
int readNmea(const int fd, NmeaType *nmeaType,
             char datarray[][NMEA_WORD_SIZE]) {

    unsigned int valid = 0;
    int ret = 0, nRead = 0;
    char readbuf[NMEA_MAX_BUFLEN];
    char *ptr = readbuf;
    char ch;

    /* squeaky clean */
    memset(datarray, '\0', NMEA_MAX_WORDS * NMEA_WORD_SIZE);
    *nmeaType = Other;

    while (1) { // hopefully not an infinite loop

        ret = read(fd, &ch, 1); // read something

        if (ret == -1) {
            LOG_FATAL_PERROR_EXIT("main loop (reading from serial port)");
        } else if (ret == 0) { // EOF
            if (nRead == 0) return 1; // no bytes read; return ERROR
            else break; // some bytes read; go on
        } else { // 'ret' must == 1 if we get here
            if (ch == '\n') break;
            if (ch == '\r') continue;

            if (nRead < NMEA_MAX_BUFLEN - 2) { // discard > (n - 1) bytes
                nRead++;
                *ptr++ = ch;
            } else break;
        }
    }

    // process what we've read
    readbuf[nRead] = '\0';

    // figure out nmea message type by looking at 1st few chars
    // GP -> GPS and GN -> GLONASS
    if (!strncmp(readbuf, "$GPRMC", 6) || !strncmp(readbuf, "$GNRMC", 6)) {
        *nmeaType = Rmc;
    }
    else if (!strncmp(readbuf, "$GPGGA", 6) || !strncmp(readbuf, "$GNGGA", 6)) {
        *nmeaType = Gga;
    }
    else if (!strncmp(readbuf, "$GPGSA", 6) || !strncmp(readbuf, "$GNGSA", 6)) {
        *nmeaType = Gsa;
    }
    else if (!strncmp(readbuf, "$GPVTG", 6) || !strncmp(readbuf, "$GNVTG", 6)) {
        *nmeaType = Vtg;
    }
    else {
        *nmeaType = Other;
    }

    if (*nmeaType != Other) {
        char *bufptr = readbuf;
        int nwords = 0, i = 0, ret = 0;

        // %n gives us the index where the match ended
        while (nwords < NMEA_MAX_WORDS
               && (ret = sscanf(bufptr, "%[^,*]%n", datarray[nwords], &i)) != EOF) {
            if (ret == 0) { // found a ',' or '*'
                i = 0;
                datarray[nwords][0] = '\0'; // overkill because squeaky clean
            }

            nwords++;
            bufptr += i;

            /* should run everytime except for the last word (checksum).
              just to avoid consuming the terminating char */
            if (*bufptr == ',' || *bufptr == '*') bufptr++;
        }

        // checksum validation
        sscanf(datarray[nwords - 1], "%X", &valid);
        bufptr = readbuf + 1; // '$' shouldn't be used in checksum
        while (*bufptr != '*' && *bufptr != '\0') valid ^= *bufptr++;
    }

    return valid;
}

/* aux functions end */

/**
 * A thread that processes NMEA info as it comes in and updates GpsInfo shared memory accordingly.
 */
void *nmeaProcThread(void *arg) {

    Config *config = (Config *) arg;

    GpsInfo gpsinfo;
    memset(&gpsinfo, 0, sizeof(GpsInfo)); // start out with zero

    // open the serial port for reading
    int serialFd = 0;
    if ((serialFd = open(config->serialDevice.c_str(), O_RDONLY | O_NOCTTY)) < 0)
        LOG_FATAL_PERROR_EXIT("nmeaProcThread open() serial device");

    // create the shared memory region
    int shmfd = 0;
    if ((shmfd = shm_open(config->shmPath.c_str(),
                          O_CREAT | O_RDWR, S_IRWXU | S_IRWXG))
        < 0)
        LOG_FATAL_PERROR_EXIT("nmeaProcThread shm_open()");

    // adjust shared memory segment to desired size
    int shmobjSize = (1 * sizeof(GpsInfo));
    ftruncate(shmfd, shmobjSize);

    // request the shared segment -- mmap()
    GpsInfo *gpsInfoShm;
    if ((gpsInfoShm = (GpsInfo *) mmap(NULL, shmobjSize,
                                       PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0)) == NULL)
        LOG_FATAL_PERROR_EXIT("nmeaProcThread nmap()");

    // create a mutex to protect the shared region and be used with cond variable
    pthread_mutexattr_t mattr;
    if (pthread_mutexattr_init(&mattr))
        LOG_FATAL_PERROR_EXIT("nmeaProcThread pthread_mutexattr_init()");

    if (pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED))
        LOG_FATAL_PERROR_EXIT("nmeaProcThread pthread_condattr_setpshared()");

    if (pthread_mutex_init(&gpsInfoShm->mutex, &mattr))
        LOG_FATAL_PERROR_EXIT("nmeaProcThread pthread_mutex_init()");

    // initialize info update condition variable
    pthread_condattr_t cattr;
    if (pthread_condattr_init(&cattr))
        LOG_FATAL_PERROR_EXIT("nmeaProcThread pthread_condattr_init()");

    if (pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED))
        LOG_FATAL_PERROR_EXIT("nmeaProcThread pthread_condattr_setpshared()");

    if (pthread_cond_init(&gpsInfoShm->updateCond, &cattr))
        LOG_FATAL_PERROR_EXIT("nmeaProcThread pthread_cond_init()");

    // install the signal handler
    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);
    signal(SIGHUP, sigHandler);

    gpsInfoShm->daemonOn = true; // meaning we're up and running

    LOG_MSG("mygpsd up and running");

    // loop variables
    bool wrmc; // waiting for rmc message flag
    char timestr[20];
    struct tm tm;
    struct timeval systime;
    char datarray[NMEA_MAX_WORDS][NMEA_WORD_SIZE];
    NmeaType nmeaType = Other;

    while (!endProgram_) { // main loop
        memset(&gpsinfo, 0, sizeof(GpsInfo)); // zero to start with

        /* we assume the RMC tag is always the last one (from experiments and
           online doc, this is usually the case) */
        wrmc = true; // yes, we're waiting for an rmc message
        while (wrmc) {
            if (readNmea(serialFd, &nmeaType, datarray)) { // checksum failed
                LOG_WARN("read nmea checksum failed");
                continue;
            }

            switch (nmeaType) {

                case Rmc:
                    /* rmc has time and date:
                       time is in position 1 and date in position 9 */
                    snprintf(timestr, 20, "20%c%c-%c%c-%c%c %c%c:%c%c:%c%c",
                             datarray[9][4], datarray[9][5], datarray[9][2],
                             datarray[9][3], datarray[9][0], datarray[9][1],
                             datarray[1][0], datarray[1][1], datarray[1][2],
                             datarray[1][3], datarray[1][4], datarray[1][5]);

                    if (strptime(timestr, "%Y-%m-%d %H:%M:%S", &tm) != 0)
                        gpsinfo.gpstime = mktime(&tm);
                    else gpsinfo.gpstime = 0;

                    // compute systime in millis
                    gettimeofday(&systime, NULL);
                    gpsinfo.systime = ((uint64_t) systime.tv_sec * 1000) +
                                      ((uint64_t) systime.tv_usec / 1000);

                    // A stands for Active and V stands for Void
                    if (datarray[2][0] == 'A') {

                        // handle latitude
                        if (datarray[3][0] != '\0' && datarray[4][0] != '\0') {
                            float lat1 = 0.0, lat2 = 0.0;
                            sscanf(datarray[3], "%2f%f", &lat1, &lat2);

                            if (strcmp(datarray[4], "N") == 0)
                                gpsinfo.lat = (lat1 + lat2 / 60.0);
                            else if (strcmp(datarray[4], "S") == 0)
                                gpsinfo.lat = -(lat1 + lat2 / 60.0);
                        }

                        // handle longitude
                        if (datarray[5][0] != '\0' && datarray[6][0] != '\0') {
                            float lon1 = 0.0, lon2 = 0.0;
                            sscanf(datarray[5], "%3f%f", &lon1, &lon2);

                            if (strcmp(datarray[6], "E") == 0)
                                gpsinfo.lon = (lon1 + lon2 / 60.0);
                            else if (strcmp(datarray[6], "W") == 0)
                                gpsinfo.lon = -(lon1 + lon2 / 60.0);
                        }

                        if (datarray[7][0] != '\0') // 1.852 factor -> knot to km/h
                            gpsinfo.speed = strtof(datarray[7], NULL) * 1.852;

                        if (datarray[8][0] != '\0')
                            gpsinfo.head = strtof(datarray[8], NULL);
                    }

                    wrmc = false;
                    break;

                case Gga:
                    // handle latitude
                    if (datarray[2][0] != '\0' && datarray[3][0] != '\0') {
                        float lat1 = 0.0, lat2 = 0.0;
                        sscanf(datarray[2], "%2f%f", &lat1, &lat2);

                        if (strcmp(datarray[3], "N") == 0)
                            gpsinfo.lat = (lat1 + lat2 / 60.0);
                        else if (strcmp(datarray[3], "S") == 0)
                            gpsinfo.lat = -(lat1 + lat2 / 60.0);
                    }

                    // handle longitude
                    if (datarray[4][0] != '\0' && datarray[5][0] != '\0') {
                        float lon1 = 0.0, lon2 = 0.0;
                        sscanf(datarray[4], "%3f%f", &lon1, &lon2);

                        if (strcmp(datarray[5], "E") == 0)
                            gpsinfo.lon = (lon1 + lon2 / 60.0);
                        else if (strcmp(datarray[5], "W") == 0)
                            gpsinfo.lon = -(lon1 + lon2 / 60.0);
                    }

                    if (datarray[6][0] != '\0')
                        gpsinfo.qual = (uint8_t) atoi(datarray[6]);

                    if (datarray[7][0] != '\0')
                        gpsinfo.nsats = (uint8_t) atoi(datarray[7]);

                    if (datarray[8][0] != '\0')
                        gpsinfo.hdop = strtof(datarray[8], NULL);

                    if (datarray[9][0] != '\0')
                        gpsinfo.alt = strtof(datarray[9], NULL);

                    break;

                case Vtg:
                    if (datarray[2][0] != '\0')
                        gpsinfo.head = strtof(datarray[2], NULL);

                    if (datarray[4][0] != '\0')
                        gpsinfo.head_mag = strtof(datarray[4], NULL);

                    if (datarray[6][0] != '\0') // 1.852 factor -> knot to km/h
                        gpsinfo.speed = strtof(datarray[6], NULL) * 1.852;

                    break;

                case Gsa:
                    if (datarray[2][0] != '\0')
                        gpsinfo.fix = (uint8_t) atoi(datarray[2]); // 1=nofix, 2=2D, 3=3D

                    if (datarray[16][0] != '\0')
                        gpsinfo.pdop = strtof(datarray[16], NULL);

                    if (datarray[17][0] != '\0')
                        gpsinfo.hdop = strtof(datarray[17], NULL);

                    if (datarray[18][0] != '\0')
                        gpsinfo.vdop = strtof(datarray[18], NULL);

                    break;

                default:
                    break;
            } // switch (nmeaType) end
        } // while(wrmc) end

        // write new info to shared memory
        if (pthread_mutex_lock(&gpsInfoShm->mutex))
            LOG_FATAL_PERROR_EXIT("nmeaProcThread pthread_mutex_lock()");

        gpsInfoShm->systime = gpsinfo.systime;
        gpsInfoShm->gpstime = gpsinfo.gpstime;

        gpsInfoShm->fix = gpsinfo.fix;
        gpsInfoShm->nsats = gpsinfo.nsats;
        gpsInfoShm->hdop = gpsinfo.hdop;
        gpsInfoShm->vdop = gpsinfo.vdop;
        gpsInfoShm->pdop = gpsinfo.pdop;
        gpsInfoShm->qual = gpsinfo.qual;

        gpsInfoShm->lat = gpsinfo.lat;
        gpsInfoShm->lon = gpsinfo.lon;
        gpsInfoShm->alt = gpsinfo.alt;
        gpsInfoShm->speed = gpsinfo.speed;
        gpsInfoShm->head = gpsinfo.head;
        gpsInfoShm->head_mag = gpsinfo.head_mag;

        // let all observers know there's new gps info to be read
        if (pthread_cond_broadcast(&gpsInfoShm->updateCond))
            LOG_FATAL_PERROR_EXIT("nmeaProcThread pthread_cond_broadcast()");

        if (pthread_mutex_unlock(&gpsInfoShm->mutex))
            LOG_FATAL_PERROR_EXIT("nmeaProcThread pthread_mutex_unlock()");

        printGpsInfo(&gpsinfo);

    } // main loop end

    // we're done here, just tidy everything up before leaving
    close(serialFd); // no more reading from gps device

    // write new info to shared memory
    if (pthread_mutex_lock(&gpsInfoShm->mutex))
        LOG_FATAL_PERROR_EXIT("nmeaProcThread pthread_mutex_lock()");

    gpsInfoShm->daemonOn = false;

    if (pthread_cond_broadcast(&gpsInfoShm->updateCond))
        LOG_FATAL_PERROR_EXIT("nmeaProcThread pthread_cond_broadcast()");

    if (pthread_mutex_unlock(&gpsInfoShm->mutex))
        LOG_FATAL_PERROR_EXIT("nmeaProcThread pthread_mutex_unlock()");

    if (pthread_cond_destroy(&gpsInfoShm->updateCond))
        LOG_FATAL_PERROR_EXIT("nmeaProcThread pthread_cond_destroy)");

    // bye bye gps shared memory region
    if (shm_unlink(config->shmPath.c_str()) != 0)
        LOG_FATAL_PERROR_EXIT("nmeaProcThread shm_unlink()");

    return 0;
}

/**
 * The main procedure constantly reads new gps information and
 * stores it in a shared memory region for applications to read.
 */
int main(int argc, char *argv[]) {

    /* make sure we have the correct timezone set */
    char timezone[] = "TZ=Europe/Lisbon";
    putenv(timezone);
    tzset();

    LOG_INIT(LOG_FNAME);

    /* load them settings */
    Config config;
    readConfig(CONFIG_FNAME, config);

    /* launch nmea processor thread */
    pthread_t ptid;
    if (pthread_create(&ptid, NULL, nmeaProcThread, (void *) &config))
        LOG_FATAL_PERROR_EXIT("main pthread_create() nmeaProcThread");

    // calling join prevents the main thread from ending
    // and killing the sender thread along with it
    if (pthread_join(ptid, NULL))
        LOG_FATAL_PERROR_EXIT("main pthread_join() nmeaProcThread");

    LOG_CLOSE();

    return 0;
}
