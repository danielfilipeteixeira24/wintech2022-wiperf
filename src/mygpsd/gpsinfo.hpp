/**
 *
 * Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
 * Distributed under the GNU GPL v2. For full terms see the file LICENSE.
 *
 * This program polls a serial GPS device, parses the NMEA sentences and puts the data it reads in a
 * shared memory region for other applications to read.
 *
 * Definition of the GpsInfo structure. The GPS information is kept in a shared memory and is updated
 * by our custom GPS daemon (mygpsd). The shared memory segment is accessed by the other modules of WiPerf.
 */

#ifndef GPS_INFO_H__
#define GPS_INFO_H__

#include <stdint.h> // uint*_t typedefs
#include <pthread.h> // pthread_cond_t

// store gps information
struct GpsInfo {
    uint64_t systime; // in millis
    uint32_t gpstime; // in seconds

    uint8_t fix;   // 1=nofix, 2=2D fix, 3=3D fix
    uint8_t nsats; // number of visible satellites
    float hdop;    // horizontal dilution of precision (lower is better)
    float vdop;    // vertical dilution of precision
    float pdop;    // positional dilution of precision (combines hdop and vdop)

    uint8_t qual;  // 0=nofix, 1=fix valid, 4=RTK fixed ambiguities, 5=RTK float ambiguities

    float lat; // decimal degrees
    float lon; // decimal degrees

    float alt;   // meters
    float speed; // hm/h
    float head;  // degrees from true north
    float head_mag; // degrees from magnetic north

    // extra info for inter-process communication
    bool daemonOn;
    pthread_mutex_t mutex; // exclusive access
    pthread_cond_t updateCond; // supports update notification
};

typedef struct GpsInfo GpsInfo;

#endif
