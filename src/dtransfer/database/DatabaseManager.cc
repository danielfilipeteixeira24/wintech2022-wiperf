/**
 *
 * Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
 * Distributed under the GNU GPL v2. For full terms see the file LICENSE.
 *
 */

#include <libpq-fe.h>

#include "DatabaseManager.hpp"
#include "DatabaseInfo.hpp"

#include <utility>
#include "../../util/logfile.hpp"

DatabaseManager::DatabaseManager() : dbName(), host(), dbUser(), password() {}

DatabaseManager::DatabaseManager(
        std::string dbName, std::string host, std::string dbUser, std::string password) :
        dbName(std::move(dbName)), host(std::move(host)), dbUser(std::move(dbUser)), password(std::move(password)) {
}

void DatabaseManager::configure(ConfigFile &configFile) {
    dbName = configFile.Value("database", "db-name");
    host = configFile.Value("database", "host");
    dbUser = configFile.Value("database", "user");
    password = configFile.Value("database", "password");
}

void DatabaseManager::createAll(std::vector<DatabaseInfo> &databaseInfoList) {
    // The creation order doesn't matter unless entry t data depends on entry t-1
    // ot t-2

    for (DatabaseInfo &databaseInfo : databaseInfoList) {
        this->create(databaseInfo);
    }
}

