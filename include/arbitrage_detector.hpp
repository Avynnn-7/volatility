/**
 * @file arbitrage_detector.hpp
 * @brief Static arbitrage violation detection for volatility surfaces
 * @author vol_arb Team
 * @version 2.0
 * @date 2024
 *
 * This file provides comprehensive static arbitrage detection including:
 * - Butterfly arbitrage (negative risk-neutral density)
 * - Calendar arbitrage (decreasing total variance)
 * - Monotonicity violations (call price increasing with strike)
 * - Vertical spread violations
 * - Extreme value detection
 * - Density integrity checking
 *
 * ## Mathematical Background
 *
 * ### Butterfly Arbitrage
 * A butterfly spread has non-negative value iff the risk-neutral density is
 * non-negative, which requires: d^2C/dK^2 >= 0
 *
 * ### Calendar Arbitrage
 * For calendar spreads to have non-negative value, total variance must be
 * non-decreasing: d(sigma^2*T)/dT >= 0
 *
 * ### Monotonicity
 * Call prices must be decreasing in strike: dC/dK <= 0
 *
 * @see VolSurface for surface construction
 * @see QPSolver for arbitrage repair
 */

#pragma once
#include "vol_surface.hpp"
#include <vector>
#include <string>

/**
 * @brief Types of static arbitrage violations
 *
 * Enumeration of all detectable arbitrage violation types.
 * Each type represents a specific no-arbitrage condition failure.
 */
enum class ArbType {
    ButterflyViolation,      ///< d^2C/dK^2 < 0 (negative risk-neutral density)
    CalendarViolation,       ///< dC/dT < 0 (call price decreases with maturity)
    MonotonicityViolation,   ///< dC/dK > 0 (call price increases with strike)
    VerticalSpreadViolation, ///< C(K1,T) - C(K2,T) > K2-K1 for K1<K2
    TimeSpreadValueViolation,///< Put-call parity violations
    DensityIntegrityViolation, ///< integral of density != 1
    ExtremeValueViolation    ///< Unreasonable extreme values (sigma > 5 or sigma < 0)
};

/**
 * @brief Single arbitrage violation record
 *
 * Contains all information about a detected violation including
 * location, magnitude, and severity assessment.
 *
 * @code
 * for (const auto& v : detector.detect()) {
 *     if (v.isCritical()) {
 *         std::cerr << "CRITICAL: " << v.description << std::endl;
 *     }
 * }
 * @endcode
 */
struct ArbViolation {
    ArbType type;           ///< Type of violation
    double strike;          ///< Strike where violation occurs
    double expiry;          ///< Expiry where violation occurs
    double magnitude;       ///< Signed magnitude (negative = violation)
    double threshold;       ///< Detection threshold used
    std::string description; ///< Human-readable description
    
    /**
     * @brief Calculate severity score
     * @return Severity in [0, 1] where 1 is most severe
     *
     * Based on magnitude relative to threshold and violation type.
     */
    double severityScore() const;
    
    /**
     * @brief Check if violation is critical
     * @return True if severity > 0.5 or butterfly/calendar type
     *
     * Critical violations should be corrected before using the surface.
     */
    bool isCritical() const;
};

/**
 * @brief Static arbitrage detector for volatility surfaces
 *
 * Provides comprehensive detection of static arbitrage violations
 * with configurable thresholds and severity assessment.
 *
 * ## Example Usage
 * @code
 * VolSurface surface(quotes, marketData);
 * ArbitrageDetector detector(surface);
 *
 * // Configure detection
 * ArbitrageDetector::Config config;
 * config.butterflyThreshold = 1e-6;
 * config.enableParallelization = true;
 * detector.setConfig(config);
 *
 * // Run detection
 * auto violations = detector.detect();
 * detector.report(violations);
 *
 * // Check quality
 * double quality = detector.getQualityScore();
 * std::cout << "Surface quality: " << (quality * 100) << "%" << std::endl;
 * @endcode
 *
 * @see QPSolver for correcting detected violations
 */
class ArbitrageDetector {
public:
    /**
     * @brief Detection configuration parameters
     *
     * Configures thresholds for each violation type and
     * parallel processing options.
     */
    struct Config {
        double butterflyThreshold = 1e-6;     ///< Min density for butterfly
        double calendarThreshold = 1e-6;      ///< Min time decay for calendar
        double monotonicityThreshold = 1e-6;  ///< Max price increase for monotonicity
        double verticalSpreadThreshold = 1e-6;///< Max spread violation
        double extremeValueThreshold = 10.0;  ///< Maximum reasonable volatility
        double densityIntegrityThreshold = 1e-3; ///< Density integration tolerance
        bool enableDensityCheck = true;       ///< Enable expensive density check
        bool enableExtremeValueCheck = true;  ///< Check for unreasonable values
        
