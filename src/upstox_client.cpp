/**
 * @file upstox_client.cpp
 * @brief Implementation of the Upstox v3 API Client
 */

#include "upstox_client.hpp"
#include <iostream>
#include <sstream>
#include <chrono>

// We require httplib.h for the REST API calls. 
// If it's missing, we compile a dummy version so the build succeeds.
#if __has_include("httplib.h")
    #include "httplib.h"
#else
    #pragma message("WARNING: httplib.h not found. Upstox API integration will fail at runtime.")
    namespace httplib {
        using Headers = std::multimap<std::string, std::string>;
        struct ResultData {
            int status = 0;
            std::string body;
        };
        struct Result {
            ResultData* data = nullptr;
            operator bool() const { return false; } 
            ResultData* operator->() { return data; }
        };
        class Client {
        public:
            Client(const char*) {}
            void set_connection_timeout(int, int) {}
            void set_read_timeout(int, int) {}
            Result Get(const char*, const Headers&) { return Result{}; }
        };
    }
#endif

#include <nlohmann/json.hpp>

namespace upstox {

// Hardcoded most common instruments to avoid downloading a 100MB list every time.
// In production, sync this daily or do a lookup.
const std::map<std::string, std::string> Client::knownInstruments_ = {
    // Indexes
    {"NIFTY",      "NSE_INDEX|Nifty 50"},
    {"BANKNIFTY",  "NSE_INDEX|Nifty Bank"},
    {"FINNIFTY",   "NSE_INDEX|Nifty Fin Service"},
    // Top NSE EQ
    {"RELIANCE",   "NSE_EQ|INE002A01018"},
    {"HDFCBANK",   "NSE_EQ|INE040A01034"},
    {"INFY",       "NSE_EQ|INE009A01021"},
    {"TCS",        "NSE_EQ|INE467B01029"},
    {"SBIN",       "NSE_EQ|INE062A01020"},
    {"ICICIBANK",  "NSE_EQ|INE090A01021"},
    // Example BSE EQ
    {"RELIANCE_BSE", "BSE_EQ|500325"},
    {"HDFCBANK_BSE", "BSE_EQ|500180"}
};

Client::Client(const Config& config) : config_(config) {
    if (config_.accessToken.empty()) {
        throw ApiException("Upstox access token is missing or empty.");
    }
}

std::string Client::getInstrumentKey(const std::string& symbol, const std::string& exchange) const {
    std::string lookup = symbol;
    if (exchange == "BSE_EQ") lookup += "_BSE";
    
    auto it = knownInstruments_.find(lookup);
    if (it != knownInstruments_.end()) {
        return it->second;
    }
    
    // Fallback: This requires the underlying instrument key format
    // Ideally downloaded via the instruments API
    throw ApiException("Unknown symbol: " + symbol + " for exchange: " + exchange + 
                       ". Please add it to known instruments map.");
}

std::string Client::get(const std::string& path) const {
    httplib::Client cli("https://api.upstox.com");
    cli.set_connection_timeout(5, 0); // 5 seconds
    cli.set_read_timeout(10, 0);      // 10 seconds

    httplib::Headers headers = {
        {"Accept", "application/json"},
        {"Authorization", "Bearer " + config_.accessToken}
    };

    auto res = cli.Get(path.c_str(), headers);
    
    if (!res) {
        throw ApiException("HTTP Request failed: Connection Error to api.upstox.com");
    }
    
    if (res->status != 200) {
        throw ApiException("Upstox API Error (HTTP " + std::to_string(res->status) + "): " + res->body);
    }
    
    return res->body;
}

std::pair<std::vector<Quote>, MarketData> Client::fetchOptionChain(const std::string& instrumentKey, 
                                                                   const std::string& expiry) const {
    std::vector<Quote> quotes;
    MarketData md;
    
    // Endpoint: GET /v2/option/chain?instrument_key=...&expiry_date=...
    std::string path = "/v2/option/chain?instrument_key=" + instrumentKey;
    if (!expiry.empty()) {
        path += "&expiry_date=" + expiry;
    }

    std::string responseBody = get(path);
    nlohmann::json j;
    
    try {
        j = nlohmann::json::parse(responseBody);
    } catch (const std::exception& e) {
        throw ApiException("Failed to parse Upstox JSON response: " + std::string(e.what()));
    }

    if (j.value("status", "") != "success") {
        throw ApiException("Upstox returned error status: " + responseBody);
    }
    
    // Parse the data
    const auto& dataArr = j["data"];
    if (dataArr.empty()) {
        throw ApiException("Empty option chain data returned from Upstox.");
    }
    
    // We assume the underlying spot is the same for all strikes in this payload
    double spotPrice = 0.0;
    bool spotSet = false;
    
    auto now = std::chrono::system_clock::now();
    double currentYear = 365.25 * 24 * 3600; // rough seconds in a year

    for (const auto& item : dataArr) {
        double strike = item.value("strike_price", 0.0);
        
        // Parse expiry to time-to-maturity (T)
        // Upstox provides expiry in ISO 8601 or similar format, e.g., "2024-04-25"
        std::string expStr = item.value("expiry_date", "");
        double T = 0.25; // Default fallback
        if (!expStr.empty()) {
            // Simplified parsing for "YYYY-MM-DD"
            std::tm tm = {};
            int y, m, d;
#ifdef _MSC_VER
            if (sscanf_s(expStr.c_str(), "%d-%d-%d", &y, &m, &d) == 3) {
#else
            if (sscanf(expStr.c_str(), "%d-%d-%d", &y, &m, &d) == 3) {
#endif
                tm.tm_year = y - 1900;
                tm.tm_mon = m - 1;
                tm.tm_mday = d;
                tm.tm_hour = 15; // 3:30 PM Expiry
                tm.tm_min = 30;
                
                auto expTime = std::chrono::system_clock::from_time_t(mktime(&tm));
                auto diffCount = std::chrono::duration_cast<std::chrono::seconds>(expTime - now).count();
                T = std::max(diffCount / currentYear, 0.0001); // Avoid 0 T
            }
        }

        // Parse CALL
        if (item.contains("call_options") && !item["call_options"].is_null()) {
            const auto& ce = item["call_options"];
            
            // Set spot once
            if (!spotSet && ce.contains("underlying_spot_price")) {
                spotPrice = ce.value("underlying_spot_price", 0.0);
                spotSet = true;
            }
            
            // Vol_arb expects IV as a decimal (e.g. 0.25 for 25%). Upstox provides it as is if available
            // Sometimes IV is nested in market_data or greeks.
            double iv = 0.0;
            if (ce.contains("market_data") && ce["market_data"].contains("implied_volatility")) {
                iv = ce["market_data"]["implied_volatility"].get<double>() / 100.0; // Assume Upstox returns percentage
            }
            
            // Optional bid/ask/vol
            double bid = 0.0, ask = 0.0, vol = 0.0;
            if (ce.contains("market_data")) {
                bid = ce["market_data"].value("bid_price", 0.0);
                ask = ce["market_data"].value("ask_price", 0.0);
                vol = ce["market_data"].value("volume", 0.0);
            }
            
            if (iv > 0.001) {
                quotes.push_back({strike, T, iv, bid, ask, vol});
            }
        }
        
        // Note: The vol_arb detector currently ignores puts for arbitrage detection as it uses Calls,
        // but we could parse PUTs here similarly and convert via put-call parity if needed.
    }
    
    if (!spotSet || spotPrice <= 0.0) {
        throw ApiException("Could not determine underlying spot price from Upstox response.");
    }
    
    md.spot = spotPrice;
    md.riskFreeRate = 0.065; // ~6.5% INR risk free rate
    md.dividendYield = 0.0;
    md.valuationDate = "LIVE";
    md.currency = "INR";
    
    return {quotes, md};
}

} // namespace upstox