void DatabaseManager::create(DatabaseInfo &databaseInfo) {

    // This adds another entry to Location
    // Since location has UNIQUE(lat, lon), there won't be two equal locations
    // $1 -> latitude
    // $2 -> longitude
    const std::string insertLocationPrepStatement =
            "INSERT INTO location (latitude, longitude) "
            "VALUES ($1, $2) ON CONFLICT DO NOTHING;";
    const int insertLocationNumParams = 2;

    // Adds entry to history
    // The locationId is obtained by querying the Location table based on lat and lon
    // The throughput is computed based on the time and number of bits compared with the
    // last History entry for the same RAT
    // $1 -> timestamp (in seconds)
    // $2 -> throughput
    // $3 -> num_bits
    // $4 -> channel_info
    // $5 -> scan_info
    // $6 -> rat
    // $7 -> speed
    // $8 -> orientation
    // $9 -> moving
    // $10 -> tx_bitrate
    // $11 -> signal_strength
    // $12 -> latitude
    // $13 -> longitude

    std::string insertHistoryPrepStatement;
    const int insertHistoryNumParams = 13;

    if (databaseInfo.numBits == 0 && databaseInfo.throughput == 0 && !databaseInfo.channelInfo.empty()) {
        //If the throughput is empty and it has channel info => CHANNEL MONITOR
        insertHistoryPrepStatement =
                "INSERT INTO history "
                "(timestamp, throughput, num_bits, channel_info, scan_info, rat, speed, "
                "orientation, moving, tx_bitrate, signal_strength, location_id) "
                "VALUES (to_timestamp($1), $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, "
                "        (SELECT location_id "
                "         FROM location "
                "         WHERE latitude = $12 "
                "           AND longitude = $13)) "
                "ON CONFLICT (timestamp, rat) DO UPDATE "
                "       SET channel_info = excluded.channel_info,"
                "           tx_bitrate = excluded.tx_bitrate,"
                "           signal_strength = excluded.signal_strength; ";
    }
    else if (databaseInfo.numBits == 0 && databaseInfo.throughput == 0 && !databaseInfo.scanInfo.empty()) {
        //If the throughput is empty and it has scan info => CHANNEL MONITOR SCAN
        insertHistoryPrepStatement =
                "INSERT INTO history "
                "(timestamp, throughput, num_bits, channel_info, scan_info, rat, speed, "
                "orientation, moving, tx_bitrate, signal_strength, location_id) "
                "VALUES (to_timestamp($1), $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, "
                "        (SELECT location_id "
                "         FROM location "
                "         WHERE latitude = $12 "
                "           AND longitude = $13)) "
                "ON CONFLICT (timestamp, rat) DO UPDATE "
                "       SET scan_info = excluded.scan_info; ";
    }
    else {
        //else => FEEDBACK RECEIVER
        insertHistoryPrepStatement =
                "INSERT INTO history "
                "(timestamp, throughput, num_bits, channel_info, scan_info, rat, speed, "
                "orientation, moving, tx_bitrate, signal_strength, location_id) "
                "VALUES (to_timestamp($1), $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, "
                "        (SELECT location_id "
                "         FROM location "
                "         WHERE latitude = $12 "
                "           AND longitude = $13)) "
                "ON CONFLICT (timestamp, rat) DO UPDATE "
                "       SET throughput = excluded.throughput, "
                "           num_bits = excluded.num_bits, "
                "           speed = excluded.speed, "
                "           orientation = excluded.orientation, "
                "           moving = excluded.moving, "
                "           location_id = excluded.location_id; ";
    }

    try {
        std::string connectionString = this->getConnectionString();
        PGconn *conn = PQconnectdb(connectionString.c_str());

        /* Check to see that the backend conn was successfully made */
        if (PQstatus(conn) != CONNECTION_OK) {
            std::stringstream ss;
            ss << "Connection to database failed: " << PQerrorMessage(conn);
            LOG_ERR(ss.str().c_str());

            std::cout << ss << std::endl;
            PQfinish(conn);
            return;
        }

        PGresult *res = NULL;

        // Start a Transaction block
        res = PQexec(conn, "BEGIN");
        if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::stringstream ss;
            ss << "PQexec failed:" << PQresultErrorMessage(res);
            LOG_ERR(ss.str().c_str());

            std::cout << ss << std::endl;

            PQclear(res);
            PQfinish(conn);
            return;
        }

        // Prepare statements
        res = PQprepare(conn, "insert_location", insertLocationPrepStatement.c_str(), insertLocationNumParams, NULL);
        if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::stringstream ss;
            ss << "PQprepare failed:" << PQresultErrorMessage(res);
            LOG_ERR(ss.str().c_str());

            std::cout << ss << std::endl;

            PQclear(res);
            PQfinish(conn);
            return;
        }

        PQclear(res);

        res = PQprepare(conn, "insert_history", insertHistoryPrepStatement.c_str(), insertHistoryNumParams, NULL);
        if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::stringstream ss;
            ss << "PQprepare failed:" << PQresultErrorMessage(res);
            LOG_ERR(ss.str().c_str());

            std::cout << ss << std::endl;

            PQclear(res);
            PQfinish(conn);
            return;
        }
        PQclear(res);

        // EXECUTE STATEMENT TO INSERT LOCATION and HISTORY

        int resultFormat = 0;

        std::string param_latitude = std::to_string(databaseInfo.latitude);
        std::string param_longitude = std::to_string(databaseInfo.longitude);
        std::string param_timestamp = std::to_string(databaseInfo.timestamp / (double) 1000);
        std::string param_throughput = std::to_string(databaseInfo.throughput);
        std::string param_numbits = std::to_string(databaseInfo.numBits);
        std::string param_channelinfo = databaseInfo.channelInfo;
        std::string param_scaninfo = databaseInfo.scanInfo;
        std::string param_rat = databaseInfo.rat;
        std::string param_speed = std::to_string(databaseInfo.speed);
        std::string param_orientation = std::to_string(databaseInfo.orientation);
        std::string param_moving = std::to_string(databaseInfo.moving);
        std::string param_txbitrate = std::to_string(databaseInfo.tx_bitrate);
        std::string param_signalstrength = std::to_string(databaseInfo.signal_strength);

        const char *const insertLocationParamValues[] = { param_latitude.c_str(), param_longitude.c_str() };

        res = PQexecPrepared(conn, "insert_location", insertLocationNumParams, insertLocationParamValues,
                             nullptr, nullptr, resultFormat);

        if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::stringstream ss;
            ss << "PQexecPrepared failed: " << PQresultErrorMessage(res);
            LOG_ERR(ss.str().c_str());

            std::cout << ss << std::endl;

            PQclear(res);
            PQfinish(conn);
            return;
        }

        PQclear(res);

        const char *const insertHistoryParamValues[] = {
                param_timestamp.c_str(), param_throughput.c_str(), param_numbits.c_str(),
                param_channelinfo.c_str(), param_scaninfo.c_str(), param_rat.c_str(),
                param_speed.c_str(), param_orientation.c_str(), param_moving.c_str(),
                param_txbitrate.c_str(), param_signalstrength.c_str(), param_latitude.c_str(),
                param_longitude.c_str()};

        res = PQexecPrepared(conn, "insert_history", insertHistoryNumParams,
                             insertHistoryParamValues, nullptr, nullptr, resultFormat);

        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::stringstream ss;
            ss << "PQexecPrepared failed: " << PQresultErrorMessage(res);
            LOG_ERR(ss.str().c_str());

            std::cout << ss << std::endl;

            PQclear(res);
            PQfinish(conn);
            return;
        }
        PQclear(res);

        /* commit the transaction */
        res = PQexec(conn, "COMMIT");
        PQclear(res);

        /* close the connection to the database and cleanup */
        PQfinish(conn);
    }
    catch (std::exception const &exception) {
        std::cout << "[DEBUG] exception :: " << exception.what() << std::endl;
        LOG_ERR(exception.what())
    }
}

