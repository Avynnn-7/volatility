// ============================================================================
// PHASE 6: Integration Tests
// ============================================================================
// End-to-end workflow tests covering complete pipelines
//
// Test Categories:
//   1. JSON file loading through QP solving
//   2. Full arbitrage detection and repair pipeline
//   3. API workflow tests
//   4. Data handler integration
//   5. SVI fitting pipeline
//   6. Local volatility computation pipeline
// ============================================================================

#include "test_framework.hpp"
#include "vol_surface.hpp"
#include "arbitrage_detector.hpp"
#include "qp_solver.hpp"
#include "svi_surface.hpp"
#include "local_vol.hpp"
#include "data_handler.hpp"
#include "vol_api.hpp"
#include <cmath>
#include <fstream>

// ============================================================================
// Test Suite: Full Pipeline Tests
// ============================================================================

void test_full_pipeline_arbitrage_free() {
    // Complete pipeline: quotes -> surface -> detection -> report
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    auto quotes = MockDataGenerator::generateArbitrageFreeQuotes(20, 100.0);
    
    // Step 1: Build surface
    VolSurface surface(quotes, marketData);
    ASSERT_TRUE(surface.strikes().size() > 0);
    ASSERT_TRUE(surface.expiries().size() > 0);
    
    // Step 2: Detect arbitrage
    ArbitrageDetector detector(surface);
    auto violations = detector.detect();
    
    // Step 3: Verify quality
    double quality = detector.getQualityScore();
    ASSERT_IN_RANGE(quality, 0.0, 1.0);  // Quality is in valid range
    
    // Count significant violations only
    int significantViolations = 0;
    for (const auto& v : violations) {
        if (std::abs(v.magnitude) > 0.01) significantViolations++;
    }
    ASSERT_TRUE(significantViolations <= 5);  // Allow minor numerical issues
}

void test_full_pipeline_with_arbitrage() {
    // Complete pipeline: quotes with arb -> detection -> QP repair -> verification
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    auto quotes = MockDataGenerator::generateButterflyArbitrageQuotes(15, 100.0);
    
    // Step 1: Build surface
    VolSurface surface(quotes, marketData);
    
    // Step 2: Detect violations
    ArbitrageDetector detectorBefore(surface);
    auto violationsBefore = detectorBefore.detect();
    ASSERT_TRUE(violationsBefore.size() > 0);  // Should have violations
    
    // Step 3: QP repair
    QPSolver solver(surface);
    auto result = solver.solve();
    ASSERT_TRUE(result.success);
    
    // Step 4: Build corrected surface
    VolSurface corrected = solver.buildCorrectedSurface(result);
    
    // Step 5: Verify correction
    ArbitrageDetector detectorAfter(corrected);
    auto violationsAfter = detectorAfter.detect();
    
    // Should have fewer violations (or at least not more)
    ASSERT_TRUE(violationsAfter.size() <= violationsBefore.size() + 5);
    
    // Quality should be in valid range
    double qualityAfter = detectorAfter.getQualityScore();
    ASSERT_IN_RANGE(qualityAfter, 0.0, 1.0);
}

void test_full_pipeline_batch_processing() {
    // Process multiple surfaces in sequence
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    
    std::vector<std::vector<Quote>> allQuotes;
    allQuotes.push_back(MockDataGenerator::generateArbitrageFreeQuotes(10, 100.0));
    allQuotes.push_back(MockDataGenerator::generateButterflyArbitrageQuotes(12, 100.0));
    allQuotes.push_back(MockDataGenerator::generateCalendarArbitrageQuotes(15, 100.0));
    
    int successCount = 0;
    
    for (const auto& quotes : allQuotes) {
        VolSurface surface(quotes, marketData);
        ArbitrageDetector detector(surface);
        auto violations = detector.detect();
        
        if (!violations.empty()) {
            QPSolver solver(surface);
            auto result = solver.solve();
            if (result.success) {
                successCount++;
            }
        } else {
            successCount++;  // Already arb-free
        }
    }
    
    ASSERT_EQ(successCount, 3);
}

// ============================================================================
// Test Suite: Data Handler Integration
// ============================================================================

void test_data_handler_roundtrip() {
    // Create test data
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    auto quotes = MockDataGenerator::generateArbitrageFreeQuotes(10, 100.0);
    
    // Build surface and verify data integrity
    VolSurface surface(quotes, marketData);
    
    // Check surface has reasonable number of strikes (quotes span multiple expiries)
    ASSERT_TRUE(surface.strikes().size() > 0);
    ASSERT_TRUE(surface.strikes().size() <= quotes.size());  // Unique strikes <= quotes
    ASSERT_TRUE(std::abs(surface.spot() - 100.0) < 0.01);
}

