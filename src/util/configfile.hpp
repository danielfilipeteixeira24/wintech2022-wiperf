/**
 *
 * Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
 * Distributed under the GNU GPL v2. For full terms see the file LICENSE.
 *
 * Based on work done by René Nyffenegger {rene.nyffenegger@adp-gmbh.ch}.
 *
 * This file defines the Config File class. An object of this class contains configuration data
 * uploaded from a given file. The class provides methods to access said data and performs
 * basic string validation.
 *
 */

#ifndef CONFIG_FILE_H__
#define CONFIG_FILE_H__

#include <string>
#include <map>
#include <vector>

class ConfigFile {
    std::map   <std::string, std::string> content_;
    std::vector<std::string>              sections_;

public:
    ConfigFile(std::string const& configFile);
    
    std::vector<std::string> GetSections();
	
    std::string const& Value(std::string const& section, 
                             std::string const& entry) const;
	
    std::string const& Value(std::string const& section, 
                             std::string const& entry, 
                             std::string const& value);
};

#endif 
