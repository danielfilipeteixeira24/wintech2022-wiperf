/**
 *
 * Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
 * Distributed under the GNU GPL v2. For full terms see the file LICENSE.
 *
 * Abstract class that provides a template for the creation of a data sender or receiver program.
 *
 */

#ifndef DTRANSFER_H__
#define DTRANSFER_H__

#include <string>       // std::string
#include <map>          // std::map
#include <utility>      // std::pair
#include <cstdint>     // uint*_t
#include <netinet/in.h> // struct sockaddr_in

#include "../util/configfile.hpp"        // class ConfigFile
#include "WiperfUtility.hpp"

// declare globals needed to end things cleanly on exit
// extern so they can be initialized in a single cc file
extern bool endProgram_;
extern int wakefd_;

void sigHandler(int); // for clean termination

class DataTransfer {

protected:
  bool endProgram_;
  int wakefd_;

  std::string printTag;
  std::string gpsShmPath;

  IfaceInfoMap ifaceMap; // iface name -> iface info
  pthread_mutex_t ifaceMapMutex{}; // to ensure exclusive access

  uint16_t portSrv{}; // server (receiver) port
  uint16_t portCli{}; // client (sender) port

  // protected constructor so only children can call it
  explicit DataTransfer(std::string printTag);
  
  // to be implemented by concrete classes
  virtual void readAndSetLogLevel(ConfigFile& cfile) = 0;

  void createSockaddr(const std::string& addrStr, uint16_t port,
                      struct sockaddr_in* sockaddr);

  /**
   * Close all of the interface sockets (which are assumed open).
   */
  void closeIfaceSocks();
  
  // worker threads
  void printerThread();
  virtual void commThread() = 0; // subclasses must implement

public:
  virtual void readConfig(const std::string& configFname) = 0;
  void run(); // launches the threads that do actual work
  virtual void stopThread();

  std::string getGpsShmPath();
  // For feedback sender to safely access the iface info map
  IfaceInfoMap getIfaceInfoMap();
  pthread_mutex_t getIfaceInfoMutex();

};

#endif
