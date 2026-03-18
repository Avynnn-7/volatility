#include "vol_api.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <algorithm>
#include <execution>

// ArbitrageCheckResponse implementation
std::string ArbitrageCheckResponse::toJson() const {
    nlohmann::json j;
    
    j["arbitrage_free"] = arbitrageFree;
    j["quality_score"] = qualityScore;
    j["surface_type"] = surfaceType;
    j["detection_time"] = detectionTime;
    j["correction_time"] = correctionTime;
    
    // Serialize violations
    j["violations"] = nlohmann::json::array();
    for (const auto& v : violations) {
        nlohmann::json violation;
        violation["type"] = static_cast<int>(v.type);
        violation["strike"] = v.strike;
        violation["expiry"] = v.expiry;
        violation["magnitude"] = v.magnitude;
        violation["threshold"] = v.threshold;
        violation["description"] = v.description;
        violation["severity"] = v.severityScore();
        violation["critical"] = v.isCritical();
        j["violations"].push_back(violation);
    }
    
    // Serialize QP result
    j["qp_result"]["success"] = qpResult.success;
    j["qp_result"]["objective_value"] = qpResult.objectiveValue;
    j["qp_result"]["regularization_penalty"] = qpResult.regularizationPenalty;
    j["qp_result"]["iterations"] = qpResult.iterations;
    j["qp_result"]["status"] = qpResult.status;
    j["qp_result"]["solve_time"] = qpResult.solveTime;
    
    return j.dump(4);
}

// CachedVolSurface implementation
CachedVolSurface::CachedVolSurface(const std::vector<Quote>& quotes, const MarketData& marketData) {
    surface_ = std::make_unique<VolSurface>(quotes, marketData);
}

double CachedVolSurface::impliedVol(double strike, double expiry) const {
    CacheKey key{strike, expiry};
    
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        cacheHits_++;
        return it->second;
    }
    
    cacheMisses_++;
    double iv = surface_->impliedVol(strike, expiry);
    
    // Add to cache (with size management)
    if (cache_.size() >= maxCacheSize_) {
        // Simple LRU: remove oldest entry
        cache_.erase(cache_.begin());
    }
    cache_[key] = iv;
    
    return iv;
}

void CachedVolSurface::clearCache() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    cache_.clear();
    cacheHits_ = 0;
    cacheMisses_ = 0;
}

size_t CachedVolSurface::getCacheSize() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return cache_.size();
}

void CachedVolSurface::setCacheSize(size_t maxSize) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    maxCacheSize_ = maxSize;
    
    // Trim cache if necessary
    while (cache_.size() > maxCacheSize_) {
        cache_.erase(cache_.begin());
    }
}

double CachedVolSurface::getCacheHitRate() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    size_t total = cacheHits_ + cacheMisses_;
    return total > 0 ? static_cast<double>(cacheHits_) / total : 0.0;
}

void CachedVolSurface::resetPerformanceMetrics() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    cacheHits_ = 0;
    cacheMisses_ = 0;
}

// ThreadSafeArbitrageDetector implementation
ThreadSafeArbitrageDetector::ThreadSafeArbitrageDetector(const VolSurface& surface) {
    detector_ = std::make_unique<ArbitrageDetector>(surface);
}

std::vector<ArbViolation> ThreadSafeArbitrageDetector::detect() const {
    std::lock_guard<std::mutex> lock(detectorMutex_);
    return detector_->detect();
}

std::vector<std::vector<ArbViolation>> ThreadSafeArbitrageDetector::detectBatch(const std::vector<VolSurface>& surfaces) const {
    std::vector<std::vector<ArbViolation>> results;
    results.reserve(surfaces.size());
    
    // Process in parallel if available
    #if defined(__cpp_lib_execution_policies)
    std::for_each(std::execution::par_unseq, surfaces.begin(), surfaces.end(),
        [&results](const VolSurface& surface) {
            ArbitrageDetector detector(surface);
            results.push_back(detector.detect());
        });
    #else
    for (const auto& surface : surfaces) {
        ArbitrageDetector detector(surface);
        results.push_back(detector.detect());
    }
    #endif
    
    return results;
}

// VolatilityArbitrageAPI implementation
VolatilityArbitrageAPI& VolatilityArbitrageAPI::getInstance() {
    static VolatilityArbitrageAPI instance;
    return instance;
}

ApiResponse VolatilityArbitrageAPI::checkArbitrage(const ArbitrageCheckRequest& request) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    try {
        auto response = processRequestInternal(request);
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        return ApiResponse(true, "Arbitrage check completed", response.toJson(), 
                          duration.count() / 1000.0);
    } catch (const std::exception& e) {
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        return ApiResponse(false, "Error: " + std::string(e.what()), "", 
                          duration.count() / 1000.0);
    }
}

