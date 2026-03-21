#include "data_handler.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <set>

// Data quality metrics implementation
double DataQualityMetrics::getOverallQuality() const {
    if (totalQuotes == 0) return 0.0;
    
    double completeness = completenessRatio;
    double consistency = consistencyScore;
    double validity = static_cast<double>(validQuotes) / totalQuotes;
    
    // Weighted average of quality metrics
    return 0.4 * completeness + 0.3 * consistency + 0.3 * validity;
}

bool DataQualityMetrics::isAcceptable() const {
    return getOverallQuality() >= 0.8 && validationErrors.empty();
}

// DataHandler implementation
DataHandler::DataHandler(const Config& config) : config_(config) {}

std::pair<std::vector<Quote>, MarketData> DataHandler::loadData() {
    qualityMetrics_ = DataQualityMetrics{};
    
    std::vector<Quote> rawQuotes;
    MarketData marketData;
    
    try {
        switch (config_.source) {
            case DataSource::JSON_FILE:
                std::tie(rawQuotes, marketData) = loadFromJSON();
                break;
            case DataSource::CSV_FILE:
                std::tie(rawQuotes, marketData) = loadFromCSV();
                break;
            case DataSource::BLOOMBERG:
            case DataSource::REUTERS:
            case DataSource::CUSTOM_API:
                std::tie(rawQuotes, marketData) = loadFromAPI();
                break;
        }
        
        qualityMetrics_.totalQuotes = static_cast<int>(rawQuotes.size());
        
        // Validate market data
        std::string marketDataError;
        if (!validateMarketData(marketData, marketDataError)) {
            throw std::runtime_error("Invalid market data: " + marketDataError);
        }
        
        // Clean and validate quotes
        std::vector<Quote> cleanedQuotes = cleanData(rawQuotes);
        qualityMetrics_.validQuotes = static_cast<int>(cleanedQuotes.size());
        
        // Calculate quality metrics
        calculateQualityMetrics(rawQuotes, cleanedQuotes);
        
        if (!qualityMetrics_.isAcceptable()) {
            std::cerr << "Warning: Data quality score is " 
                      << std::fixed << std::setprecision(3) 
                      << qualityMetrics_.getOverallQuality() << std::endl;
            
            for (const auto& error : qualityMetrics_.validationErrors) {
                std::cerr << "  - " << error << std::endl;
            }
        }
        
        return {cleanedQuotes, marketData};
        
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to load data: " + std::string(e.what()));
    }
}

bool DataHandler::validateQuote(const Quote& quote, std::string& errorMessage) const {
    // Validate strike
    if (quote.strike <= 0) {
        errorMessage = "Strike must be positive: " + std::to_string(quote.strike);
        return false;
    }
    
    // Validate expiry
    if (quote.expiry < config_.minTimeToExpiry || quote.expiry > config_.maxTimeToExpiry) {
        errorMessage = "Expiry " + std::to_string(quote.expiry) + 
                     " outside range [" + std::to_string(config_.minTimeToExpiry) + 
                     ", " + std::to_string(config_.maxTimeToExpiry) + "]";
        return false;
    }
    
    // Validate implied volatility
    if (quote.iv < config_.minVol || quote.iv > config_.maxVol) {
        errorMessage = "IV " + std::to_string(quote.iv * 100) + 
                     "% outside range [" + std::to_string(config_.minVol * 100) + 
                     "%, " + std::to_string(config_.maxVol * 100) + "%]";
        return false;
    }
    
    // Validate bid-ask spread if required
    if (config_.requireBidAsk) {
        if (quote.bid <= 0 || quote.ask <= 0 || quote.ask <= quote.bid) {
            errorMessage = "Invalid bid-ask: bid=" + std::to_string(quote.bid) + 
                         ", ask=" + std::to_string(quote.ask);
            return false;
        }
        
        double spread = quote.ask - quote.bid;
        if (spread < config_.minSpread) {
            errorMessage = "Bid-ask spread too small: " + std::to_string(spread);
            return false;
        }
    }
    
    return true;
}

