/**
 * @file data_handler.hpp
 * @brief Market data loading, validation, and preprocessing
 * @author vol_arb Team
 * @version 2.0
 * @date 2024
 *
 * Provides comprehensive data handling including:
 * - Multiple data sources (JSON, CSV, API feeds)
 * - Data quality validation and metrics
 * - Outlier detection and removal
 * - Missing data interpolation
 *
 * @see VolSurface for surface construction from cleaned data
 */

#pragma once
#include "vol_surface.hpp"
#include <vector>
#include <string>
#include <memory>
#include <map>

/**
 * @brief Supported data source types
 */
enum class DataSource {
    JSON_FILE,   ///< JSON file format
    CSV_FILE,    ///< CSV file format
    BLOOMBERG,   ///< Bloomberg API (placeholder)
    REUTERS,     ///< Reuters API (placeholder)
    CUSTOM_API   ///< Custom API integration
};

/**
 * @brief Data quality assessment metrics
 *
 * Tracks data quality through the loading and cleaning process.
 */
struct DataQualityMetrics {
    int totalQuotes = 0;         ///< Total quotes loaded
    int validQuotes = 0;         ///< Quotes passing validation
    int duplicateQuotes = 0;     ///< Duplicate quotes removed
    int rejectedQuotes = 0;      ///< Total rejected (invalid + outliers)
    int outlierQuotes = 0;       ///< Outliers removed
    int missingQuotes = 0;       ///< Missing data points filled
    double completenessRatio = 0.0; ///< Valid / Total ratio
    double consistencyScore = 0.0;  ///< Consistency metric [0,1]
    std::vector<std::string> validationErrors; ///< Specific error messages
    
    /**
     * @brief Calculate overall quality score
     * @return Quality score in [0,1]
     */
    double getOverallQuality() const;
    
    /**
     * @brief Check if data quality is acceptable
     * @return True if quality > 0.8 and no critical errors
     */
    bool isAcceptable() const;
};

/**
 * @brief Enhanced data loader with validation
 *
 * Handles loading market data from various sources with comprehensive
 * validation and quality assessment.
 *
 * ## Example Usage
 * @code
 * DataHandler::Config config;
 * config.source = DataSource::JSON_FILE;
 * config.filePath = "market_data.json";
 * config.outlierThreshold = 3.0;
 * config.enableOutlierDetection = true;
 *
 * DataHandler handler(config);
 * auto [quotes, marketData] = handler.loadData();
 *
 * if (handler.getQualityMetrics().isAcceptable()) {
 *     VolSurface surface(quotes, marketData);
 * }
 * @endcode
 */
class DataHandler {
public:
    /**
     * @brief Data handler configuration
     */
    struct Config {
        DataSource source = DataSource::JSON_FILE;
        std::string filePath;                    ///< File path for file sources
        std::map<std::string, std::string> apiCredentials; ///< API credentials
        double outlierThreshold = 3.0;           ///< Z-score threshold
        double minVol = 0.01;                    ///< Minimum valid IV (1%)
        double maxVol = 3.0;                     ///< Maximum valid IV (300%)
        double minTimeToExpiry = 0.001;          ///< Min expiry (~9 hours)
        double maxTimeToExpiry = 10.0;           ///< Max expiry (10 years)
        bool enableDuplicateRemoval = true;      ///< Remove duplicates
        bool enableOutlierDetection = true;      ///< Detect outliers
        bool enableDataCleaning = true;          ///< Clean data
        bool requireBidAsk = false;              ///< Require bid/ask prices
        double minSpread = 0.001;                ///< Min bid-ask spread
    };
    
    /**
     * @brief Construct handler with configuration
     */
    explicit DataHandler(const Config& config = Config{});
    
    /**
     * @brief Load and validate market data
     * @return Pair of (quotes, marketData)
     * @throws std::runtime_error on critical data errors
     *
     * Loads data from configured source, validates, and cleans.
     */
    std::pair<std::vector<Quote>, MarketData> loadData();
    
    /**
     * @brief Validate individual quote
     * @param quote Quote to validate
     * @param[out] errorMessage Description of failure (if any)
     * @return True if quote is valid
     */
    bool validateQuote(const Quote& quote, std::string& errorMessage) const;
    
    /**
     * @brief Clean and preprocess quotes
     * @param rawQuotes Uncleaned quotes
     * @return Cleaned quotes
     *
     * Removes duplicates, outliers, and fills missing data.
     */
    std::vector<Quote> cleanData(const std::vector<Quote>& rawQuotes) const;
    
    /**
     * @brief Get quality metrics from last load
     */
    const DataQualityMetrics& getQualityMetrics() const { return qualityMetrics_; }
    
    /**
     * @brief Export cleaned data to file
     * @param quotes Quotes to export
     * @param filePath Output file path
     * @return True on success
     */
    bool exportData(const std::vector<Quote>& quotes, const std::string& filePath) const;

private:
    Config config_;
    mutable DataQualityMetrics qualityMetrics_;
    
    // Source-specific loaders
    std::pair<std::vector<Quote>, MarketData> loadFromJSON() const;
    std::pair<std::vector<Quote>, MarketData> loadFromCSV() const;
    std::pair<std::vector<Quote>, MarketData> loadFromAPI() const;
    
    // Validation methods
    bool validateMarketData(const MarketData& marketData, std::string& errorMessage) const;
    void detectOutliers(std::vector<Quote>& quotes) const;
    void removeDuplicates(std::vector<Quote>& quotes) const;
    void fillMissingData(std::vector<Quote>& quotes) const;
    
    // Quality assessment
    void calculateQualityMetrics(const std::vector<Quote>& original, 
                                 const std::vector<Quote>& cleaned);
    
    // Statistics helpers
    double calculateZScore(double value, double mean, double stdDev) const;
    bool isOutlier(double value, double mean, double stdDev) const;
    std::vector<double> calculateVolatilityStats(const std::vector<Quote>& quotes) const;
};

/**
 * @brief Abstract interface for real-time data feeds
 */
class DataFeed {
public:
    virtual ~DataFeed() = default;
    virtual bool connect() = 0;
    virtual bool disconnect() = 0;
    virtual std::vector<Quote> getLatestQuotes() = 0;
    virtual MarketData getLatestMarketData() = 0;
    virtual bool isConnected() const = 0;
    virtual std::string getStatus() const = 0;
};

/**
 * @brief Bloomberg data feed (placeholder implementation)
 */
class BloombergFeed : public DataFeed {
public:
    explicit BloombergFeed(const std::string& ticker);
    bool connect() override;
    bool disconnect() override;
    std::vector<Quote> getLatestQuotes() override;
    MarketData getLatestMarketData() override;
    bool isConnected() const override;
    std::string getStatus() const override;

private:
    std::string ticker_;
    bool connected_ = false;
};

/**
 * @brief CSV file-based data feed
 */
class CSVFeed : public DataFeed {
public:
    explicit CSVFeed(const std::string& filePath);
    bool connect() override;
    bool disconnect() override;
    std::vector<Quote> getLatestQuotes() override;
    MarketData getLatestMarketData() override;
    bool isConnected() const override;
    std::string getStatus() const override;

private:
    std::string filePath_;
    bool connected_ = false;
    std::vector<Quote> cachedQuotes_;
    MarketData cachedMarketData_;
};