ApiResponse VolatilityArbitrageAPI::correctSurface(const ArbitrageCheckRequest& request) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    try {
        auto response = processRequestInternal(request);
        
        // Ensure QP correction was performed
        if (!response.qpResult.success) {
            return ApiResponse(false, "QP correction failed", response.toJson(), 0.0);
        }
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        return ApiResponse(true, "Surface correction completed", response.toJson(), 
                          duration.count() / 1000.0);
    } catch (const std::exception& e) {
        return ApiResponse(false, "Error: " + std::string(e.what()), "", 0.0);
    }
}

ApiResponse VolatilityArbitrageAPI::analyzeQuality(const ArbitrageCheckRequest& request) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    try {
        auto response = processRequestInternal(request);
        
        nlohmann::json analysis;
        analysis["quality_score"] = response.qualityScore;
        analysis["arbitrage_free"] = response.arbitrageFree;
        analysis["violation_count"] = response.violations.size();
        analysis["critical_violations"] = std::count_if(response.violations.begin(), 
                                                        response.violations.end(),
                                                        [](const ArbViolation& v) { return v.isCritical(); });
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        return ApiResponse(true, "Quality analysis completed", analysis.dump(4), 
                          duration.count() / 1000.0);
    } catch (const std::exception& e) {
        return ApiResponse(false, "Error: " + std::string(e.what()), "", 0.0);
    }
}

ArbitrageCheckResponse VolatilityArbitrageAPI::processRequestInternal(const ArbitrageCheckRequest& request) {
    if (request.interpolationMethod == "svi") {
        return processWithSVI(request);
    } else {
        return processWithBilinear(request);
    }
}

ArbitrageCheckResponse VolatilityArbitrageAPI::processWithBilinear(const ArbitrageCheckRequest& request) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Create surface
    VolSurface surface(request.quotes, request.marketData);
    
    // Detect arbitrage
    ArbitrageDetector detector(surface);
    detector.setConfig(request.arbConfig);
    
    auto detectStartTime = std::chrono::high_resolution_clock::now();
    auto violations = detector.detect();
    auto detectEndTime = std::chrono::high_resolution_clock::now();
    
    double detectionTime = std::chrono::duration<double>(detectEndTime - detectStartTime).count();
    
    ArbitrageCheckResponse response;
    response.arbitrageFree = violations.empty();
    response.qualityScore = detector.getQualityScore();
    response.violations = violations;
    response.surfaceType = "bilinear";
    response.detectionTime = detectionTime;
    
    // QP correction if requested
    if (request.enableQPCorrection && !violations.empty()) {
        auto correctStartTime = std::chrono::high_resolution_clock::now();
        
        QPSolver solver(surface, request.qpConfig);
        response.qpResult = solver.solve();
        
        auto correctEndTime = std::chrono::high_resolution_clock::now();
        response.correctionTime = std::chrono::duration<double>(correctEndTime - correctStartTime).count();
        
        if (response.qpResult.success) {
            auto correctedSurface = solver.buildCorrectedSurface(response.qpResult);
            ArbitrageDetector correctedDetector(correctedSurface);
            auto correctedViolations = correctedDetector.detect();
            
            response.arbitrageFree = correctedViolations.empty();
            response.qualityScore = correctedDetector.getQualityScore();
        }
    } else {
        response.correctionTime = 0.0;
        response.qpResult = QPResult{false, {}, 0.0, 0.0, 0, "skipped", 0.0};
    }
    
    return response;
}