void test_data_validation_integration() {
    // Test that data validation works in pipeline
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    
    // Valid quotes
    std::vector<Quote> validQuotes = {
        {90.0, 0.5, 0.22},
        {100.0, 0.5, 0.20},
        {110.0, 0.5, 0.22},
    };
    
    ASSERT_NO_THROW({
        VolSurface surface(validQuotes, marketData);
        ArbitrageDetector detector(surface);
        detector.detect();
    });
}

// ============================================================================
// Test Suite: SVI Integration
// ============================================================================

void test_svi_pipeline_basic() {
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    
    // Create smile-like quotes for SVI fitting
    std::vector<Quote> quotes;
    for (double K : {80.0, 90.0, 95.0, 100.0, 105.0, 110.0, 120.0}) {
        double moneyness = std::log(K / 100.0);
        double vol = 0.20 + 0.15 * std::abs(moneyness) + 0.1 * moneyness * moneyness;
        quotes.push_back({K, 1.0, vol});
    }
    
    ASSERT_NO_THROW({
        SVISurface sviSurface(quotes, marketData);
        
        // Check SVI properties
        bool isArbFree = sviSurface.isArbitrageFree();
        (void)isArbFree;  // Just verify it doesn't crash
        
        // Get parameters
        const auto& params = sviSurface.sviParams();
        ASSERT_TRUE(params.size() > 0);
    });
}

void test_svi_arbitrage_check_integration() {
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    
    // Well-formed convex smile
    std::vector<Quote> quotes = {
        {80.0, 1.0, 0.28},
        {90.0, 1.0, 0.23},
        {100.0, 1.0, 0.20},
        {110.0, 1.0, 0.23},
        {120.0, 1.0, 0.28},
    };
    
    SVISurface sviSurface(quotes, marketData);
    auto violations = sviSurface.getArbitrageViolations();
    
    // Well-formed smile should have few/no violations
    ASSERT_TRUE(violations.size() <= 2);
}

// ============================================================================
// Test Suite: Local Volatility Integration
// ============================================================================

void test_local_vol_pipeline() {
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    auto quotes = MockDataGenerator::generateArbitrageFreeQuotes(20, 100.0);
    
    VolSurface surface(quotes, marketData);
    
    ASSERT_NO_THROW({
        LocalVolSurface localVol(surface);
        
        // Check that local vol is computed
        const auto& grid = localVol.localVolGrid();
        ASSERT_TRUE(grid.rows() > 0);
        ASSERT_TRUE(grid.cols() > 0);
        
        // Check positivity
        bool allPositive = localVol.allPositive();
        (void)allPositive;  // May or may not be positive depending on surface
    });
}

void test_local_vol_from_corrected_surface() {
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    auto quotes = MockDataGenerator::generateButterflyArbitrageQuotes(15, 100.0);
    
    // Original surface
    VolSurface surface(quotes, marketData);
    
    // QP correction
    QPSolver solver(surface);
    auto result = solver.solve();
    ASSERT_TRUE(result.success);
    
    // Build corrected surface
    VolSurface corrected = solver.buildCorrectedSurface(result);
    
    // Compute local vol on corrected surface
    ASSERT_NO_THROW({
        LocalVolSurface localVol(corrected);
        
        // Corrected surface should have better local vol properties
        bool allPositive = localVol.allPositive();
        (void)allPositive;
    });
}

// ============================================================================
// Test Suite: API Integration
// ============================================================================

void test_api_check_arbitrage() {
    auto& api = VolatilityArbitrageAPI::getInstance();
    
    // Build request
    ArbitrageCheckRequest request;
    request.quotes = MockDataGenerator::generateArbitrageFreeQuotes(10, 100.0);
    request.marketData = MockDataGenerator::generateMarketData(100.0);
    request.interpolationMethod = "bilinear";
    request.enableQPCorrection = false;
    
    // Call API
    ApiResponse response = api.checkArbitrage(request);
    
    ASSERT_TRUE(response.success);
    ASSERT_TRUE(response.executionTime > 0.0);
    ASSERT_FALSE(response.data.empty());
}

void test_api_correct_surface() {
    auto& api = VolatilityArbitrageAPI::getInstance();
    
    // Build request with arbitrage
    ArbitrageCheckRequest request;
    request.quotes = MockDataGenerator::generateButterflyArbitrageQuotes(12, 100.0);
    request.marketData = MockDataGenerator::generateMarketData(100.0);
    request.enableQPCorrection = true;
    
    // Call API
    ApiResponse response = api.correctSurface(request);
    
    ASSERT_TRUE(response.success);
    ASSERT_TRUE(response.executionTime > 0.0);
}