std::vector<Quote> DataHandler::cleanData(const std::vector<Quote>& rawQuotes) const {
    std::vector<Quote> cleaned = rawQuotes;
    
    if (config_.enableDataCleaning) {
        // Remove invalid quotes
        cleaned.erase(
            std::remove_if(cleaned.begin(), cleaned.end(),
                [this](const Quote& q) {
                    std::string error;
                    return !this->validateQuote(q, error);
                }),
            cleaned.end()
        );
        
        // Remove duplicates
        if (config_.enableDuplicateRemoval) {
            removeDuplicates(cleaned);
        }
        
        // Detect and handle outliers
        if (config_.enableOutlierDetection) {
            detectOutliers(cleaned);
        }
        
        // Fill missing data (interpolation)
        fillMissingData(cleaned);
    }
    
    return cleaned;
}

std::pair<std::vector<Quote>, MarketData> DataHandler::loadFromJSON() const {
    std::ifstream file(config_.filePath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open JSON file: " + config_.filePath);
    }
    
    nlohmann::json j;
    file >> j;
    
    MarketData marketData;
    marketData.spot = j["spot"].get<double>();
    marketData.riskFreeRate = j.value("riskFreeRate", 0.05);  // Default 5%
    marketData.dividendYield = j.value("dividendYield", 0.0);  // Default 0%
    marketData.valuationDate = j.value("valuationDate", "2024-01-01");
    marketData.currency = j.value("currency", "USD");
    
    std::vector<Quote> quotes;
    for (const auto& q : j["quotes"]) {
        Quote quote;
        quote.strike = q["strike"].get<double>();
        quote.expiry = q["expiry"].get<double>();
        quote.iv = q["iv"].get<double>();
        quote.bid = q.value("bid", 0.0);
        quote.ask = q.value("ask", 0.0);
        quote.volume = q.value("volume", 0.0);
        quotes.push_back(quote);
    }
    
    return {quotes, marketData};
}

std::pair<std::vector<Quote>, MarketData> DataHandler::loadFromCSV() const {
    std::ifstream file(config_.filePath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open CSV file: " + config_.filePath);
    }
    
    // Simple CSV parsing - in production, use a proper CSV library
    std::string line;
    std::getline(file, line); // Skip header
    
    std::vector<Quote> quotes;
    MarketData marketData;
    marketData.spot = 100.0;  // Default
    marketData.riskFreeRate = 0.05;
    marketData.dividendYield = 0.0;
    marketData.valuationDate = "2024-01-01";
    marketData.currency = "USD";
    
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string cell;
        std::vector<std::string> row;
        
        while (std::getline(ss, cell, ',')) {
            row.push_back(cell);
        }
        
        if (row.size() >= 3) {
            // ═══════════════════════════════════════════════════════════════════════════
            // PHASE 5 ROBUSTNESS #5: CSV Parsing Error Recovery
            // Wrap std::stod in try/catch to handle malformed numeric data gracefully
            // ═══════════════════════════════════════════════════════════════════════════
            try {
                Quote quote;
                quote.strike = std::stod(row[0]);
                quote.expiry = std::stod(row[1]);
                quote.iv = std::stod(row[2]);
                
                if (row.size() > 3) quote.bid = std::stod(row[3]);
                if (row.size() > 4) quote.ask = std::stod(row[4]);
                if (row.size() > 5) quote.volume = std::stod(row[5]);
                
                // Basic validation before adding
                if (quote.strike > 0 && quote.expiry > 0 && quote.iv > 0) {
                    quotes.push_back(quote);
                }
            } catch (const std::invalid_argument& e) {
                // Skip rows with non-numeric data (log if needed)
                std::cerr << "Warning: Skipping CSV row with invalid numeric data: " << line << std::endl;
                continue;
            } catch (const std::out_of_range& e) {
                // Skip rows with values too large for double
                std::cerr << "Warning: Skipping CSV row with out-of-range value: " << line << std::endl;
                continue;
            }
        }
    }
    
    return {quotes, marketData};
}

std::pair<std::vector<Quote>, MarketData> DataHandler::loadFromAPI() const {
    // Placeholder for API loading
    throw std::runtime_error("API data loading not implemented yet");
}