void DatabaseManager::updateScanInfo(DatabaseInfo &databaseInfo, uint64_t begin, uint64_t end) {
    // Update a history entry
    // The entries corresponding to the given RAT and between the begin and end
    // timestamps are updated with the scan information

    // $1 -> scan_info
    // $2 -> rat
    // $3 -> timestamp begin
    // $4 -> timestamp end
    std::string updateScanInfoPrepStatement =
            "UPDATE history "
            "SET scan_info = $1 "
            "WHERE rat = $2 "
            "  AND timestamp > $3 "
            "  AND timestamp <= $4; ";
    const int updateScanIfoNumParams = 4;

    try {
        std::string connectionString = this->getConnectionString();
        PGconn *conn = PQconnectdb(connectionString.c_str());

        /* Check to see that the backend conn was successfully made */
        if (PQstatus(conn) != CONNECTION_OK) {
            std::stringstream ss;
            ss << "Connection to database failed: " << PQerrorMessage(conn);
            LOG_ERR(ss.str().c_str());

            std::cout << ss << std::endl;
            PQfinish(conn);

            return;
        }

        PGresult *res = NULL;

        // Prepare statements
        res = PQprepare(conn, "update_history_scan_info", updateScanInfoPrepStatement.c_str(), updateScanIfoNumParams,
                        NULL);

        int resultFormat = 0;

        std::string param_scaninfo = databaseInfo.scanInfo;
        std::string param_rat = databaseInfo.rat;
        std::string param_begin = std::to_string(begin);
        std::string param_end = std::to_string(end);

        const char *const updateScaninfoParamValues[] = {param_scaninfo.c_str(), param_rat.c_str(),
                                                         param_begin.c_str(), param_end.c_str()};

        // EXECUTE STATEMENT TO UPDATE HISTORY
        if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::stringstream ss;
            ss << "PQprepare failed:" << PQresultErrorMessage(res);
            LOG_ERR(ss.str().c_str());

            std::cout << ss << std::endl;
            PQclear(res);
            PQfinish(conn);
            return;
        } else {
            /*
             * ParamValues -> array with the values for the parameters
             * ParamLengths -> length of binay-format values (ignored for text values)
             * ParamFormat -> specify if the params are text (0) or binary (1) (if null, all are text)
             */
            PQclear(res);

            res = PQexecPrepared(conn, "update_history_scan_info", updateScanIfoNumParams,
                                 updateScaninfoParamValues, nullptr, nullptr, resultFormat);

            if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
                std::stringstream ss;
                ss << "PQexecPrepared failed: " << PQresultErrorMessage(res);
                LOG_ERR(ss.str().c_str());

                std::cout << ss << std::endl;

                PQclear(res);
                PQfinish(conn);
                return;
            }

            PQclear(res);
        }

        PQfinish(conn);
    }
    catch (std::exception const &exception) {
        LOG_ERR(exception.what())
    }
}

