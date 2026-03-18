#pragma once
#include "vol_surface.hpp"
#include "arbitrage_detector.hpp"
#include "qp_solver.hpp"
#include "data_handler.hpp"
#include "svi_surface.hpp"
#include "logger.hpp"
#include "config_manager.hpp"
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <future>
#include <mutex>

// API response structures
struct ApiResponse {
    bool success;
    std::string message;
    std::string data;  // JSON string
    double executionTime;
    
    ApiResponse(bool s = false, const std::string& msg = "", const std::string& d = "", double t = 0.0)
        : success(s), message(msg), data(d), executionTime(t) {}
};

// Request parameters
struct ArbitrageCheckRequest {
    std::vector<Quote> quotes;
    MarketData marketData;
    std::string interpolationMethod = "bilinear"; // "bilinear" or "svi"
    bool enableQPCorrection = true;
    
    // QP solver configuration
    QPSolver::Config qpConfig;
    
    // Arbitrage detection configuration
    ArbitrageDetector::Config arbConfig;
};

struct ArbitrageCheckResponse {
    bool arbitrageFree;
    double qualityScore;
    std::vector<ArbViolation> violations;
    QPResult qpResult;
    double detectionTime;
    double correctionTime;
    std::string surfaceType;
    
    // Convert to JSON
    std::string toJson() const;
};

// Performance-optimized volatility surface with caching
class CachedVolSurface {
public:
    CachedVolSurface(const std::vector<Quote>& quotes, const MarketData& marketData);
    
    // Thread-safe interpolation with caching
    double impliedVol(double strike, double expiry) const;
    
    // Cache management
    void clearCache();
    size_t getCacheSize() const;
    void setCacheSize(size_t maxSize);
    
    // Performance metrics
    double getCacheHitRate() const;
    void resetPerformanceMetrics();

private:
    mutable std::mutex cacheMutex_;
    std::unique_ptr<VolSurface> surface_;
    
    struct CacheKey {
        double strike;
        double expiry;
        
        bool operator<(const CacheKey& other) const {
            if (std::abs(strike - other.strike) > 1e-10) return strike < other.strike;
            return expiry < other.expiry;
        }
    };
    
    mutable std::map<CacheKey, double> cache_;
    mutable size_t cacheHits_ = 0;
    mutable size_t cacheMisses_ = 0;
    size_t maxCacheSize_ = 10000;
};

// Thread-safe arbitrage detector
class ThreadSafeArbitrageDetector {
public:
    explicit ThreadSafeArbitrageDetector(const VolSurface& surface);
    
    // Thread-safe detection
    std::vector<ArbViolation> detect() const;
    
    // Batch processing
    std::vector<std::vector<ArbViolation>> detectBatch(const std::vector<VolSurface>& surfaces) const;

private:
    mutable std::mutex detectorMutex_;
    std::unique_ptr<ArbitrageDetector> detector_;
};

// Main API class
class VolatilityArbitrageAPI {
public:
    static VolatilityArbitrageAPI& getInstance();
    
    // Core functionality
    ApiResponse checkArbitrage(const ArbitrageCheckRequest& request);
    ApiResponse correctSurface(const ArbitrageCheckRequest& request);
    ApiResponse analyzeQuality(const ArbitrageCheckRequest& request);
    
    // Batch operations
    ApiResponse batchCheckArbitrage(const std::vector<ArbitrageCheckRequest>& requests);
    
    // Real-time processing
    void startRealTimeProcessing(std::function<void(const ArbitrageCheckRequest&)> callback);
    void stopRealTimeProcessing();
    
    // Configuration
    ApiResponse updateConfiguration(const std::string& configJson);
    ApiResponse getConfiguration();
    
    // Status and monitoring
    ApiResponse getStatus();
    ApiResponse getPerformanceMetrics();
    
    // Data management
    ApiResponse loadData(const std::string& dataSource);
    ApiResponse exportData(const std::string& format, const std::string& filePath);
    
    // Health check
    bool healthCheck();
    std::string getVersion();

private:
    VolatilityArbitrageAPI() = default;
    
    // Internal processing methods
    ArbitrageCheckResponse processRequestInternal(const ArbitrageCheckRequest& request);
    ArbitrageCheckResponse processWithSVI(const ArbitrageCheckRequest& request);
    ArbitrageCheckResponse processWithBilinear(const ArbitrageCheckRequest& request);
    
    // Performance monitoring
    struct PerformanceMetrics {
        double totalProcessingTime = 0.0;
        int totalRequests = 0;
        int successfulRequests = 0;
        double averageResponseTime = 0.0;
        std::map<std::string, double> operationTimes;
    };
    
    mutable std::mutex metricsMutex_;
    PerformanceMetrics metrics_;
    
    // Real-time processing
    std::thread processingThread_;
    std::atomic<bool> processingActive_{false};
    std::queue<ArbitrageCheckRequest> requestQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCondition_;
    
    // Configuration
    mutable std::mutex configMutex_;
    ArbitrageDetector::Config defaultArbConfig_;
    QPSolver::Config defaultQPConfig_;
};

// REST API interface (placeholder for web framework integration)
class RestAPIHandler {
public:
    // HTTP endpoints
    std::string handlePostArbitrageCheck(const std::string& requestBody);
    std::string handleGetStatus();
    std::string handleGetConfig();
    std::string handlePostConfig(const std::string& requestBody);
    
    // Error handling
    std::string createErrorResponse(const std::string& error, int httpCode = 400);
    std::string createSuccessResponse(const std::string& data);

private:
    VolatilityArbitrageAPI& api_ = VolatilityArbitrageAPI::getInstance();
    
    // JSON parsing helpers
    ArbitrageCheckRequest parseArbitrageRequest(const std::string& json);
    std::string serializeResponse(const ApiResponse& response);
};

// Performance optimization utilities
class PerformanceOptimizer {
public:
    // Parallel processing
    template<typename Func, typename... Args>
    static auto parallelProcess(Func func, Args&&... args) -> std::vector<decltype(func(args...))>;
    
    // Memory pool for frequently allocated objects
    template<typename T>
    class ObjectPool {
    public:
        ObjectPool(size_t initialSize = 100);
        std::unique_ptr<T> acquire();
        void release(std::unique_ptr<T> obj);
        
    private:
        std::vector<std::unique_ptr<T>> pool_;
        std::mutex poolMutex_;
    };
    
    // SIMD-optimized calculations (placeholder for actual implementation)
    static void vectorizedBlackScholes(const double* S, const double* K, const double* T, 
                                      const double* sigma, const double* r, const double* q,
                                      double* prices, size_t n);
};

// Async processing utilities
class AsyncTaskProcessor {
public:
    template<typename Func, typename... Args>
    static std::future<typename std::result_of<Func(Args...)>::type> 
    submitAsync(Func&& func, Args&&... args);
    
    void processBatch(const std::vector<std::function<void()>>& tasks);
    
private:
    static thread_pool pool_; // Would need a thread pool implementation
};