ArbitrageCheckResponse VolatilityArbitrageAPI::processWithSVI(const ArbitrageCheckRequest& request) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Create SVI surface
    SVISurface sviSurface(request.quotes, request.marketData);
    
    // Check for arbitrage in SVI parameters
    bool sviArbFree = sviSurface.isArbitrageFree();
    auto sviViolations = sviSurface.getArbitrageViolations();
    
    // Convert SVI to regular surface for detection
    VolSurface surface(request.quotes, request.marketData);
    ArbitrageDetector detector(surface);
    detector.setConfig(request.arbConfig);
    
    auto detectStartTime = std::chrono::high_resolution_clock::now();
    auto violations = detector.detect();
    auto detectEndTime = std::chrono::high_resolution_clock::now();
    
    double detectionTime = std::chrono::duration<double>(detectEndTime - detectStartTime).count();
    
    ArbitrageCheckResponse response;
    response.arbitrageFree = violations.empty() && sviArbFree;
    response.qualityScore = detector.getQualityScore();
    response.violations = violations;
    response.surfaceType = "svi";
    response.detectionTime = detectionTime;
    
    // Add SVI violations to the list
    for (const auto& sviViol : sviViolations) {
        ArbViolation arbViol;
        arbViol.type = ArbType::ExtremeValueViolation; // Map to appropriate type
        arbViol.description = sviViol;
        arbViol.magnitude = 0.0;
        arbViol.threshold = 0.0;
        response.violations.push_back(arbViol);
    }
    
    // QP correction if requested
    if (request.enableQPCorrection && !violations.empty()) {
        auto correctStartTime = std::chrono::high_resolution_clock::now();
        
        QPSolver solver(surface, request.qpConfig);
        response.qpResult = solver.solve();
        
        auto correctEndTime = std::chrono::high_resolution_clock::now();
        response.correctionTime = std::chrono::duration<double>(correctEndTime - correctStartTime).count();
        
        if (response.qpResult.success) {
            auto correctedSurface = solver.buildCorrectedSurface(response.qpResult);
            ArbitrageDetector correctedDetector(correctedSurface);
            auto correctedViolations = correctedDetector.detect();
            
            response.arbitrageFree = correctedViolations.empty();
            response.qualityScore = correctedDetector.getQualityScore();
        }
    } else {
        response.correctionTime = 0.0;
        response.qpResult = QPResult{false, {}, 0.0, 0.0, 0, "skipped", 0.0};
    }
    
    return response;
}

bool VolatilityArbitrageAPI::healthCheck() {
    try {
        // Simple health check - create a small test surface
        auto marketData = MockDataGenerator::generateMarketData(100.0);
        auto quotes = MockDataGenerator::generateArbitrageFreeQuotes(5, 100.0);
        
        VolSurface surface(quotes, marketData);
        ArbitrageDetector detector(surface);
        auto violations = detector.detect();
        
        return true;
    } catch (...) {
        return false;
    }
}

std::string VolatilityArbitrageAPI::getVersion() {
    return "2.0.0-enhanced";
}

// RestAPIHandler implementation
std::string RestAPIHandler::handlePostArbitrageCheck(const std::string& requestBody) {
    try {
        auto request = parseArbitrageRequest(requestBody);
        auto response = api_.checkArbitrage(request);
        return serializeResponse(response);
    } catch (const std::exception& e) {
        return createErrorResponse("Invalid request: " + std::string(e.what()));
    }
}

std::string RestAPIHandler::handleGetStatus() {
    try {
        bool healthy = api_.healthCheck();
        std::string version = api_.getVersion();
        
        nlohmann::json status;
        status["healthy"] = healthy;
        status["version"] = version;
        status["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        return createSuccessResponse(status.dump());
    } catch (const std::exception& e) {
        return createErrorResponse("Status check failed: " + std::string(e.what()));
    }
}

std::string RestAPIHandler::createErrorResponse(const std::string& error, int httpCode) {
    nlohmann::json response;
    response["success"] = false;
    response["error"] = error;
    response["http_code"] = httpCode;
    
    return response.dump();
}

std::string RestAPIHandler::createSuccessResponse(const std::string& data) {
    nlohmann::json response;
    response["success"] = true;
    response["data"] = nlohmann::json::parse(data);
    
    return response.dump();
}

ArbitrageCheckRequest RestAPIHandler::parseArbitrageRequest(const std::string& json) {
    auto j = nlohmann::json::parse(json);
    
    ArbitrageCheckRequest request;
    
    // Parse market data
    request.marketData.spot = j["market_data"]["spot"];
    request.marketData.riskFreeRate = j.value("market_data", "risk_free_rate", 0.05);
    request.marketData.dividendYield = j.value("market_data", "dividend_yield", 0.0);
    request.marketData.valuationDate = j.value("market_data", "valuation_date", "2024-01-01");
    request.marketData.currency = j.value("market_data", "currency", "USD");
    
    // Parse quotes
    for (const auto& q : j["quotes"]) {
        Quote quote;
        quote.strike = q["strike"];
        quote.expiry = q["expiry"];
        quote.iv = q["iv"];
        quote.bid = q.value("bid", 0.0);
        quote.ask = q.value("ask", 0.0);
        quote.volume = q.value("volume", 0.0);
        request.quotes.push_back(quote);
    }
    
    // Parse options
    request.interpolationMethod = j.value("interpolation_method", "bilinear");
    request.enableQPCorrection = j.value("enable_qp_correction", true);
    
    return request;
}

std::string RestAPIHandler::serializeResponse(const ApiResponse& response) {
    nlohmann::json j;
    j["success"] = response.success;
    j["message"] = response.message;
    j["execution_time"] = response.executionTime;
    
    if (!response.data.empty()) {
        j["data"] = nlohmann::json::parse(response.data);
    }
    
    return j.dump();
}