bool DataHandler::validateMarketData(const MarketData& marketData, std::string& errorMessage) const {
    if (marketData.spot <= 0) {
        errorMessage = "Spot price must be positive: " + std::to_string(marketData.spot);
        return false;
    }
    
    if (marketData.riskFreeRate < -0.5 || marketData.riskFreeRate > 1.0) {
        errorMessage = "Risk-free rate out of reasonable range: " + std::to_string(marketData.riskFreeRate);
        return false;
    }
    
    if (marketData.dividendYield < 0 || marketData.dividendYield > 0.5) {
        errorMessage = "Dividend yield out of reasonable range: " + std::to_string(marketData.dividendYield);
        return false;
    }
    
    return true;
}

void DataHandler::detectOutliers(std::vector<Quote>& quotes) const {
    auto volStats = calculateVolatilityStats(quotes);
    double meanVol = volStats[0];
    double stdDevVol = volStats[1];
    
    int outlierCount = 0;
    for (auto& quote : quotes) {
        if (isOutlier(quote.iv, meanVol, stdDevVol)) {
            // Replace outlier with interpolated value or remove
            quote.iv = std::clamp(quote.iv, meanVol - 2*stdDevVol, meanVol + 2*stdDevVol);
            outlierCount++;
        }
    }
    
    qualityMetrics_.outlierQuotes = outlierCount;
}

void DataHandler::removeDuplicates(std::vector<Quote>& quotes) const {
    std::set<std::pair<double, double>> seen; // (strike, expiry) pairs
    
    auto newEnd = std::remove_if(quotes.begin(), quotes.end(),
        [&seen](const Quote& q) {
            auto key = std::make_pair(q.strike, q.expiry);
            if (seen.count(key)) {
                return true; // Remove duplicate
            }
            seen.insert(key);
            return false;
        });
    
    int removedCount = std::distance(newEnd, quotes.end());
    quotes.erase(newEnd, quotes.end());
    qualityMetrics_.duplicateQuotes = removedCount;
}

void DataHandler::fillMissingData(std::vector<Quote>& quotes) const {
    // Simple interpolation for missing data points
    // In production, use more sophisticated methods
    
    std::sort(quotes.begin(), quotes.end(),
        [](const Quote& a, const Quote& b) {
            if (std::abs(a.expiry - b.expiry) > 1e-6) {
                return a.expiry < b.expiry;
            }
            return a.strike < b.strike;
        });
    
    // Group by expiry and interpolate missing strikes
    std::map<double, std::vector<Quote*>> expiryGroups;
    for (auto& q : quotes) {
        expiryGroups[q.expiry].push_back(&q);
    }
    
    for (auto& [expiry, group] : expiryGroups) {
        if (group.size() < 2) continue;
        
        // Sort by strike
        std::sort(group.begin(), group.end(),
            [](const Quote* a, const Quote* b) { return a->strike < b->strike; });
        
        // Simple linear interpolation for missing points
        // This is a placeholder - real implementation would be more sophisticated
    }
}

void DataHandler::calculateQualityMetrics(const std::vector<Quote>& original, 
                                      const std::vector<Quote>& cleaned) {
    qualityMetrics_.totalQuotes = static_cast<int>(original.size());
    qualityMetrics_.validQuotes = static_cast<int>(cleaned.size());
    
    // Don't recalculate duplicateQuotes - already set by removeDuplicates()
    // Total rejected includes duplicates + invalid + outliers
    qualityMetrics_.rejectedQuotes = qualityMetrics_.totalQuotes - qualityMetrics_.validQuotes;
    
    qualityMetrics_.completenessRatio = static_cast<double>(qualityMetrics_.validQuotes) / 
                                        qualityMetrics_.totalQuotes;
    
    // Calculate consistency score based on volatility patterns
    if (!cleaned.empty()) {
        auto volStats = calculateVolatilityStats(cleaned);
        double mean = volStats[0];
        double stdDev = volStats[1];
        
        // ═══════════════════════════════════════════════════════════════════════════
        // PHASE 5 ROBUSTNESS #9: Numerical Stability - Protect division by zero
        // ═══════════════════════════════════════════════════════════════════════════
        double cv = (mean > 1e-10) ? stdDev / mean : 0.0; // Coefficient of variation
        qualityMetrics_.consistencyScore = std::max(0.0, 1.0 - cv);
    }
    
    // Check for validation errors
    if (qualityMetrics_.completenessRatio < 0.9) {
        qualityMetrics_.validationErrors.push_back("Low data completeness: " + 
            std::to_string(qualityMetrics_.completenessRatio * 100) + "%");
    }
    
    if (qualityMetrics_.consistencyScore < 0.7) {
        qualityMetrics_.validationErrors.push_back("Low data consistency: " + 
            std::to_string(qualityMetrics_.consistencyScore));
    }
}

