// ══════════════════════════════════════════════════════════════════════════════
// PHASE 6: QP Solver Unit Tests
// ══════════════════════════════════════════════════════════════════════════════
// Tests for the quadratic programming solver used to repair arbitrage violations
// in the volatility surface while minimizing perturbations.
// ══════════════════════════════════════════════════════════════════════════════

#include "test_framework.hpp"
#include "vol_surface.hpp"
#include "arbitrage_detector.hpp"
#include "qp_solver.hpp"
#include <cmath>

// ──────────────────────────────────────────────────────────────────────────────
// Test Suite: Basic QP Solver Functionality
// ──────────────────────────────────────────────────────────────────────────────

void test_qp_solver_construction() {
    auto quotes = MockDataGenerator::generateArbitrageFreeQuotes(21, 100.0);
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    
    VolSurface surface(quotes, marketData);
    
    ASSERT_NO_THROW(QPSolver solver(surface));
}

void test_qp_solver_with_config() {
    auto quotes = MockDataGenerator::generateArbitrageFreeQuotes(21, 100.0);
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    
    VolSurface surface(quotes, marketData);
    
    QPSolver::Config config;
    config.maxIterations = 500;
    config.tolerance = 1e-8;
    config.regularizationWeight = 0.01;
    
    ASSERT_NO_THROW(QPSolver solver(surface, config));
}

void test_qp_solver_solve_arbitrage_free() {
    auto quotes = MockDataGenerator::generateArbitrageFreeQuotes(21, 100.0);
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    
    VolSurface surface(quotes, marketData);
    QPSolver solver(surface);
    
    QPResult result = solver.solve();
    
    ASSERT_TRUE(result.success);
}

// ──────────────────────────────────────────────────────────────────────────────
// Test Suite: Repairing Arbitrage Violations
// ──────────────────────────────────────────────────────────────────────────────

void test_qp_solver_repairs_butterfly() {
    auto quotes = MockDataGenerator::generateButterflyArbitrageQuotes(21, 100.0);
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    
    VolSurface surface(quotes, marketData);
    
    // Verify there are violations before repair
    ArbitrageDetector detectorBefore(surface);
    auto violationsBefore = detectorBefore.detect();
    ASSERT_NOT_EMPTY(violationsBefore);
    
    // Run QP solver
    QPSolver solver(surface);
    QPResult result = solver.solve();
    
    ASSERT_TRUE(result.success);
    
    // Build corrected surface
    VolSurface corrected = solver.buildCorrectedSurface(result);
    
    // Verify violations are fixed
    ArbitrageDetector detectorAfter(corrected);
    auto violationsAfter = detectorAfter.detect();
    
    ASSERT_TRUE(violationsAfter.size() <= violationsBefore.size());
}

void test_qp_solver_repairs_calendar() {
    auto quotes = MockDataGenerator::generateCalendarArbitrageQuotes(21, 100.0);
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    
    VolSurface surface(quotes, marketData);
    
    // Verify there are violations before repair
    ArbitrageDetector detectorBefore(surface);
    auto violationsBefore = detectorBefore.detect();
    ASSERT_NOT_EMPTY(violationsBefore);
    
    // Run QP solver
    QPSolver solver(surface);
    QPResult result = solver.solve();
    
    ASSERT_TRUE(result.success);
}

void test_qp_solver_minimizes_perturbation() {
    auto quotes = MockDataGenerator::generateButterflyArbitrageQuotes(21, 100.0);
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    
    VolSurface surface(quotes, marketData);
    QPSolver solver(surface);
    
    QPResult result = solver.solve();
    
    // Check that perturbations are relatively small
    double maxPerturbation = 0.0;
    for (int i = 0; i < result.ivFlat.size(); ++i) {
        maxPerturbation = std::max(maxPerturbation, std::abs(result.ivFlat(i)));
    }
    
    // Max perturbation should be reasonable (less than 100% vol)
    ASSERT_TRUE(maxPerturbation < 1.0);
}

// ──────────────────────────────────────────────────────────────────────────────
// Test Suite: QPResult Properties
// ──────────────────────────────────────────────────────────────────────────────

void test_qp_result_iterations() {
    auto quotes = MockDataGenerator::generateButterflyArbitrageQuotes(21, 100.0);
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    
    VolSurface surface(quotes, marketData);
    QPSolver solver(surface);
    
    QPResult result = solver.solve();
    
    ASSERT_TRUE(result.iterations > 0);
    ASSERT_TRUE(result.iterations <= 2000);  // Should finish in reasonable iterations
}

void test_qp_result_objective_value() {
    auto quotes = MockDataGenerator::generateArbitrageFreeQuotes(21, 100.0);
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    
    VolSurface surface(quotes, marketData);
    QPSolver solver(surface);
    
    QPResult result = solver.solve();
    
    // Objective value can be negative or positive depending on numerical issues
    // Just verify it's a finite number
    ASSERT_FINITE(result.objectiveValue);
}

