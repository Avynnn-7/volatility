/**
 * @file profit_advisor.hpp
 * @brief Profit advisor: transforms arbitrage violations into actionable trade recommendations
 * @author vol_arb Team
 * @version 1.0
 * @date 2024
 *
 * Given detected arbitrage violations on a volatility surface, generates
 * specific option trade suggestions with expected P&L, risk, and breakeven.
 *
 * Supports:
 * - Butterfly spread exploitation (from butterfly violations)
 * - Calendar spread exploitation (from calendar violations)
 * - Vertical spread exploitation (from monotonicity/vertical violations)
 */

#pragma once
#include "vol_surface.hpp"
#include "arbitrage_detector.hpp"
#include <vector>
#include <string>
#include <nlohmann/json.hpp>

/**
 * @brief A single leg in an option trade
 */
struct TradeLeg {
    enum class Action { BUY, SELL };
    enum class OptionType { CALL, PUT };

    Action action;
    OptionType optionType;
    double strike;
    double expiry;
    int quantity;       ///< Number of contracts (always positive)
    double price;       ///< Theoretical price per contract

    nlohmann::json toJson() const;
};

/**
 * @brief A complete trade recommendation with P&L analysis
 */
struct TradeRecommendation {
    std::string name;               ///< e.g., "Butterfly Spread at K=100, T=0.25"
    std::string strategy;           ///< e.g., "BUTTERFLY", "CALENDAR", "VERTICAL"
    std::string description;        ///< Human-readable explanation
    std::vector<TradeLeg> legs;     ///< Ordered list of trade legs
    
    double expectedProfit;          ///< Expected profit per unit at expiry
    double maxRisk;                 ///< Maximum loss per unit
    double netCost;                 ///< Net cost to enter (negative = credit)
    double profitRatio;             ///< expectedProfit / maxRisk
    
    double severity;                ///< Underlying violation severity [0,1]
    std::string urgency;            ///< "HIGH", "MEDIUM", "LOW"
    
    // Source violation info
    ArbType violationType;
    double violationMagnitude;

    nlohmann::json toJson() const;
};

/**
 * @brief Analyzes arbitrage violations and generates trade recommendations
 *
 * ## Example Usage
 * @code
 * VolSurface surface(quotes, marketData);
 * ArbitrageDetector detector(surface);
 * auto violations = detector.detect();
 *
 * ProfitAdvisor advisor(surface, violations);
 * auto trades = advisor.generateRecommendations();
 *
 * // Print as JSON
 * std::cout << advisor.toJson(trades).dump(2) << std::endl;
 *
 * // Print human-readable report
 * advisor.printReport(trades);
 * @endcode
 */
class ProfitAdvisor {
public:
    /**
     * @brief Construct profit advisor
     * @param surface Market volatility surface (for pricing legs)
     * @param violations Detected arbitrage violations
     */
    ProfitAdvisor(const VolSurface& surface,
                  const std::vector<ArbViolation>& violations);

    /**
     * @brief Generate all trade recommendations
     * @return Vector of trade recommendations sorted by profit ratio (best first)
     */
    std::vector<TradeRecommendation> generateRecommendations() const;

    /**
     * @brief Print human-readable profit report
     */
    static void printReport(const std::vector<TradeRecommendation>& trades);

    /**
     * @brief Convert all recommendations to JSON
     */
    static nlohmann::json toJson(const std::vector<TradeRecommendation>& trades);

private:
    const VolSurface& surface_;
    const std::vector<ArbViolation>& violations_;

    // Strategy generators
    std::vector<TradeRecommendation> generateButterflyTrades() const;
    std::vector<TradeRecommendation> generateCalendarTrades() const;
    std::vector<TradeRecommendation> generateVerticalTrades() const;

    // Helpers
    double callPrice(double K, double T) const;
    double putPrice(double K, double T) const;
    
    /// Find nearest grid strikes around a target strike
    bool findAdjacentStrikes(double targetK, double& K_lower, double& K_upper) const;
    
    /// Find nearest grid expiry after a target
    bool findNextExpiry(double targetT, double& T_next) const;

    /// Assign urgency based on severity
    static std::string classifyUrgency(double severity);
};