double DataHandler::calculateZScore(double value, double mean, double stdDev) const {
    // Protect against division by zero AND very small stdDev
    // If stdDev is below threshold, data is essentially constant (no real outliers)
    // For implied volatility, 1e-6 = 0.0001% is well below any meaningful variation
    const double MIN_STDDEV = 1e-6;
    
    if (stdDev < MIN_STDDEV) {
        return 0.0;  // Data is constant - no outliers
    }
    
    return (value - mean) / stdDev;
}

bool DataHandler::isOutlier(double value, double mean, double stdDev) const {
    return std::abs(calculateZScore(value, mean, stdDev)) > config_.outlierThreshold;
}

std::vector<double> DataHandler::calculateVolatilityStats(const std::vector<Quote>& quotes) const {
    if (quotes.empty()) return {0.0, 0.0};
    
    double sum = 0.0;
    for (const auto& q : quotes) {
        sum += q.iv;
    }
    double mean = sum / quotes.size();
    
    double sumSq = 0.0;
    for (const auto& q : quotes) {
        double diff = q.iv - mean;
        sumSq += diff * diff;
    }
    double variance = sumSq / quotes.size();
    double stdDev = std::sqrt(variance);
    
    return {mean, stdDev};
}

bool DataHandler::exportData(const std::vector<Quote>& quotes, const std::string& filePath) const {
    try {
        nlohmann::json j;
        j["quotes"] = nlohmann::json::array();
        
        for (const auto& q : quotes) {
            nlohmann::json quoteJson;
            quoteJson["strike"] = q.strike;
            quoteJson["expiry"] = q.expiry;
            quoteJson["iv"] = q.iv;
            quoteJson["bid"] = q.bid;
            quoteJson["ask"] = q.ask;
            quoteJson["volume"] = q.volume;
            j["quotes"].push_back(quoteJson);
        }
        
        std::ofstream file(filePath);
        file << j.dump(4);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to export data: " << e.what() << std::endl;
        return false;
    }
}

// DataFeed implementations (simplified)
BloombergFeed::BloombergFeed(const std::string& ticker) : ticker_(ticker) {}

bool BloombergFeed::connect() {
    // Placeholder for Bloomberg API connection
    connected_ = true;
    return true;
}

bool BloombergFeed::disconnect() {
    connected_ = false;
    return true;
}

std::vector<Quote> BloombergFeed::getLatestQuotes() {
    // Placeholder implementation
    return {};
}

MarketData BloombergFeed::getLatestMarketData() {
    // Placeholder implementation
    return {};
}

bool BloombergFeed::isConnected() const {
    return connected_;
}

std::string BloombergFeed::getStatus() const {
    return connected_ ? "Connected" : "Disconnected";
}

CSVFeed::CSVFeed(const std::string& filePath) : filePath_(filePath) {}

bool CSVFeed::connect() {
    try {
        DataHandler::Config config;
        config.source = DataSource::CSV_FILE;
        config.filePath = filePath_;
        DataHandler handler{config};
        std::tie(cachedQuotes_, cachedMarketData_) = handler.loadData();
        connected_ = true;
        return true;
    } catch (...) {
        connected_ = false;
        return false;
    }
}

bool CSVFeed::disconnect() {
    connected_ = false;
    return true;
}

std::vector<Quote> CSVFeed::getLatestQuotes() {
    return cachedQuotes_;
}

MarketData CSVFeed::getLatestMarketData() {
    return cachedMarketData_;
}

bool CSVFeed::isConnected() const {
    return connected_;
}

std::string CSVFeed::getStatus() const {
    return connected_ ? "Connected" : "Disconnected";
}
