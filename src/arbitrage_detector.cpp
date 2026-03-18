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
    
    if (config_.enableExtremeValueCheck) {
        auto v4 = checkExtremeValues();
        v1.insert(v1.end(), v4.begin(), v4.end());
    }
    
    if (config_.enableDensityCheck) {
        auto v5 = checkDensityIntegrity();
        v1.insert(v1.end(), v5.begin(), v5.end());
    }
    
    auto v6 = checkVerticalSpread();
    v1.insert(v1.end(), v6.begin(), v6.end());
    
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
    
    // Sort by severity (critical first)
    auto sortedViols = violations;
    std::sort(sortedViols.begin(), sortedViols.end(), 
              [](const ArbViolation& a, const ArbViolation& b) {
                  return a.severityScore() > b.severityScore();
              });
    
    for (const auto& v : sortedViols) {
        std::string type;
        switch (v.type) {
            case ArbType::ButterflyViolation:    type = "[BUTTERFLY]";    break;
            case ArbType::CalendarViolation:     type = "[CALENDAR]";     break;
            case ArbType::MonotonicityViolation: type = "[MONOTONICITY]"; break;
            case ArbType::VerticalSpreadViolation: type = "[VERTICAL]";     break;
            case ArbType::ExtremeValueViolation: type = "[EXTREME]";      break;
            case ArbType::DensityIntegrityViolation: type = "[DENSITY]";   break;
        }
        
        std::string severity = v.isCritical() ? " [CRITICAL]" : "";
        std::cout << std::setw(16) << type << severity << "  " << v.description
                  << "  (severity=" << std::fixed << std::setprecision(3) << v.severityScore() << ")\n";
    }
    std::cout << "\n";
}

// ──────────────────────────────────────────────────────────────────────────────
// Additional enhanced arbitrage checks
// ──────────────────────────────────────────────────────────────────────────────
std::vector<ArbViolation> ArbitrageDetector::checkExtremeValues() const {
    std::vector<ArbViolation> viols;
    const auto& Ks = surface_.strikes();
    const auto& Ts = surface_.expiries();

    for (double T : Ts) {
        for (double K : Ks) {
            double iv = surface_.impliedVol(K, T);
            
            if (iv > config_.extremeValueThreshold || iv < 0.01) {
                ArbViolation viol;
                viol.type = ArbType::ExtremeValueViolation;
                viol.strike = K;
                viol.expiry = T;
                viol.magnitude = iv;
                viol.threshold = config_.extremeValueThreshold;
                viol.description = "Extreme IV: " + std::to_string(iv * 100) + "% at K=" + 
                    std::to_string(K) + ", T=" + std::to_string(T);
                viols.push_back(viol);
            }
        }
    }
    return viols;
}

std::vector<ArbViolation> ArbitrageDetector::checkVerticalSpread() const {
    std::vector<ArbViolation> viols;
    const auto& Ks = surface_.strikes();
    const auto& Ts = surface_.expiries();

    for (double T : Ts) {
        for (int i = 0; i + 1 < (int)Ks.size(); ++i) {
            double K1 = Ks[i], K2 = Ks[i + 1];
            double C1 = surface_.callPrice(K1, T);
            double C2 = surface_.callPrice(K2, T);
            
            // Vertical spread should not exceed intrinsic value difference
            double spreadValue = C1 - C2;
            double maxAllowed = K2 - K1;
            
            if (spreadValue > maxAllowed + config_.verticalSpreadThreshold) {
                ArbViolation viol;
                viol.type = ArbType::VerticalSpreadViolation;
                viol.strike = (K1 + K2) / 2.0;  // Midpoint
                viol.expiry = T;
                viol.magnitude = spreadValue - maxAllowed;
                viol.threshold = config_.verticalSpreadThreshold;
                viol.description = "Vertical spread: C(" + std::to_string(K1) + ") - C(" + 
                    std::to_string(K2) + ") = " + std::to_string(spreadValue) + 
                    " > " + std::to_string(maxAllowed) + " at T=" + std::to_string(T);
                viols.push_back(viol);
            }
        }
    }
    return viols;
}

std::vector<ArbViolation> ArbitrageDetector::checkDensityIntegrity() const {
    std::vector<ArbViolation> viols;
    const auto& Ts = surface_.expiries();

    for (double T : Ts) {
        double integral = integrateDensity(T);
        double error = std::abs(integral - 1.0);
        
        if (error > config_.densityIntegrityThreshold) {
            ArbViolation viol;
            viol.type = ArbType::DensityIntegrityViolation;
            viol.strike = 0.0;  // Not applicable
            viol.expiry = T;
            viol.magnitude = error;
            viol.threshold = config_.densityIntegrityThreshold;
            viol.description = "Density integral: " + std::to_string(integral) + 
                " (error=" + std::to_string(error) + ") at T=" + std::to_string(T);
            viols.push_back(viol);
        }
    }
    return viols;
}

double ArbitrageDetector::integrateDensity(double T) const {
    // Simple trapezoidal integration of risk-neutral density
    const auto& Ks = surface_.strikes();
    if (Ks.size() < 2) return 1.0;
    
    double integral = 0.0;
    for (int i = 0; i + 1 < (int)Ks.size(); ++i) {
        double K1 = Ks[i], K2 = Ks[i + 1];
        double density1 = d2CdK2(K1, T, adaptiveStepSize(K1, T));
        double density2 = d2CdK2(K2, T, adaptiveStepSize(K2, T));
        
        integral += 0.5 * (density1 + density2) * (K2 - K1);
    }
    return integral;
}

double ArbitrageDetector::getQualityScore() const {
    auto violations = detect();
    if (violations.empty()) return 1.0;
    
    double totalSeverity = 0.0;
    int criticalCount = 0;
    
    for (const auto& v : violations) {
        totalSeverity += v.severityScore();
        if (v.isCritical()) criticalCount++;
    }
    
    // Penalize critical violations heavily
    double criticalPenalty = criticalCount * 0.3;
    double severityPenalty = std::min(0.7, totalSeverity / violations.size());
    
    return std::max(0.0, 1.0 - criticalPenalty - severityPenalty);
}
