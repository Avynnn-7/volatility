#include "vol_surface.hpp"
#include "arbitrage_detector.hpp"
#include <cassert>
#include <iostream>
#include <cmath>

// ──────────────────────────────────────────────────────────────────────────────
// Minimal test harness (no external test framework needed)
// ──────────────────────────────────────────────────────────────────────────────
static int tests_run    = 0;
static int tests_passed = 0;

#define CHECK(cond, msg) do { \
    ++tests_run; \
    if (cond) { ++tests_passed; std::cout << "  PASS: " << msg << "\n"; } \
    else       { std::cout << "  FAIL: " << msg << "\n"; } \
} while(0)

// ──────────────────────────────────────────────────────────────────────────────
// Test 1: Flat surface has no violations
// ──────────────────────────────────────────────────────────────────────────────
static void test_flat_surface_no_violations() {
    std::cout << "\n[Test 1] Flat surface → no violations\n";
    std::vector<Quote> quotes;
    for (double T : {0.25, 0.5, 1.0})
        for (double K : {80.0, 90.0, 100.0, 110.0, 120.0})
            quotes.push_back({K, T, 0.20});

    VolSurface surf(quotes, 100.0);
    ArbitrageDetector det(surf);
    auto viols = det.detect();
    CHECK(viols.empty(), "Flat 20% surface is arbitrage-free");
}

// ──────────────────────────────────────────────────────────────────────────────
// Test 2: Butterfly violation is detected
// ──────────────────────────────────────────────────────────────────────────────
static void test_butterfly_detected() {
    std::cout << "\n[Test 2] Butterfly violation is detected\n";
    // ATM IV lower than wings → negative risk-neutral density → butterfly arb
    std::vector<Quote> quotes = {
        {80.0,  0.5, 0.25},   // wing
        {90.0,  0.5, 0.20},   // wing
        {100.0, 0.5, 0.10},   // ATM too low → butterfly arb
        {110.0, 0.5, 0.20},   // wing
        {120.0, 0.5, 0.25},   // wing
    };
    VolSurface surf(quotes, 100.0);
    ArbitrageDetector det(surf);
    auto viols = det.checkButterfly();
    bool found = !viols.empty();
    CHECK(found, "Butterfly violation detected when ATM IV << wings");
    if (found) ArbitrageDetector::report(viols);
}

// ──────────────────────────────────────────────────────────────────────────────
// Test 3: Calendar violation is detected
// ──────────────────────────────────────────────────────────────────────────────
static void test_calendar_detected() {
    std::cout << "\n[Test 3] Calendar violation is detected\n";
    // IV decreasing with maturity → calendar arb
    std::vector<Quote> quotes = {
        {100.0, 0.25, 0.30},   // short-dated high IV
        {100.0, 0.50, 0.20},   // long-dated lower IV → arb
        {100.0, 1.00, 0.21},
        // pad other strikes to avoid singular surface
        {90.0,  0.25, 0.32}, {110.0, 0.25, 0.31},
        {90.0,  0.50, 0.22}, {110.0, 0.50, 0.21},
        {90.0,  1.00, 0.22}, {110.0, 1.00, 0.22},
    };
    VolSurface surf(quotes, 100.0);
    ArbitrageDetector det(surf);
    auto viols = det.checkCalendar();
    bool found = !viols.empty();
    CHECK(found, "Calendar violation detected when IV decreases with maturity");
    if (found) ArbitrageDetector::report(viols);
}

// ──────────────────────────────────────────────────────────────────────────────
// Test 4: VolSurface interpolation is within bounds
// ──────────────────────────────────────────────────────────────────────────────
static void test_interpolation_bounds() {
    std::cout << "\n[Test 4] Interpolation stays in [min_iv, max_iv]\n";
    std::vector<Quote> quotes;
    double minIV = 0.15, maxIV = 0.35;
    for (double T : {0.25, 0.50, 1.0})
        for (double K : {80.0, 100.0, 120.0})
            quotes.push_back({K, T, minIV + (maxIV - minIV) * ((K - 80.0) / 40.0)});

    VolSurface surf(quotes, 100.0);
    bool ok = true;
    for (double K = 81.0; K < 119.0; K += 5.0)
        for (double T = 0.26; T < 0.99; T += 0.1) {
            double v = surf.impliedVol(K, T);
            if (v < minIV - 0.01 || v > maxIV + 0.01) { ok = false; break; }
        }
    CHECK(ok, "Bilinear interpolation stays within [min_iv, max_iv] ± 1%");
}

// ──────────────────────────────────────────────────────────────────────────────
int main() {
    std::cout << "========================================\n";
    std::cout << " vol_arb  Butterfly & Arbitrage Tests  \n";
    std::cout << "========================================\n";

    test_flat_surface_no_violations();
    test_butterfly_detected();
    test_calendar_detected();
    test_interpolation_bounds();

    std::cout << "\n----------------------------------------\n";
    std::cout << "Results: " << tests_passed << "/" << tests_run << " passed\n";
    std::cout << "----------------------------------------\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
