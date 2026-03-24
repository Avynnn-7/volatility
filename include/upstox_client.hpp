/**
 * @file upstox_client.hpp
 * @brief C++ REST Client for Upstox v3 API
 * @author vol_arb Team
 *
 * Handles fetching live option chain data from Upstox (NSE/BSE).
 * Maps stock symbols to instrument keys and generates VolSurface inputs.
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include "vol_surface.hpp"

// Define HTTPLIB_AVAILABLE if the header is present, done in CMake/main usually.
// For the pure implementation, we assume cpp-httplib is available since we are 
// using it for the API server anyway.

namespace upstox {

struct Config {
    std::string apiKey;
    std::string apiSecret;
    std::string redirectUri;
    std::string accessToken;
};

/**
 * @brief Exception thrown on API errors
 */
class ApiException : public std::runtime_error {
public:
    explicit ApiException(const std::string& msg) : std::runtime_error(msg) {}
};

/**
 * @brief Upstox v3 API Client
 *
 * Note: Requires a valid access token. We won't implement the full OAuth HTTP server 
 * inside the C++ client to keep it simple; the user/Node backend provides the token.
 */
class Client {
public:
    explicit Client(const Config& config);

    /**
     * @brief Lookup instrument key for a symbol (NSE or BSE)
     * @param symbol Stock symbol (e.g., "RELIANCE", "NIFTY", "INFY")
     * @param exchange "NSE_INDEX", "NSE_EQ", "BSE_EQ"
     * @return Instrument key string (e.g., "NSE_INDEX|Nifty 50")
     */
    std::string getInstrumentKey(const std::string& symbol, const std::string& exchange = "NSE_EQ") const;

    /**
     * @brief Fetch live option chain and convert to vol_arb quotes
     * @param instrumentKey Upstox instrument key
     * @param expiry Optional specific expiry (YYYY-MM-DD). If empty, fetches next 3 expiries.
     * @return Pair of Quotes and MarketData ready for VolSurface
     */
    std::pair<std::vector<Quote>, MarketData> fetchOptionChain(const std::string& instrumentKey, 
                                                               const std::string& expiry = "") const;

private:
    Config config_;
    
    std::string get(const std::string& path) const;
    
    // Internal hardcoded mapping for top symbols to assist without downloading full 100MB instruments file
    static const std::map<std::string, std::string> knownInstruments_;
};

} // namespace upstox