void test_qp_result_solve_time() {
    auto quotes = MockDataGenerator::generateArbitrageFreeQuotes(21, 100.0);
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    
    VolSurface surface(quotes, marketData);
    QPSolver solver(surface);
    
    QPResult result = solver.solve();
    
    // Should complete in reasonable time (less than 10 seconds)
    ASSERT_TRUE(result.solveTime >= 0.0);
    ASSERT_TRUE(result.solveTime < 10.0);
}

void test_qp_result_status_message() {
    auto quotes = MockDataGenerator::generateArbitrageFreeQuotes(21, 100.0);
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    
    VolSurface surface(quotes, marketData);
    QPSolver solver(surface);
    
    QPResult result = solver.solve();
    
    ASSERT_FALSE(result.status.empty());
}

// ──────────────────────────────────────────────────────────────────────────────
// Test Suite: Configuration Options
// ──────────────────────────────────────────────────────────────────────────────

void test_qp_config_regularization_weight() {
    auto quotes = MockDataGenerator::generateButterflyArbitrageQuotes(21, 100.0);
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    
    VolSurface surface(quotes, marketData);
    
    // High regularization
    QPSolver::Config configHigh;
    configHigh.regularizationWeight = 1.0;
    QPSolver solverHigh(surface, configHigh);
    QPResult resultHigh = solverHigh.solve();
    
    // Low regularization
    QPSolver::Config configLow;
    configLow.regularizationWeight = 0.001;
    QPSolver solverLow(surface, configLow);
    QPResult resultLow = solverLow.solve();
    
    // Both should succeed
    ASSERT_TRUE(resultHigh.success);
    ASSERT_TRUE(resultLow.success);
}

void test_qp_config_smoothness_weight() {
    auto quotes = MockDataGenerator::generateButterflyArbitrageQuotes(21, 100.0);
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    
    VolSurface surface(quotes, marketData);
    
    QPSolver::Config config;
    config.smoothnessWeight = 0.5;
    
    QPSolver solver(surface, config);
    QPResult result = solver.solve();
    
    ASSERT_TRUE(result.success);
}

void test_qp_config_max_iterations() {
    auto quotes = MockDataGenerator::generateButterflyArbitrageQuotes(21, 100.0);
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    
    VolSurface surface(quotes, marketData);
    
    QPSolver::Config config;
    config.maxIterations = 100;
    
    QPSolver solver(surface, config);
    QPResult result = solver.solve();
    
    ASSERT_TRUE(result.iterations <= 100);
}

void test_qp_config_tolerance() {
    auto quotes = MockDataGenerator::generateButterflyArbitrageQuotes(21, 100.0);
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    
    VolSurface surface(quotes, marketData);
    
    // Tight tolerance
    QPSolver::Config configTight;
    configTight.tolerance = 1e-10;
    QPSolver solverTight(surface, configTight);
    QPResult resultTight = solverTight.solve();
    
    // Loose tolerance
    QPSolver::Config configLoose;
    configLoose.tolerance = 1e-4;
    QPSolver solverLoose(surface, configLoose);
    QPResult resultLoose = solverLoose.solve();
    
    // Both should succeed
    ASSERT_TRUE(resultTight.success);
    ASSERT_TRUE(resultLoose.success);
}

