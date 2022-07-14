/**
 *
 * Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
 * Distributed under the GNU GPL v2. For full terms see the file LICENSE.
 *
 * Utility class to log program errors and definition of useful macros.
 *
 */

#ifndef LOGGING_H__
#define LOGGING_H__

#include <iostream> // std::cerr
#include <cstddef> // std::size_t
#include <cstdlib> // exit()

// some default constants
#define LOG_LEVEL_DEF ERROR
#define LOG_LEN_MAX_DEF 1048576 // 1 MiB

// some useful macros
#define LOG_INIT(fname) do{if(fname!= NULL)LogFile::getInstance()->initLog(fname); else std::cerr << "error init log file " << fname << std::endl;}while(0);
#define LOG_LEVEL_SET(level) LogFile::getInstance()->setLevel(level);
#define LOG_VERBOSE(msg) LogFile::getInstance()->writeLog(VERBOSE, msg, __FILE__, __LINE__);
#define LOG_MSG(msg) LogFile::getInstance()->writeLog(MSG, msg, __FILE__, __LINE__);
#define LOG_WARN(msg) LogFile::getInstance()->writeLog(WARN, msg, __FILE__, __LINE__);
#define LOG_ERR(msg) LogFile::getInstance()->writeLog(ERROR, msg, __FILE__, __LINE__);
#define LOG_FATAL(msg) LogFile::getInstance()->writeLog(FATAL, msg, __FILE__, __LINE__);
#define LOG_FATAL_EXIT(msg) do{LOG_FATAL(msg) exit(1);}while(0);
#define LOG_FATAL_PERROR(msg) LogFile::getInstance()->writeLogPerror(FATAL, msg, __FILE__, __LINE__);
#define LOG_FATAL_PERROR_EXIT(msg) do{LOG_FATAL_PERROR(msg) exit(1);}while(0);
#define LOG_CLOSE() LogFile::getInstance()->closeLog();

enum LogLevel {FATAL, ERROR, WARN, MSG, VERBOSE, NLOG_LEVELS};
typedef enum LogLevel LogLevel;

class LogFile {

protected:
  static LogFile* single_; // singleton instance

  FILE *logfp_;
  LogLevel level_;

  LogFile();

public:
  static LogFile* getInstance();

  void initLog(const char* fname);

  void initLog(const char* fname, std::size_t maxlen);

  void setLevel(const LogLevel level);

  void writeLog(LogLevel level, const char* pmsg, const char* pFname,
                int lineNo);

  void writeLogPerror(LogLevel level, const char* pMsg, const char* pFname,
                      int lineNo);

  void closeLog();

  /**
   * Singletons shouldn't be cloneable.
   */
  LogFile(LogFile& other) = delete;

  /**
   * Singletons shouldn't be assignable.
   */
  void operator=(const LogFile&) = delete;
};

#endif 