        // OpenMP parallelization settings
        bool enableParallelization = true;    ///< Enable parallel detection
        int numThreads = 0;                   ///< Thread count (0 = auto)
        int minWorkPerThread = 100;           ///< Min iterations per thread
    };
    
    /**
     * @brief Construct detector for given surface
     * @param surface Volatility surface to analyze
     */
    explicit ArbitrageDetector(const VolSurface& surface);
    
    /**
     * @brief Set detection configuration
     * @param config New configuration to use
     */
    void setConfig(const Config& config) { config_ = config; }
    
    /**
     * @brief Run all arbitrage checks
     * @return Vector of all detected violations
     *
     * Runs all enabled checks and returns combined results.
     * Uses parallel processing if enabled in config.
     */
    std::vector<ArbViolation> detect() const;

    /**
     * @brief Check for butterfly arbitrage
     * @return Vector of butterfly violations
     *
     * Detects where d^2C/dK^2 < threshold, indicating negative
     * risk-neutral density.
     */
    std::vector<ArbViolation> checkButterfly() const;
    
    /**
     * @brief Check for calendar arbitrage
     * @return Vector of calendar violations
     *
     * Detects where dC/dT < threshold, indicating call prices
     * decrease with maturity.
     */
    std::vector<ArbViolation> checkCalendar() const;
    
    /**
     * @brief Check for monotonicity violations
     * @return Vector of monotonicity violations
     *
     * Detects where dC/dK > threshold, indicating call prices
     * increase with strike.
     */
    std::vector<ArbViolation> checkMonotonicity() const;
    
    /**
     * @brief Check for vertical spread violations
     * @return Vector of vertical spread violations
     *
     * Detects where C(K1) - C(K2) > (K2-K1)*exp(-rT) for K1 < K2.
     */
    std::vector<ArbViolation> checkVerticalSpread() const;
    
    /**
     * @brief Check for extreme volatility values
     * @return Vector of extreme value violations
     *
     * Detects implausible volatility values (e.g., > 1000% or < 0).
     */
    std::vector<ArbViolation> checkExtremeValues() const;
    
    /**
     * @brief Check risk-neutral density integrates to 1
     * @return Vector of density integrity violations
     *
     * Numerically integrates the implied density and checks
     * that integral of p(K)dK is approximately 1.
     *
     * @note This is computationally expensive.
     */
    std::vector<ArbViolation> checkDensityIntegrity() const;

    /**
     * @brief Print human-readable violation report
     * @param violations Vector of violations to report
     *
     * Outputs formatted report with severity indicators.
     */
    static void report(const std::vector<ArbViolation>& violations);
    
    /**
     * @brief Calculate overall surface quality score
     * @return Quality score in [0, 1] where 1 = arbitrage-free
     *
     * Based on number and severity of violations detected.
     */
    double getQualityScore() const;

private:
    const VolSurface& surface_;
    Config config_;

    // Numerical derivatives with configurable step sizes
    double d2CdK2(double K, double T, double dK) const;
    double dCdT(double K, double T, double dT) const;
    double dCdK(double K, double T, double dK) const;
    
    // Adaptive step size configuration
    struct AdaptiveStepConfig {
        double h_min_K = 1e-6;      ///< Minimum step for K
        double h_max_K = 1e-2;      ///< Maximum step for K
        double h_min_T = 1e-8;      ///< Minimum step for T
        double h_max_T = 1e-3;      ///< Maximum step for T
        double safety_factor = 0.8; ///< Conservative factor
    };
    
    AdaptiveStepConfig adaptiveConfig_;
    
    // Adaptive step size computation
    double adaptiveStepSize(double K, double T) const;
    double computeOptimalStepK(double K, double T) const;
    double computeOptimalStepT(double K, double T) const;
    
    // Third derivative estimation for optimal step
    double estimate3rdDerivativeK(double K, double T) const;
    double estimate3rdDerivativeT(double K, double T) const;
    
    // Put-call parity verification
    bool checkPutCallParity(double K, double T, double tolerance = 1e-6) const;
    
    // Density integration
    double integrateDensity(double T) const;
    
    // Severity calculation
    double calculateButterflySeverity(double density) const;
    double calculateCalendarSeverity(double timeDecay) const;
};
