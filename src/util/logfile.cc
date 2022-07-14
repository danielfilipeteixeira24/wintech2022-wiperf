/**
 *
 * Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
 * Distributed under the GNU GPL v2. For full terms see the file LICENSE.
 *
 * Implementation of Log File utility class, used to log program errors.
 *
 */

#include "logfile.hpp"

#include <cstring> // std::strerror
#include <cerrno> // errno
#include <sstream> // stringstream
#include <sys/stat.h> // stat()


LogFile* LogFile::single_ = nullptr; // initialize the singleton

/**
 * A trivial  but necessary constructor.
 */
LogFile::LogFile() : logfp_(nullptr), level_(LOG_LEVEL_DEF) { }


LogFile* LogFile::getInstance(){
    if (LogFile::single_ == nullptr) LogFile::single_ = new LogFile();
    return LogFile::single_;
}

/**
 * Initialize log variant 1.
 * Uses default max log length of (1 MiB).
 */
void LogFile::initLog(const char* fname){
    this->initLog(fname, LOG_LEN_MAX_DEF);
}

/**
 * Constructor variant 2.
 * Uses caller-specified max log length.
 */
void LogFile::initLog(const char* fname, std::size_t maxlen){

    if (fname != nullptr) {
        /* check the size */
        struct stat fstat;
        stat(fname, &fstat);

        /* reset log if it's larger than limit */
        if ((size_t)fstat.st_size > maxlen)
            this->logfp_ = fopen(fname, "w");
        else this->logfp_ = fopen(fname, "a+");

        if (this->logfp_ != nullptr)
            fseek(this->logfp_, 0, SEEK_END); /* scan to the end */
    }
}

void LogFile::setLevel(const LogLevel level){
    this->level_ = level;
}

void LogFile::writeLog(LogLevel level, const char* pMsg, const char* pFname,
        int lineNo){

    if (this->logfp_ != nullptr && level <= this->level_) {
        static const char* levelNames[] = {"fatal", "error", "warn", "msg",
            "verbose"};
        const char*  pType = levelNames[level];

        time_t rawtime;
        struct tm *ptm;

        time(&rawtime);
        ptm = gmtime(&rawtime);

        fprintf(logfp_, "%s\t%4d-%02d-%02d\t%02d:%02d:%02d\t%s\t%d\t%s\n", pType,
                1900 + ptm->tm_year, 1 + ptm->tm_mon, ptm->tm_mday,
                ptm->tm_hour, ptm->tm_min, ptm->tm_sec, pFname, lineNo,
                pMsg);
        fflush(logfp_);
    }
}

void LogFile::writeLogPerror(LogLevel level, const char* pMsg,
        const char* pFname, int lineNo){
    std::stringstream ss;
    ss << pMsg << ": " << std::strerror(errno);
    this->writeLog(level, ss.str().c_str(), pFname, lineNo);
}

void LogFile::closeLog() {
    if (this->logfp_ != nullptr){
        fclose(this->logfp_);
        this->logfp_ = nullptr;
    }
}