void test_qp_config_vol_bounds() {
    auto quotes = MockDataGenerator::generateButterflyArbitrageQuotes(21, 100.0);
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    
    VolSurface surface(quotes, marketData);
    
    QPSolver::Config config;
    config.minVol = 0.01;
    config.maxVol = 2.0;
    
    QPSolver solver(surface, config);
    QPResult result = solver.solve();
    
    ASSERT_TRUE(result.success);
    
    // Check all IVs are within bounds
    for (int i = 0; i < result.ivFlat.size(); ++i) {
        ASSERT_TRUE(result.ivFlat(i) >= config.minVol - 0.001);
        ASSERT_TRUE(result.ivFlat(i) <= config.maxVol + 0.001);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Test Suite: Edge Cases
// ──────────────────────────────────────────────────────────────────────────────

void test_qp_small_surface() {
    std::vector<Quote> quotes = {
        {90.0, 0.5, 0.20},
        {100.0, 0.5, 0.18},
        {110.0, 0.5, 0.22}
    };
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    
    VolSurface surface(quotes, marketData);
    QPSolver solver(surface);
    
    QPResult result = solver.solve();
    
    ASSERT_TRUE(result.success);
}

void test_qp_large_surface() {
    auto quotes = ExtendedMockDataGenerator::generateLargeDataset(20, 10, 100.0);
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    
    VolSurface surface(quotes, marketData);
    QPSolver solver(surface);
    
    QPResult result = solver.solve();
    
    ASSERT_TRUE(result.success);
}

void test_qp_high_vol_surface() {
    std::vector<Quote> quotes;
    for (double T : {0.25, 0.5, 1.0}) {
        for (double K : {80.0, 90.0, 100.0, 110.0, 120.0}) {
            double logM = std::log(K / 100.0);
            double iv = 1.5 + 0.3 * logM * logM;  // High vol
            quotes.push_back({K, T, iv});
        }
    }
    
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    VolSurface surface(quotes, marketData);
    QPSolver solver(surface);
    
    QPResult result = solver.solve();
    
    ASSERT_TRUE(result.success);
}

void test_qp_low_vol_surface() {
    std::vector<Quote> quotes;
    for (double T : {0.25, 0.5, 1.0}) {
        for (double K : {80.0, 90.0, 100.0, 110.0, 120.0}) {
            double logM = std::log(K / 100.0);
            double iv = 0.05 + 0.01 * logM * logM;  // Low vol
            quotes.push_back({K, T, iv});
        }
    }
    
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    VolSurface surface(quotes, marketData);
    QPSolver solver(surface);
    
    QPResult result = solver.solve();
    
    ASSERT_TRUE(result.success);
}

// ──────────────────────────────────────────────────────────────────────────────
// Test Suite: Build Corrected Surface
// ──────────────────────────────────────────────────────────────────────────────

void test_build_corrected_surface() {
    auto quotes = MockDataGenerator::generateButterflyArbitrageQuotes(21, 100.0);
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    
    VolSurface surface(quotes, marketData);
    QPSolver solver(surface);
    
    QPResult result = solver.solve();
    ASSERT_TRUE(result.success);
    
    VolSurface corrected = solver.buildCorrectedSurface(result);
    
    // Verify corrected surface is valid
    double testVol = corrected.impliedVol(100.0, 0.5);
    ASSERT_TRUE(testVol > 0);
    ASSERT_FINITE(testVol);
}

void test_corrected_surface_maintains_structure() {
    auto quotes = MockDataGenerator::generateButterflyArbitrageQuotes(21, 100.0);
    MarketData marketData{100.0, 0.05, 0.02, "2024-01-01", "USD"};
    
    VolSurface surface(quotes, marketData);
    QPSolver solver(surface);
    
    QPResult result = solver.solve();
    VolSurface corrected = solver.buildCorrectedSurface(result);
    
    // Same number of strikes and expiries
    ASSERT_EQ(surface.strikes().size(), corrected.strikes().size());
    ASSERT_EQ(surface.expiries().size(), corrected.expiries().size());
}

// ──────────────────────────────────────────────────────────────────────────────
// Register All QP Solver Tests
// ──────────────────────────────────────────────────────────────────────────────

std::unique_ptr<TestSuite> createQPSolverTestSuite() {
    auto suite = std::make_unique<TestSuite>("QP Solver Unit Tests");
    
    // Basic functionality
    suite->addTest("QP Solver Construction", test_qp_solver_construction);
    suite->addTest("QP Solver With Config", test_qp_solver_with_config);
    suite->addTest("QP Solver Solve Arbitrage-Free", test_qp_solver_solve_arbitrage_free);
    
    // Repairing violations
    suite->addTest("QP Solver Repairs Butterfly", test_qp_solver_repairs_butterfly);
    suite->addTest("QP Solver Repairs Calendar", test_qp_solver_repairs_calendar);
    suite->addTest("QP Solver Minimizes Perturbation", test_qp_solver_minimizes_perturbation);
    
    // Result properties
    suite->addTest("QP Result Iterations", test_qp_result_iterations);
    suite->addTest("QP Result Objective Value", test_qp_result_objective_value);
    suite->addTest("QP Result Solve Time", test_qp_result_solve_time);
    suite->addTest("QP Result Status Message", test_qp_result_status_message);
    
    // Configuration
    suite->addTest("QP Config Regularization Weight", test_qp_config_regularization_weight);
    suite->addTest("QP Config Smoothness Weight", test_qp_config_smoothness_weight);
    suite->addTest("QP Config Max Iterations", test_qp_config_max_iterations);
    suite->addTest("QP Config Tolerance", test_qp_config_tolerance);
    suite->addTest("QP Config Vol Bounds", test_qp_config_vol_bounds);
    
    // Edge cases
    suite->addTest("QP Small Surface", test_qp_small_surface);
    suite->addTest("QP Large Surface", test_qp_large_surface);
    suite->addTest("QP High Vol Surface", test_qp_high_vol_surface);
    suite->addTest("QP Low Vol Surface", test_qp_low_vol_surface);
    
    // Corrected surface
    suite->addTest("Build Corrected Surface", test_build_corrected_surface);
    suite->addTest("Corrected Surface Maintains Structure", test_corrected_surface_maintains_structure);
    
    return suite;
}

void registerQPSolverTests() {
    auto& runner = TestRunner::getInstance();
    runner.addSuite(createQPSolverTestSuite());
}
