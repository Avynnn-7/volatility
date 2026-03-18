#pragma once
#include "vol_surface.hpp"
#include <vector>
#include <string>
#include <memory>
#include <map>

// Data source types
enum class DataSource {
    JSON_FILE,
    CSV_FILE,
    BLOOMBERG,
    REUTERS,
    CUSTOM_API
};

// Data quality metrics
struct DataQualityMetrics {
    int totalQuotes = 0;
    int validQuotes = 0;
    int duplicateQuotes = 0;
    int outlierQuotes = 0;
    int missingQuotes = 0;
    double completenessRatio = 0.0;
    double consistencyScore = 0.0;
    std::vector<std::string> validationErrors;
    
    double getOverallQuality() const;
    bool isAcceptable() const;
};

// Enhanced data loader with validation
class DataHandler {
public:
    // Configuration for data loading
    struct Config {
        DataSource source = DataSource::JSON_FILE;
        std::string filePath;
        std::map<std::string, std::string> apiCredentials;
        double outlierThreshold = 3.0;        // Standard deviations
        double minVol = 0.01;                 // 1% minimum vol
        double maxVol = 3.0;                  // 300% maximum vol
        double minTimeToExpiry = 0.001;         // ~9 hours minimum
        double maxTimeToExpiry = 10.0;          // 10 years maximum
        bool enableDuplicateRemoval = true;
        bool enableOutlierDetection = true;
        bool enableDataCleaning = true;
        bool requireBidAsk = false;              // Require bid/ask prices
        double minSpread = 0.001;              // Minimum bid-ask spread
    };
    
    explicit DataHandler(const Config& config = Config{});
    
    // Load and validate market data
    std::pair<std::vector<Quote>, MarketData> loadData();
    
    // Validate individual quote
    bool validateQuote(const Quote& quote, std::string& errorMessage) const;
    
    // Clean and preprocess data
    std::vector<Quote> cleanData(const std::vector<Quote>& rawQuotes) const;
    
    // Get data quality metrics
    const DataQualityMetrics& getQualityMetrics() const { return qualityMetrics_; }
    
    // Export cleaned data
    bool exportData(const std::vector<Quote>& quotes, const std::string& filePath) const;

private:
    Config config_;
    DataQualityMetrics qualityMetrics_;
    
    // Specific loaders
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
    
    // Helper methods
    double calculateZScore(double value, double mean, double stdDev) const;
    bool isOutlier(double value, double mean, double stdDev) const;
    std::vector<double> calculateVolatilityStats(const std::vector<Quote>& quotes) const;
};

// Real-time data feed interface
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

// Bloomberg data feed implementation (placeholder)
class BloombergFeed : public DataFeed {
public:
    BloombergFeed(const std::string& ticker);
    bool connect() override;
    bool disconnect() override;
    std::vector<Quote> getLatestQuotes() override;
    MarketData getLatestMarketData() override;
    bool isConnected() const override;
    std::string getStatus() const override;

private:
    std::string ticker_;
    bool connected_ = false;
    // Bloomberg API connection would go here
};

// CSV data feed implementation
class CSVFeed : public DataFeed {
public:
    CSVFeed(const std::string& filePath);
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
