/**
 *
 * Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
 * Distributed under the GNU GPL v2. For full terms see the file LICENSE.
 *
 * Based on work done by René Nyffenegger {rene.nyffenegger@adp-gmbh.ch}.
 *
 */

#include "configfile.hpp"
#include "logfile.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

std::string trim(std::string const& source, char const* delims = " \t\r\n") {

    std::string result(source);
    std::string::size_type index = result.find_last_not_of(delims);
    if(index != std::string::npos)
        result.erase(++index);

    index = result.find_first_not_of(delims);
    if(index != std::string::npos)
        result.erase(0, index);
    else
        result.erase();
    return result;
}

ConfigFile::ConfigFile(std::string const& configFile) {

    std::ifstream file(configFile.c_str());
    if (not file.good()) {
        std::stringstream ss;
        ss << "Could not open config file \"" << configFile << "\". Will use defaults.";
        LOG_ERR(ss.str().c_str());
    }

    std::string line;
    std::string name;
    std::string value;
    std::string inSection;
    std::size_t posEqual;
    std::size_t posHash;
    while (std::getline(file,line)) {

        if (!line.length()) continue;

        if (line[0] == '#') continue;

        if (line[0] == '[') {
            inSection=trim(line.substr(1,line.find(']')-1));
            sections_.push_back(inSection);
            continue;
        }

        // start by discarding everything from first # onwards (if # exists)
        posHash = line.find('#');
        if (posHash != std::string::npos)
            line = line.substr(0, posHash); // substr of len posHash, starting at 0

        posEqual = line.find('=');
        name  = trim(line.substr(0,posEqual));

        value = trim(line.substr(posEqual+1));

        content_[inSection+'/'+name] = value;
    }
}

std::vector<std::string> ConfigFile::GetSections() {
    return sections_;
}


std::string const& ConfigFile::Value(std::string const& section, 
        std::string const& entry) const {

    std::map<std::string, std::string>::const_iterator ci = content_.find(section + '/' + entry);

    if (ci == content_.end()) {
        throw std::runtime_error("does not exist");
    }

    return ci->second;
}


std::string const& ConfigFile::Value(std::string const& section, 
        std::string const& entry, 
        std::string const& value) {
    try {
        return Value(section, entry);
    } catch(const char *) {
        return content_.insert(std::make_pair(section+'/'+entry, value)).\
            first->second;
    }
}
