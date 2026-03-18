#include "arbitrage_detector.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>

// ArbViolation severity assessment
double ArbViolation::severityScore() const {
    // Normalize severity based on violation type and magnitude
    double abs_magnitude = std::abs(magnitude);
    
    switch (type) {
        case ArbType::ButterflyViolation:
            // Negative density is very serious
            return std::min(1.0, abs_magnitude * 1000.0);
        case ArbType::CalendarViolation:
            // Negative time decay is serious
            return std::min(1.0, abs_magnitude * 100.0);
        case ArbType::MonotonicityViolation:
            // Price increasing with strike is moderate
            return std::min(1.0, abs_magnitude * 10.0);
        case ArbType::VerticalSpreadViolation:
            return std::min(1.0, abs_magnitude * 50.0);
        case ArbType::ExtremeValueViolation:
            // Extreme vol values are very serious
            return std::min(1.0, abs_magnitude / 10.0);
        case ArbType::DensityIntegrityViolation:
            // Density not integrating to 1 is serious
            return std::min(1.0, abs_magnitude * 100.0);
        default:
            return 0.1;
    }
}

bool ArbViolation::isCritical() const {
    return severityScore() > 0.7;
}

ArbitrageDetector::ArbitrageDetector(const VolSurface& surface)
    : surface_(surface) {}

// ──────────────────────────────────────────────────────────────────────────────
// Adaptive step size selection for robust numerical derivatives
// ──────────────────────────────────────────────────────────────────────────────
double ArbitrageDetector::adaptiveStepSize(double K, double T) const {
    // Step size should scale with the magnitude of the variables
    double dK = K * 1e-4;  // 0.01% of strike
    double dT = T * 1e-3;  // 0.1% of time
    
    // Ensure minimum step sizes to avoid numerical issues
    dK = std::max(dK, 1e-6);
    dT = std::max(dT, 1e-8);
    
    return std::max(dK, dT);
}

// ──────────────────────────────────────────────────────────────────────────────
// Enhanced finite difference helpers with adaptive step sizes
// ──────────────────────────────────────────────────────────────────────────────
double ArbitrageDetector::d2CdK2(double K, double T, double dK) const {
    double step = dK > 0 ? dK : adaptiveStepSize(K, T);
    double Cu = surface_.callPrice(K + step, T);
    double Cm = surface_.callPrice(K, T);
    double Cd = surface_.callPrice(K - step, T);
    return (Cu - 2.0 * Cm + Cd) / (step * step);
}

double ArbitrageDetector::dCdT(double K, double T, double dT) const {
    double step = dT > 0 ? dT : adaptiveStepSize(K, T);
    double Cu = surface_.callPrice(K, T + step);
    double Cd = surface_.callPrice(K, T - step);
    return (Cu - Cd) / (2.0 * step);
}

double ArbitrageDetector::dCdK(double K, double T, double dK) const {
    double step = dK > 0 ? dK : adaptiveStepSize(K, T);
    double Cu = surface_.callPrice(K + step, T);
    double Cd = surface_.callPrice(K - step, T);
    return (Cu - Cd) / (2.0 * step);
}

// ──────────────────────────────────────────────────────────────────────────────
// Enhanced butterfly arbitrage check with configurable thresholds
// ──────────────────────────────────────────────────────────────────────────────
std::vector<ArbViolation> ArbitrageDetector::checkButterfly() const {
    std::vector<ArbViolation> viols;
    const auto& Ks = surface_.strikes();
    const auto& Ts = surface_.expiries();

    for (double T : Ts) {
        for (int j = 1; j + 1 < (int)Ks.size(); ++j) {
            double dK = (Ks[j + 1] - Ks[j - 1]) / 2.0;
            double density = d2CdK2(Ks[j], T, dK);
            
            if (density < config_.butterflyThreshold) {
                ArbViolation viol;
                viol.type = ArbType::ButterflyViolation;
                viol.strike = Ks[j];
                viol.expiry = T;
                viol.magnitude = density;
                viol.threshold = config_.butterflyThreshold;
                viol.description = "Butterfly: density = " + std::to_string(density) +
                    " < " + std::to_string(config_.butterflyThreshold) +
                    " at K=" + std::to_string(Ks[j]) + ", T=" + std::to_string(T);
                viols.push_back(viol);
            }
        }
    }
    return viols;
}

