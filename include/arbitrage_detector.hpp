#pragma once
#include "vol_surface.hpp"
#include <vector>
#include <string>

// Types of static arbitrage violations
enum class ArbType {
    ButterflyViolation,     // d²C/dK² < 0  (negative risk-neutral density)
    CalendarViolation,      // dC/dT < 0    (call price decreases with maturity)
    MonotonicityViolation,  // dC/dK > 0    (call price increases with strike)
    VerticalSpreadViolation, // C(K1,T) - C(K2,T) > K2-K1 for K1<K2
    TimeSpreadValueViolation, // Put-call parity violations
    DensityIntegrityViolation, // ∫ density ≠ 1
    ExtremeValueViolation    // Unreasonable extreme values
};

// A single detected arbitrage violation
struct ArbViolation {
    ArbType   type;
    double    strike;
    double    expiry;
    double    magnitude;   // How large the violation is (negative = bad)
    double    threshold;    // Threshold for detection
    std::string description;
    
    // Severity assessment
    double severityScore() const;
    bool isCritical() const;
};

// Enhanced arbitrage detector with robust methods
class ArbitrageDetector {
public:
    explicit ArbitrageDetector(const VolSurface& surface);
    
    // Configuration for detection thresholds
    struct Config {
        double butterflyThreshold = 1e-6;     // Minimum density
        double calendarThreshold = 1e-6;      // Minimum time decay
        double monotonicityThreshold = 1e-6;   // Maximum price increase
        double verticalSpreadThreshold = 1e-6; // Maximum spread violation
        double extremeValueThreshold = 10.0;  // Maximum reasonable vol
        double densityIntegrityThreshold = 1e-3; // Density integration error
        bool enableDensityCheck = true;       // Expensive density integration
        bool enableExtremeValueCheck = true;  // Check for unreasonable values
    };
    
    // Set configuration
    void setConfig(const Config& config) { config_ = config; }
    
    // Run all checks; returns list of violations found
    std::vector<ArbViolation> detect() const;

    // Individual checks with enhanced methods
    std::vector<ArbViolation> checkButterfly() const;
    std::vector<ArbViolation> checkCalendar() const;
    std::vector<ArbViolation> checkMonotonicity() const;
    std::vector<ArbViolation> checkVerticalSpread() const;
    std::vector<ArbViolation> checkExtremeValues() const;
    std::vector<ArbViolation> checkDensityIntegrity() const;

    // Print a human-readable summary with severity assessment
    static void report(const std::vector<ArbViolation>& violations);
    
    // Get overall surface quality score (0-1, higher is better)
    double getQualityScore() const;

private:
    const VolSurface& surface_;
    Config config_;

    // Robust numerical derivatives with adaptive step sizes
    double d2CdK2(double K, double T, double dK) const;
    double dCdT(double K, double T, double dT) const;
    double dCdK(double K, double T, double dK) const;
    
    // Adaptive step size selection
    double adaptiveStepSize(double K, double T) const;
    
    // Put-call parity verification
    bool checkPutCallParity(double K, double T, double tolerance = 1e-6) const;
    
    // Risk-neutral density integration
    double integrateDensity(double T) const;
    
    // Helper methods for severity assessment
    double calculateButterflySeverity(double density) const;
    double calculateCalendarSeverity(double timeDecay) const;
};
