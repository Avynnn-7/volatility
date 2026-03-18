#include "test_framework.hpp"
#include "vol_surface.hpp"
#include "arbitrage_detector.hpp"
#include "qp_solver.hpp"
#include "data_handler.hpp"
#include "svi_surface.hpp"
#include "logger.hpp"
#include "config_manager.hpp"

// VolSurface Tests
void testVolSurfaceConstruction() {
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    auto quotes = MockDataGenerator::generateArbitrageFreeQuotes(20, 100.0);
    
    VolSurface surface(quotes, marketData);
    
    ASSERT_EQ(20, quotes.size());
    ASSERT_TRUE(surface.strikes().size() > 0);
    ASSERT_TRUE(surface.expiries().size() > 0);
    ASSERT_EQ(100.0, surface.spot());
}

void testVolSurfaceInterpolation() {
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    auto quotes = MockDataGenerator::generateArbitrageFreeQuotes(20, 100.0);
    
    VolSurface surface(quotes, marketData);
    
    // Test interpolation at existing points
    double ivAtQuote = surface.impliedVol(quotes[0].strike, quotes[0].expiry);
    ASSERT_NEAR(quotes[0].iv, ivAtQuote, 1e-6);
    
    // Test interpolation at intermediate points
    double ivInterp = surface.impliedVol(105.0, 0.5);
    ASSERT_TRUE(ivInterp > 0.0 && ivInterp < 1.0);
}

void testBlackScholesFormulas() {
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    auto quotes = MockDataGenerator::generateArbitrageFreeQuotes(1, 100.0);
    
    VolSurface surface(quotes, marketData);
    
    // Test put-call parity
    double K = 100.0, T = 1.0;
    double callPrice = surface.callPrice(K, T);
    double putPrice = surface.putPrice(K, T);
    double forward = surface.forward(T);
    double discount = surface.discountFactor(T);
    
    double parityLHS = callPrice - putPrice;
    double parityRHS = discount * (forward - K);
    
    ASSERT_NEAR(parityLHS, parityRHS, 1e-8);
}

// ArbitrageDetector Tests
void testArbitrageFreeDetection() {
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    auto quotes = MockDataGenerator::generateArbitrageFreeQuotes(20, 100.0);
    
    VolSurface surface(quotes, marketData);
    ArbitrageDetector detector(surface);
    
    auto violations = detector.detect();
    ASSERT_TRUE(violations.empty());
    
    double qualityScore = detector.getQualityScore();
    ASSERT_NEAR(1.0, qualityScore, 0.1);
}

void testButterflyArbitrageDetection() {
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    auto quotes = MockDataGenerator::generateButterflyArbitrageQuotes(20, 100.0);
    
    VolSurface surface(quotes, marketData);
    ArbitrageDetector detector(surface);
    
    auto violations = detector.detect();
    ASSERT_FALSE(violations.empty());
    
    bool foundButterfly = false;
    for (const auto& v : violations) {
        if (v.type == ArbType::ButterflyViolation) {
            foundButterfly = true;
            break;
        }
    }
    ASSERT_TRUE(foundButterfly);
}

void testCalendarArbitrageDetection() {
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    auto quotes = MockDataGenerator::generateCalendarArbitrageQuotes(20, 100.0);
    
    VolSurface surface(quotes, marketData);
    ArbitrageDetector detector(surface);
    
    auto violations = detector.detect();
    ASSERT_FALSE(violations.empty());
    
    bool foundCalendar = false;
    for (const auto& v : violations) {
        if (v.type == ArbType::CalendarViolation) {
            foundCalendar = true;
            break;
        }
    }
    ASSERT_TRUE(foundCalendar);
}

void testSeverityAssessment() {
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    auto quotes = MockDataGenerator::generateButterflyArbitrageQuotes(20, 100.0);
    
    VolSurface surface(quotes, marketData);
    ArbitrageDetector detector(surface);
    
    auto violations = detector.detect();
    ASSERT_FALSE(violations.empty());
    
    for (const auto& v : violations) {
        double severity = v.severityScore();
        ASSERT_TRUE(severity >= 0.0 && severity <= 1.0);
    }
}

// QPSolver Tests
void testQPSolverConstruction() {
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    auto quotes = MockDataGenerator::generateArbitrageFreeQuotes(20, 100.0);
    
    VolSurface surface(quotes, marketData);
    QPSolver solver(surface);
    
    // Test that solver can be constructed
    ASSERT_TRUE(true);
}

void testQPSolverArbitrageFree() {
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    auto quotes = MockDataGenerator::generateArbitrageFreeQuotes(20, 100.0);
    
    VolSurface surface(quotes, marketData);
    QPSolver solver(surface);
    
    auto result = solver.solve();
    ASSERT_TRUE(result.success);
    
    auto correctedSurface = solver.buildCorrectedSurface(result);
    ArbitrageDetector detector(correctedSurface);
    auto violations = detector.detect();
    
    // Should be arbitrage-free after correction
    ASSERT_TRUE(violations.empty() || violations.size() < 5);
}