std::list<DatabaseInfo> DatabaseManager::retrieveAllByPosition(
        const double latitude, const double longitude, const std::string &rat, const double radius) {

    double radius_decimal_degrees = DatabaseManager::metersToDecimalDegrees(radius);
    //If radius is in meters => decimal degrees = meters * .00001 (convert meters to decimal degrees)

    //Queries for all entries in the given coordinates and for the given RAT
    // $1 -> latitude
    // $2 -> longitude
    // $3 -> RAT
    // $4 -> radius
    const std::string queryAllPrepStatement =
            "SELECT EXTRACT(EPOCH FROM timestamp) * 1000, throughput, num_bits, channel_info, scan_info, rat,"
            " speed, orientation, moving, tx_bitrate, signal_strength, latitude, longitude "
            "FROM history INNER JOIN location "
            " USING(location_id) "
            " WHERE abs(latitude - $1) <= $4 "
            "   AND abs(longitude - $2) <= $4 "
            "   AND rat = $3;";
    const int queryAllNumParams = 4;

    std::list<DatabaseInfo> databaseInfoList;

    try {
        std::string connectionString = this->getConnectionString();
        PGconn *conn = PQconnectdb(connectionString.c_str());

        /* Check to see that the backend conn was successfully made */
        if (PQstatus(conn) != CONNECTION_OK) {
            std::stringstream ss;
            ss << "Connection to database failed: " << PQerrorMessage(conn);
            LOG_ERR(ss.str().c_str());

            std::cout << ss << std::endl;
            PQfinish(conn);
            return databaseInfoList;
        }

        PGresult *res = NULL;
        res = PQprepare(conn, "query_all_location", queryAllPrepStatement.c_str(), queryAllNumParams, NULL);

        int resultFormat = 0;

        std::string param_latitude = std::to_string(latitude);
        std::string param_longitude = std::to_string(longitude);
        std::string param_radius = std::to_string(radius_decimal_degrees);

        const char *const queryAllParamValues[] = { param_latitude.c_str(), param_longitude.c_str(),
                                                    rat.c_str(), param_radius.c_str() };

        // EXECUTE STATEMENT TO UPDATE HISTORY
        if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::stringstream ss;
            ss << "PQprepare failed:" << PQresultErrorMessage(res);
            LOG_ERR(ss.str().c_str());

            std::cout << ss << std::endl;

            PQclear(res);
            PQfinish(conn);
            return databaseInfoList;
        }
        else
        {
            PQclear(res);
            res = PQexecPrepared(conn, "query_all_location", queryAllNumParams, queryAllParamValues,
                                 nullptr, nullptr, resultFormat);

            if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                std::stringstream ss;
                ss << "PQexecPrepared failed:" << PQresultErrorMessage(res);
                LOG_ERR(ss.str().c_str());

                std::cout << ss << std::endl;
                PQclear(res);
                PQfinish(conn);

                return databaseInfoList;
            }
            else {

                for (int i = 0; i < PQntuples(res); i++) {
                    const std::string timestamp(PQgetvalue(res, i, 0));
                    const std::string throughput(PQgetvalue(res, i, 1));
                    const std::string numBits(PQgetvalue(res, i, 2));
                    const std::string channelInfo(PQgetvalue(res, i, 3));
                    const std::string scanInfo(PQgetvalue(res, i, 4));
                    const std::string speed(PQgetvalue(res, i, 6));
                    const std::string orientation(PQgetvalue(res, i, 7));
                    const std::string moving(PQgetvalue(res, i, 8));
                    const std::string tx_bitrate(PQgetvalue(res, i, 9));
                    const std::string signal_strength(PQgetvalue(res, i, 10));

                    DatabaseInfo databaseInfo;
                    databaseInfo.longitude = longitude;
                    databaseInfo.latitude = latitude;
                    databaseInfo.timestamp = std::stoll(timestamp.c_str());
                    databaseInfo.throughput = std::stoi(throughput.c_str());
                    databaseInfo.numBits = std::stoi(numBits.c_str());
                    databaseInfo.channelInfo = channelInfo;
                    databaseInfo.scanInfo = scanInfo;
                    databaseInfo.rat = rat;
                    databaseInfo.speed = std::stod(speed.c_str());
                    databaseInfo.orientation = std::stod(orientation.c_str());
                    databaseInfo.moving = std::stoi(moving.c_str());
                    databaseInfo.tx_bitrate = std::stoi(tx_bitrate.c_str());
                    databaseInfo.signal_strength = std::stoi(signal_strength.c_str());

                    databaseInfoList.push_back(databaseInfo);
                }
            }
            PQclear(res);
        }
        PQfinish(conn);
    }
    catch (std::exception const &exception) {
        LOG_ERR(exception.what())
    }

    return databaseInfoList;
}

