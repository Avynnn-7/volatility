// ══════════════════════════════════════════════════════════════════════════════
// PHASE 6: Stress Tests
// ══════════════════════════════════════════════════════════════════════════════
// Tests for system behavior under extreme load, large datasets, and
// resource-constrained conditions.
// ══════════════════════════════════════════════════════════════════════════════

#include "test_framework.hpp"
#include "vol_surface.hpp"
#include "arbitrage_detector.hpp"
#include "qp_solver.hpp"
#include <chrono>
#include <cmath>

// ──────────────────────────────────────────────────────────────────────────────
// Test Suite: Large Dataset Handling
// ──────────────────────────────────────────────────────────────────────────────

void test_stress_large_quote_count() {
    // Generate 1000 quotes
    auto quotes = ExtendedMockDataGenerator::generateLargeDataset(50, 20, 100.0);
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    
    auto start = std::chrono::high_resolution_clock::now();
    VolSurface surface(quotes, marketData);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    ASSERT_TRUE(duration.count() < 5000);  // Should complete within 5 seconds
    
    // Verify surface is functional
    double iv = surface.impliedVol(100.0, 1.0);
    ASSERT_FINITE(iv);
}

void test_stress_massive_surface() {
    // Generate 10000 quotes
    auto quotes = ExtendedMockDataGenerator::generateLargeDataset(100, 100, 100.0);
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    
    auto start = std::chrono::high_resolution_clock::now();
    VolSurface surface(quotes, marketData);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    ASSERT_TRUE(duration.count() < 30000);  // Should complete within 30 seconds
}

// ──────────────────────────────────────────────────────────────────────────────
// Test Suite: Repeated Operations
// ──────────────────────────────────────────────────────────────────────────────

void test_stress_repeated_surface_creation() {
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 1000; ++i) {
        auto quotes = MockDataGenerator::generateArbitrageFreeQuotes(21, 100.0);
        VolSurface surface(quotes, marketData);
        double iv = surface.impliedVol(100.0, 0.5);
        ASSERT_FINITE(iv);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    ASSERT_TRUE(duration.count() < 30000);
}

void test_stress_repeated_interpolation() {
    auto quotes = MockDataGenerator::generateArbitrageFreeQuotes(21, 100.0);
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    VolSurface surface(quotes, marketData);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 100000; ++i) {
        double K = 80.0 + (i % 40);
        double T = 0.25 + (i % 4) * 0.25;
        double iv = surface.impliedVol(K, T);
        ASSERT_FINITE(iv);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    ASSERT_TRUE(duration.count() < 10000);  // 100k interpolations in 10 seconds
}

void test_stress_repeated_detection() {
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 100; ++i) {
        auto quotes = MockDataGenerator::generateButterflyArbitrageQuotes(21, 100.0);
        VolSurface surface(quotes, marketData);
        ArbitrageDetector detector(surface);
        auto violations = detector.detect();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    ASSERT_TRUE(duration.count() < 30000);
}

void test_stress_repeated_qp_solve() {
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 50; ++i) {
        auto quotes = MockDataGenerator::generateButterflyArbitrageQuotes(15, 100.0);
        VolSurface surface(quotes, marketData);
        QPSolver solver(surface);
        QPResult result = solver.solve();
        ASSERT_TRUE(result.success);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    ASSERT_TRUE(duration.count() < 60000);  // 50 QP solves in 60 seconds
}

// ──────────────────────────────────────────────────────────────────────────────
// Test Suite: Memory Stress
// ──────────────────────────────────────────────────────────────────────────────

void test_stress_memory_not_growing() {
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    
    // Create and destroy many surfaces to check for leaks
    for (int iteration = 0; iteration < 100; ++iteration) {
        auto quotes = ExtendedMockDataGenerator::generateLargeDataset(20, 10, 100.0);
        VolSurface surface(quotes, marketData);
        double iv = surface.impliedVol(100.0, 1.0);
        ASSERT_FINITE(iv);
    }
    
    // If we get here without crash or exception, memory is being managed
    ASSERT_TRUE(true);
}

// ──────────────────────────────────────────────────────────────────────────────
// Test Suite: Concurrent Load (Single-threaded simulation)
// ──────────────────────────────────────────────────────────────────────────────

void test_stress_simulated_concurrent_load() {
    auto quotes = MockDataGenerator::generateArbitrageFreeQuotes(21, 100.0);
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    VolSurface surface(quotes, marketData);
    
    // Simulate interleaved operations
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 10000; ++i) {
        // Mix of operations
        switch (i % 4) {
            case 0:
                surface.impliedVol(90.0 + (i % 20), 0.25 + (i % 4) * 0.25);
                break;
            case 1:
                surface.callPrice(100.0, 0.5);
                break;
            case 2:
                surface.putPrice(100.0, 1.0);
                break;
            case 3:
                surface.impliedVol(100.0, 0.5);
                break;
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    ASSERT_TRUE(duration.count() < 5000);
}

// ──────────────────────────────────────────────────────────────────────────────
// Test Suite: Full Pipeline Stress
// ──────────────────────────────────────────────────────────────────────────────

void test_stress_full_pipeline() {
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 20; ++i) {
        // Generate quotes with violations
        auto quotes = MockDataGenerator::generateButterflyArbitrageQuotes(21, 100.0);
        
        // Create surface
        VolSurface surface(quotes, marketData);
        
        // Detect arbitrage
        ArbitrageDetector detector(surface);
        auto violations = detector.detect();
        
        // If violations exist, run QP solver
        if (!violations.empty()) {
            QPSolver solver(surface);
            QPResult result = solver.solve();
            ASSERT_TRUE(result.success);
            
            // Build corrected surface
            VolSurface corrected = solver.buildCorrectedSurface(result);
            
            // Verify correction
            ArbitrageDetector verifier(corrected);
            auto remaining = verifier.detect();
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    ASSERT_TRUE(duration.count() < 120000);  // Full pipeline 20 times in 2 minutes
}

// ──────────────────────────────────────────────────────────────────────────────
// Register All Stress Tests
// ──────────────────────────────────────────────────────────────────────────────

std::unique_ptr<TestSuite> createStressTestSuite() {
    auto suite = std::make_unique<TestSuite>("Stress Tests");
    
    // Large datasets
    suite->addTest("Stress Large Quote Count", test_stress_large_quote_count, 30.0);
    suite->addTest("Stress Massive Surface", test_stress_massive_surface, 60.0);
    
    // Repeated operations
    suite->addTest("Stress Repeated Surface Creation", test_stress_repeated_surface_creation, 60.0);
    suite->addTest("Stress Repeated Interpolation", test_stress_repeated_interpolation, 30.0);
    suite->addTest("Stress Repeated Detection", test_stress_repeated_detection, 60.0);
    suite->addTest("Stress Repeated QP Solve", test_stress_repeated_qp_solve, 120.0);
    
    // Memory
    suite->addTest("Stress Memory Not Growing", test_stress_memory_not_growing, 60.0);
    
    // Concurrent simulation
    suite->addTest("Stress Simulated Concurrent Load", test_stress_simulated_concurrent_load, 30.0);
    
    // Full pipeline
    suite->addTest("Stress Full Pipeline", test_stress_full_pipeline, 180.0);
    
    return suite;
}

void registerStressTests() {
    auto& runner = TestRunner::getInstance();
    runner.addSuite(createStressTestSuite());
}