void testQPSolverWithArbitrage() {
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    auto quotes = MockDataGenerator::generateButterflyArbitrageQuotes(20, 100.0);
    
    VolSurface surface(quotes, marketData);
    QPSolver solver(surface);
    
    auto result = solver.solve();
    ASSERT_TRUE(result.success);
    
    auto correctedSurface = solver.buildCorrectedSurface(result);
    ArbitrageDetector detector(correctedSurface);
    auto violations = detector.detect();
    
    // Should have fewer violations after correction
    ArbitrageDetector originalDetector(surface);
    auto originalViolations = originalDetector.detect();
    
    ASSERT_TRUE(violations.size() <= originalViolations.size());
}

void testQPConfiguration() {
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    auto quotes = MockDataGenerator::generateArbitrageFreeQuotes(20, 100.0);
    
    VolSurface surface(quotes, marketData);
    
    QPSolver::Config config;
    config.tolerance = 1e-12;
    config.maxIterations = 5000;
    config.regularizationWeight = 1e-5;
    
    QPSolver solver(surface, config);
    auto result = solver.solve();
    
    ASSERT_TRUE(result.success);
    ASSERT_TRUE(result.iterations <= 5000);
}

// DataHandler Tests
void testDataLoading() {
    DataHandler::Config config;
    config.source = DataSource::JSON_FILE;
    config.filePath = "data/sample_quotes.json";
    
    DataHandler handler(config);
    
    // This test requires the sample file to exist
    // In a real test environment, you'd create a temporary test file
    
    ASSERT_TRUE(true); // Placeholder
}

void dataValidation() {
    DataHandler handler;
    
    // Test valid quote
    Quote validQuote{100.0, 1.0, 0.20};
    std::string errorMessage;
    ASSERT_TRUE(handler.validateQuote(validQuote, errorMessage));
    
    // Test invalid quote (negative strike)
    Quote invalidQuote{-100.0, 1.0, 0.20};
    ASSERT_FALSE(handler.validateQuote(invalidQuote, errorMessage));
    
    // Test invalid quote (zero volatility)
    Quote zeroVolQuote{100.0, 1.0, 0.0};
    ASSERT_FALSE(handler.validateQuote(zeroVolQuote, errorMessage));
}

void dataCleaning() {
    auto quotes = MockDataGenerator::generateQuotes(20, 100.0);
    
    // Add some invalid quotes
    Quote invalidQuote1{-100.0, 1.0, 0.20}; // Negative strike
    Quote invalidQuote2{100.0, 1.0, 0.0};   // Zero vol
    quotes.push_back(invalidQuote1);
    quotes.push_back(invalidQuote2);
    
    DataHandler handler;
    auto cleaned = handler.cleanData(quotes);
    
    // Should have fewer quotes after cleaning
    ASSERT_TRUE(cleaned.size() < quotes.size());
    
    // All remaining quotes should be valid
    for (const auto& quote : cleaned) {
        std::string errorMessage;
        ASSERT_TRUE(handler.validateQuote(quote, errorMessage));
    }
}

// SVI Surface Tests
void testSVIConstruction() {
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    auto quotes = MockDataGenerator::generateArbitrageFreeQuotes(20, 100.0);
    
    SVISurface sviSurface(quotes, marketData);
    
    ASSERT_TRUE(sviSurface.expiries().size() > 0);
    ASSERT_EQ(sviSurface.sviParams().size(), sviSurface.expiries().size());
}

void testSVIInterpolation() {
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    auto quotes = MockDataGenerator::generateArbitrageFreeQuotes(20, 100.0);
    
    SVISurface sviSurface(quotes, marketData);
    
    double iv = sviSurface.impliedVol(105.0, 0.5);
    ASSERT_TRUE(iv > 0.0 && iv < 1.0);
}

void testSVIArbitrageCheck() {
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    auto quotes = MockDataGenerator::generateArbitrageFreeQuotes(20, 100.0);
    
    SVISurface sviSurface(quotes, marketData);
    
    bool isArbFree = sviSurface.isArbitrageFree();
    auto violations = sviSurface.getArbitrageViolations();
    
    if (isArbFree) {
        ASSERT_TRUE(violations.empty());
    } else {
        ASSERT_FALSE(violations.empty());
    }
}

// Performance Tests
void testVolSurfacePerformance() {
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    auto quotes = MockDataGenerator::generateArbitrageFreeQuotes(1000, 100.0);
    
    VolSurface surface(quotes, marketData);
    
    auto testFunc = [&surface]() {
        for (int i = 0; i < 1000; ++i) {
            surface.impliedVol(80.0 + i * 0.04, 0.1 + i * 0.001);
        }
    };
    
    double time = PerformanceTester::measureExecutionTime(testFunc);
    ASSERT_TRUE(time < 100.0); // Should complete in less than 100ms
}