std::list<DatabaseInfo> DatabaseManager::retrieveForecastAllByPosition(
        const double latitude, const double longitude,
        const double forecast, const double interval, double radius) {

    double radius_decimal_degrees = DatabaseManager::metersToDecimalDegrees(radius);

    // Queries for every entry where the position corresponds to lat and lon coordinates
    // and adds the history_id index with the forecast
    // $1 -> latitude
    // $2 -> longitude
    // $3 -> forecast (in seconds)
    // $4 -> time interval between samples (in seconds)
    // $5 -> radius
    const std::string queryAllForecastPrepStatement =
            "WITH subquery (timestamp, rat) "
            "         AS ( "
            "        SELECT h2.timestamp, h2.rat "
            "        FROM history h2 "
            "                 INNER JOIN location l2 on l2.location_id = h2.location_id "
            "        WHERE abs(l2.latitude - $1) <= ($5 / 2) "
            "          AND abs(l2.longitude - $2) <= ($5 / 2) "
            "    ) "
            "SELECT EXTRACT(EPOCH FROM h1.timestamp) * 1000 as millis, "
            "       h1.throughput, "
            "       h1.num_bits, "
            "       h1.channel_info, "
            "       h1.scan_info, "
            "       h1.rat, "
            "       h1.speed, "
            "       h1.orientation, "
            "       h1.moving, "
            "       h1.tx_bitrate, "
            "       h1.signal_strength, "
            "       l1.latitude, "
            "       l1.longitude "
            "FROM history h1 "
            "         JOIN location l1 on l1.location_id = h1.location_id, "
            "     subquery h2 "
            "WHERE h1.rat = h2.rat "
            "  AND ABS(extract(epoch from h1.timestamp) - (extract(epoch from h2.timestamp) + $3)) <= $4;";
    const int queryAllForecastNumParams = 5;

    std::list<DatabaseInfo> databaseInfoList;

    try {
        std::string connectionString = this->getConnectionString();
        PGconn *conn = PQconnectdb(connectionString.c_str());

        /* Check to see that the backend conn was successfully made */
        if (PQstatus(conn) != CONNECTION_OK) {
            std::stringstream ss;
            ss << "Connection to database failed: " << PQerrorMessage(conn);
            LOG_ERR(ss.str().c_str());

            std::cout << ss << std::endl;
            PQfinish(conn);
            return databaseInfoList;
        }

        PGresult *res = NULL;
        res = PQprepare(conn, "query_forecast_position", queryAllForecastPrepStatement.c_str(),
                        queryAllForecastNumParams, NULL);

        int resultFormat = 0;

        std::string param_latitude = std::to_string(latitude);
        std::string param_longitude = std::to_string(longitude);
        std::string param_radius = std::to_string(radius_decimal_degrees);
        std::string param_forecast = std::to_string(forecast);
        std::string param_interval = std::to_string(interval);

        const char *const queryAllForecastParamValues[] = {
                param_latitude.c_str(), param_longitude.c_str(),
                param_radius.c_str(),
                param_forecast.c_str(), param_interval.c_str()};

        // EXECUTE STATEMENT TO UPDATE HISTORY
        if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::stringstream ss;
            ss << "PQprepare failed:" << PQresultErrorMessage(res);
            LOG_ERR(ss.str().c_str());

            std::cout << ss << std::endl;
            PQclear(res);
            PQfinish(conn);

            return databaseInfoList;
        }
        else
        {
            PQclear(res);

            res = PQexecPrepared(conn, "query_forecast_position", queryAllForecastNumParams,
                                 queryAllForecastParamValues, nullptr, nullptr, resultFormat);

            if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                std::stringstream ss;
                ss << "PQexecPrepared failed: " << PQresultErrorMessage(res);
                LOG_ERR(ss.str().c_str());

                std::cout << ss << std::endl;
                PQclear(res);
                PQfinish(conn);

                return databaseInfoList;
            } else {
                for (int i = 0; i < PQntuples(res); i++) {
                    const std::string timestamp(PQgetvalue(res, i, 0));
                    const std::string throughput(PQgetvalue(res, i, 1));
                    const std::string numBits(PQgetvalue(res, i, 2));
                    const std::string channelInfo(PQgetvalue(res, i, 3));
                    const std::string scanInfo(PQgetvalue(res, i, 4));
                    const std::string rat(PQgetvalue(res, i, 5));
                    const std::string speed(PQgetvalue(res, i, 6));
                    const std::string orientation(PQgetvalue(res, i, 7));
                    const std::string moving(PQgetvalue(res, i, 8));
                    const std::string tx_bitrate(PQgetvalue(res, i, 9));
                    const std::string signal_strength(PQgetvalue(res, i, 10));

                    DatabaseInfo databaseInfo;
                    databaseInfo.longitude = longitude;
                    databaseInfo.latitude = latitude;
                    databaseInfo.timestamp = std::stoi(timestamp.c_str());
                    databaseInfo.throughput = std::stoi(throughput.c_str());
                    databaseInfo.numBits = std::stoi(numBits.c_str());
                    databaseInfo.channelInfo = channelInfo;
                    databaseInfo.scanInfo = scanInfo;
                    databaseInfo.rat = rat;
                    databaseInfo.speed = std::stod(speed.c_str());
                    databaseInfo.orientation = std::stod(orientation.c_str());
                    databaseInfo.moving = std::stoi(moving.c_str());
                    databaseInfo.tx_bitrate = std::stoi(tx_bitrate.c_str());
                    databaseInfo.signal_strength = std::stoi(signal_strength.c_str());

                    databaseInfoList.push_back(databaseInfo);
                }
            }

            PQclear(res);
        }

        PQfinish(conn);
    }
    catch (std::exception const &exception) {
        LOG_ERR(exception.what())
    }

    return databaseInfoList;
}

std::string DatabaseManager::getConnectionString() {
    std::string connectionString = "postgresql://" + this->dbUser + ":" + this->password + "@"
                                   + this->host + "/" + this->dbName;
    return connectionString;
}

double DatabaseManager::metersToDecimalDegrees(double meters) {
    double decimalDegrees = meters * 0.000009009;
    return decimalDegrees;
}

double DatabaseManager::decimalDegreesToMeters(double decimalDegrees) {
    double meters = decimalDegrees * 111139;
    return meters;
}
