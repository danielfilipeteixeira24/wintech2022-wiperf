/**
 *
 * Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
 * Distributed under the GNU GPL v2. For full terms see the file LICENSE.
 *
 * This class defines the Database Manager entity. The Database Manager encapsulates
 * the methods for accessing the database and performing CRUD operations.
 */

#ifndef WIPERF_IMPL_DATABASEMANAGER_HPP
#define WIPERF_IMPL_DATABASEMANAGER_HPP

#include <string>
#include <iostream>
#include <sstream>
#include <queue>
#include <list>

#include "../../util/configfile.hpp"
#include "DatabaseInfo.hpp"

/**
 * This class represents an interface through which to access the database
 * and supports write and read operations.
 *
 * This interface is specific to PostgreSQL databases, as that is the one
 * that is used to store the historic records.
 */
class DatabaseManager {
private:
    std::string dbName;
    std::string host;
    std::string dbUser;
    std::string password;

    /**
     * Get the connection string to connect to the database.
     * @return connection string
     */
    std::string getConnectionString();

public:

    DatabaseManager(std::string  dbName, std::string  host,
                    std::string  dbUser, std::string  password);
    DatabaseManager();

    /**
     * Configure the database parameters (i.e., dbName, host, dbUser and password)
     * based on the information from the given configuration file.
     * @param configFile configuration file
     */
    void configure(ConfigFile &configFile);

    /**
     * Adds multiple entries to the database.
     * @param databaseInfoList
     */
    void createAll(std::vector<DatabaseInfo>& databaseInfoList);

    /**
     * Adds a new entry to the database.
     * @param databaseInfo
     */
    void create(DatabaseInfo& databaseInfo);

    /**
     * Updates entries between begin and end with new scan_info
     * @param databaseInfo
     * @param begin start milliseconds
     * @param end   end milliseconds
     */
    void updateScanInfo(DatabaseInfo& databaseInfo, uint64_t begin, uint64_t end);

    /**
     * Retrieves every entry that corresponds to the given coordinates and to
     * the specified RAT.
     * @param latitude  latitude coordinates
     * @param longitude longitude coordinates
     * @param rat       radio access technology
     * @param radius    radius to consider similar samples (in meters)
     * @return list of entries
     */
    std::list<DatabaseInfo> retrieveAllByPosition(
            double latitude, double longitude, const std::string& rat, double radius);

    /**
     * Gets the samples that correspond to the given coordinates, and
     * then shifts forward by {@code forecast} seconds.
     * Given the samples s^(t), where t is the sampling timestamp, and where their
     * position corresponds to the coordinates, we fetch the samples s^(t+f), where
     * f is the forecast offset.
     *
     * If the coordinates correspond to the current position, with t+f we can get
     * a set of samples which position can be used to predict where we will be in
     * f seconds from now.
     *
     * @param latitude  latitude
     * @param longitude longitude
     * @param radius    radius in meters
     * @param forecast  sample forecasting offset (in seconds)
     * @param interval  time interval between the samples (in seconds)
     *  (e.g., 0.333 s for 3Hz sampling frequency)
     * @return list of  offset entries to use in forecasting
     */
    std::list<DatabaseInfo> retrieveForecastAllByPosition(
            double latitude, double longitude, double forecast, double interval, double radius);

    //Convert from meters to decimal degrees and vice versa, to compare with lat and long
    static double metersToDecimalDegrees(double meters);
    static double decimalDegreesToMeters(double decimalDegrees);
};

#endif //WIPERF_IMPL_DATABASEMANAGER_HPP