// ──────────────────────────────────────────────────────────────────────────────
// Enhanced calendar arbitrage check
// ──────────────────────────────────────────────────────────────────────────────
std::vector<ArbViolation> ArbitrageDetector::checkCalendar() const {
    std::vector<ArbViolation> viols;
    const auto& Ks = surface_.strikes();
    const auto& Ts = surface_.expiries();

    for (double K : Ks) {
        for (int i = 0; i + 1 < (int)Ts.size(); ++i) {
            double dT = (Ts[i + 1] - Ts[i]) / 2.0;
            double Teval = (Ts[i] + Ts[i + 1]) / 2.0;
            double timeDecay = dCdT(K, Teval, dT);
            
            if (timeDecay < config_.calendarThreshold) {
                ArbViolation viol;
                viol.type = ArbType::CalendarViolation;
                viol.strike = K;
                viol.expiry = Teval;
                viol.magnitude = timeDecay;
                viol.threshold = config_.calendarThreshold;
                viol.description = "Calendar: dC/dT = " + std::to_string(timeDecay) +
                    " < " + std::to_string(config_.calendarThreshold) +
                    " at K=" + std::to_string(K) + ", T=" + std::to_string(Teval);
                viols.push_back(viol);
            }
        }
    }
    return viols;
}

// ──────────────────────────────────────────────────────────────────────────────
// Enhanced monotonicity check
// ──────────────────────────────────────────────────────────────────────────────
std::vector<ArbViolation> ArbitrageDetector::checkMonotonicity() const {
    std::vector<ArbViolation> viols;
    const auto& Ks = surface_.strikes();
    const auto& Ts = surface_.expiries();

    for (double T : Ts) {
        for (int j = 1; j + 1 < (int)Ks.size(); ++j) {
            double dK = (Ks[j + 1] - Ks[j - 1]) / 2.0;
            double slope = dCdK(Ks[j], T, dK);
            
            if (slope > config_.monotonicityThreshold) {
                ArbViolation viol;
                viol.type = ArbType::MonotonicityViolation;
                viol.strike = Ks[j];
                viol.expiry = T;
                viol.magnitude = slope;
                viol.threshold = config_.monotonicityThreshold;
                viol.description = "Monotonicity: dC/dK = " + std::to_string(slope) +
                    " > " + std::to_string(config_.monotonicityThreshold) +
                    " at K=" + std::to_string(Ks[j]) + ", T=" + std::to_string(T);
                viols.push_back(viol);
            }
        }
    }
    return viols;
}

// ──────────────────────────────────────────────────────────────────────────────
// Run all checks
// ──────────────────────────────────────────────────────────────────────────────
std::vector<ArbViolation> ArbitrageDetector::detect() const {
    auto v1 = checkButterfly();
    auto v2 = checkCalendar();
    auto v3 = checkMonotonicity();
    v1.insert(v1.end(), v2.begin(), v2.end());
    v1.insert(v1.end(), v3.begin(), v3.end());
    return v1;
}

// ──────────────────────────────────────────────────────────────────────────────
// Report
// ──────────────────────────────────────────────────────────────────────────────
void ArbitrageDetector::report(const std::vector<ArbViolation>& violations) {
    if (violations.empty()) {
        std::cout << "✓ No arbitrage violations detected.\n";
        return;
    }
    std::cout << "\n=== Arbitrage Violations (" << violations.size() << ") ===\n";
    for (const auto& v : violations) {
        std::string type;
        switch (v.type) {
            case ArbType::ButterflyViolation:    type = "[BUTTERFLY]";    break;
            case ArbType::CalendarViolation:     type = "[CALENDAR]";     break;
            case ArbType::MonotonicityViolation: type = "[MONOTONICITY]"; break;
        }
        std::cout << std::setw(16) << type << "  " << v.description
                  << "  (mag=" << std::scientific << std::setprecision(3) << v.magnitude << ")\n";
    }
    std::cout << "\n";
}