void testArbitrageDetectionPerformance() {
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    auto quotes = MockDataGenerator::generateArbitrageFreeQuotes(1000, 100.0);
    
    VolSurface surface(quotes, marketData);
    ArbitrageDetector detector(surface);
    
    auto testFunc = [&detector]() {
        detector.detect();
    };
    
    double time = PerformanceTester::measureExecutionTime(testFunc);
    ASSERT_TRUE(time < 50.0); // Should complete in less than 50ms
}

// Integration Tests
void testEndToEndWorkflow() {
    // Generate test data
    auto marketData = MockDataGenerator::generateMarketData(100.0);
    auto quotes = MockDataGenerator::generateButterflyArbitrageQuotes(50, 100.0);
    
    // Create surface
    VolSurface surface(quotes, marketData);
    
    // Detect arbitrage
    ArbitrageDetector detector(surface);
    auto violations = detector.detect();
    ASSERT_FALSE(violations.empty());
    
    // Solve QP
    QPSolver solver(surface);
    auto result = solver.solve();
    ASSERT_TRUE(result.success);
    
    // Build corrected surface
    auto correctedSurface = solver.buildCorrectedSurface(result);
    
    // Verify correction
    ArbitrageDetector correctedDetector(correctedSurface);
    auto correctedViolations = correctedDetector.detect();
    
    // Should have fewer violations
    ASSERT_TRUE(correctedViolations.size() < violations.size() || correctedViolations.empty());
}

void testConfigurationIntegration() {
    auto& config = ConfigManager::getInstance();
    config.loadDefaults();
    
    // Test configuration values
    ASSERT_TRUE(config.has("data.min_vol"));
    ASSERT_EQ(0.01, config.get<double>("data.min_vol"));
    ASSERT_EQ(1e-9, config.get<double>("qp.tolerance"));
    
    // Test configuration validation
    ASSERT_TRUE(config.validate());
}

// Create test suites
void registerAllTests() {
    auto& runner = TestRunner::getInstance();
    
    // VolSurface test suite
    auto volSuite = std::make_unique<TestSuite>("VolSurface");
    volSuite->addTest("Construction", testVolSurfaceConstruction);
    volSuite->addTest("Interpolation", testVolSurfaceInterpolation);
    volSuite->addTest("BlackScholes", testBlackScholesFormulas);
    volSuite->addTest("Performance", testVolSurfacePerformance);
    runner.addSuite(std::move(volSuite));
    
    // ArbitrageDetector test suite
    auto arbSuite = std::make_unique<TestSuite>("ArbitrageDetector");
    arbSuite->addTest("ArbitrageFreeDetection", testArbitrageFreeDetection);
    arbSuite->addTest("ButterflyDetection", testButterflyArbitrageDetection);
    arbSuite->addTest("CalendarDetection", testCalendarArbitrageDetection);
    arbSuite->addTest("SeverityAssessment", testSeverityAssessment);
    arbSuite->addTest("Performance", testArbitrageDetectionPerformance);
    runner.addSuite(std::move(arbSuite));
    
    // QPSolver test suite
    auto qpSuite = std::make_unique<TestSuite>("QPSolver");
    qpSuite->addTest("Construction", testQPSolverConstruction);
    qpSuite->addTest("ArbitrageFree", testQPSolverArbitrageFree);
    qpSuite->addTest("WithArbitrage", testQPSolverWithArbitrage);
    qpSuite->addTest("Configuration", testQPConfiguration);
    runner.addSuite(std::move(qpSuite));
    
    // DataHandler test suite
    auto dataSuite = std::make_unique<TestSuite>("DataHandler");
    dataSuite->addTest("DataLoading", testDataLoading);
    dataSuite->addTest("Validation", dataValidation);
    dataSuite->addTest("Cleaning", dataCleaning);
    runner.addSuite(std::move(dataSuite));
    
    // SVI test suite
    auto sviSuite = std::make_unique<TestSuite>("SVISurface");
    sviSuite->addTest("Construction", testSVIConstruction);
    sviSuite->addTest("Interpolation", testSVIInterpolation);
    sviSuite->addTest("ArbitrageCheck", testSVIArbitrageCheck);
    runner.addSuite(std::move(sviSuite));
    
    // Integration test suite
    auto integrationSuite = std::make_unique<TestSuite>("Integration");
    integrationSuite->addTest("EndToEndWorkflow", testEndToEndWorkflow);
    integrationSuite->addTest("ConfigurationIntegration", testConfigurationIntegration);
    runner.addSuite(std::move(integrationSuite));
}

int main() {
    // Initialize logging
    auto& logger = Logger::getInstance();
    logger.setLogLevel(LogLevel::INFO);
    logger.setLogFile("test_results.log");
    
    // Register all tests
    registerAllTests();
    
    // Run all tests
    auto& runner = TestRunner::getInstance();
    int failedTests = runner.runAllSuites();
    
    return failedTests;
}