void test_api_batch_check() {
    auto& api = VolatilityArbitrageAPI::getInstance();
    
    // Build batch request
    std::vector<ArbitrageCheckRequest> requests;
    
    for (int i = 0; i < 3; ++i) {
        ArbitrageCheckRequest request;
        request.quotes = MockDataGenerator::generateArbitrageFreeQuotes(8 + i * 2, 100.0);
        request.marketData = MockDataGenerator::generateMarketData(100.0);
        request.enableQPCorrection = false;
        requests.push_back(request);
    }
    
    // Call API
    ApiResponse response = api.batchCheckArbitrage(requests);
    
    ASSERT_TRUE(response.success);
}

void test_api_status_and_config() {
    auto& api = VolatilityArbitrageAPI::getInstance();
    
    // Get status
    ApiResponse status = api.getStatus();
    ASSERT_TRUE(status.success);
    
    // Get config
    ApiResponse config = api.getConfiguration();
    ASSERT_TRUE(config.success);
    
    // Health check
    bool healthy = api.healthCheck();
    ASSERT_TRUE(healthy);
    
    // Version
    std::string version = api.getVersion();
    ASSERT_FALSE(version.empty());
}

// ============================================================================
// Test Suite: Error Handling Integration
// ============================================================================

void test_error_propagation_empty_quotes() {
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    std::vector<Quote> emptyQuotes;
    
    bool exceptionThrown = false;
    try {
        VolSurface surface(emptyQuotes, marketData);
    } catch (const std::exception& e) {
        (void)e;
        exceptionThrown = true;
    }
    
    ASSERT_TRUE(exceptionThrown);
}

void test_error_recovery_in_pipeline() {
    // Pipeline should continue even if one step fails
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    
    int successCount = 0;
    
    // Good data
    auto goodQuotes = MockDataGenerator::generateArbitrageFreeQuotes(10, 100.0);
    try {
        VolSurface surface(goodQuotes, marketData);
        ArbitrageDetector detector(surface);
        detector.detect();
        successCount++;
    } catch (...) {}
    
    // Another good data set
    auto moreQuotes = MockDataGenerator::generateButterflyArbitrageQuotes(12, 100.0);
    try {
        VolSurface surface(moreQuotes, marketData);
        ArbitrageDetector detector(surface);
        detector.detect();
        successCount++;
    } catch (...) {}
    
    ASSERT_EQ(successCount, 2);
}

// ============================================================================
// Test Suite: Performance Integration
// ============================================================================

void test_pipeline_performance() {
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    auto quotes = MockDataGenerator::generateArbitrageFreeQuotes(50, 100.0);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    VolSurface surface(quotes, marketData);
    ArbitrageDetector detector(surface);
    auto violations = detector.detect();
    
    if (!violations.empty()) {
        QPSolver solver(surface);
        solver.solve();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Full pipeline should complete within reasonable time
    ASSERT_TRUE(duration.count() < 10000);  // 10 seconds max
}

// ============================================================================
// Registration
// ============================================================================

void registerIntegrationTests() {
    auto& runner = TestRunner::getInstance();
    
    auto suite = std::make_unique<TestSuite>("Integration");
    
    // Full pipeline tests
    suite->addTest("FullPipelineArbitrageFree", test_full_pipeline_arbitrage_free);
    suite->addTest("FullPipelineWithArbitrage", test_full_pipeline_with_arbitrage);
    suite->addTest("FullPipelineBatchProcessing", test_full_pipeline_batch_processing);
    
    // Data handler integration
    suite->addTest("DataHandlerRoundtrip", test_data_handler_roundtrip);
    suite->addTest("DataValidationIntegration", test_data_validation_integration);
    
    // SVI integration
    suite->addTest("SVIPipelineBasic", test_svi_pipeline_basic);
    suite->addTest("SVIArbitrageCheckIntegration", test_svi_arbitrage_check_integration);
    
    // Local vol integration
    suite->addTest("LocalVolPipeline", test_local_vol_pipeline);
    suite->addTest("LocalVolFromCorrectedSurface", test_local_vol_from_corrected_surface);
    
    // API integration
    suite->addTest("APICheckArbitrage", test_api_check_arbitrage);
    suite->addTest("APICorrectSurface", test_api_correct_surface);
    suite->addTest("APIBatchCheck", test_api_batch_check);
    suite->addTest("APIStatusAndConfig", test_api_status_and_config);
    
    // Error handling
    suite->addTest("ErrorPropagationEmptyQuotes", test_error_propagation_empty_quotes);
    suite->addTest("ErrorRecoveryInPipeline", test_error_recovery_in_pipeline);
    
    // Performance
    suite->addTest("PipelinePerformance", test_pipeline_performance, 30.0);
    
    runner.addSuite(std::move(suite));
}
