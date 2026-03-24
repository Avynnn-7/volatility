/**
 * @file profit_advisor.cpp
 * @brief Implementation of ProfitAdvisor - generates trade recommendations from arb violations
 */

#include "profit_advisor.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <set>

// ── TradeLeg ─────────────────────────────────────────────────────────────────

nlohmann::json TradeLeg::toJson() const {
    return {
        {"action",     action == Action::BUY ? "BUY" : "SELL"},
        {"optionType", optionType == OptionType::CALL ? "CALL" : "PUT"},
        {"strike",     strike},
        {"expiry",     expiry},
        {"quantity",   quantity},
        {"price",      price}
    };
}

// ── TradeRecommendation ──────────────────────────────────────────────────────

nlohmann::json TradeRecommendation::toJson() const {
    nlohmann::json j;
    j["name"]           = name;
    j["strategy"]       = strategy;
    j["description"]    = description;
    j["expectedProfit"] = expectedProfit;
    j["maxRisk"]        = maxRisk;
    j["netCost"]        = netCost;
    j["profitRatio"]    = profitRatio;
    j["severity"]       = severity;
    j["urgency"]        = urgency;

    j["legs"] = nlohmann::json::array();
    for (const auto& leg : legs)
        j["legs"].push_back(leg.toJson());

    return j;
}

// ── ProfitAdvisor Construction ───────────────────────────────────────────────

ProfitAdvisor::ProfitAdvisor(const VolSurface& surface,
                             const std::vector<ArbViolation>& violations)
    : surface_(surface), violations_(violations) {}

// ── Helpers ──────────────────────────────────────────────────────────────────

double ProfitAdvisor::callPrice(double K, double T) const {
    return surface_.callPrice(K, T);
}

double ProfitAdvisor::putPrice(double K, double T) const {
    return surface_.putPrice(K, T);
}

bool ProfitAdvisor::findAdjacentStrikes(double targetK,
                                         double& K_lower, double& K_upper) const {
    const auto& strikes = surface_.strikes();
    if (strikes.size() < 2) return false;

    // Find the position where targetK would be inserted
    auto it = std::lower_bound(strikes.begin(), strikes.end(), targetK);
    
    if (it == strikes.begin()) {
        K_lower = strikes[0];
        K_upper = strikes[1];
        return true;
    }
    if (it == strikes.end()) {
        K_lower = strikes[strikes.size() - 2];
        K_upper = strikes[strikes.size() - 1];
        return true;
    }

    K_upper = *it;
    K_lower = *(it - 1);
    return true;
}

bool ProfitAdvisor::findNextExpiry(double targetT, double& T_next) const {
    const auto& expiries = surface_.expiries();
    for (const double t : expiries) {
        if (t > targetT + 1e-8) {
            T_next = t;
            return true;
        }
    }
    return false;
}

std::string ProfitAdvisor::classifyUrgency(double severity) {
    if (severity > 0.7)  return "HIGH";
    if (severity > 0.3)  return "MEDIUM";
    return "LOW";
}

// ── Butterfly Trade Generator ────────────────────────────────────────────────
// A butterfly violation means the risk-neutral density is negative at some point.
// We exploit this by SELLING the expensive butterfly:
//   Sell 1x Call(K_mid) + Buy 0.5x Call(K_low) + Buy 0.5x Call(K_high)
// or the integer-scaled version:
//   Buy 1x Call(K_low), Sell 2x Call(K_mid), Buy 1x Call(K_high)
// When the density is negative, the market overprices the ATM option relative
// to the wings, so selling the butterfly earns a credit that exceeds the max payout.

std::vector<TradeRecommendation> ProfitAdvisor::generateButterflyTrades() const {
    std::vector<TradeRecommendation> trades;
    const auto& strikes = surface_.strikes();

    for (const auto& v : violations_) {
        if (v.type != ArbType::ButterflyViolation) continue;
        if (strikes.size() < 3) continue;

        double K_mid = v.strike;
        double T     = v.expiry;

        // Find K_low and K_high around K_mid on the grid
        int midIdx = -1;
        for (int i = 0; i < (int)strikes.size(); ++i) {
            if (std::abs(strikes[i] - K_mid) < 1e-6) {
                midIdx = i;
                break;
            }
        }
        if (midIdx < 1 || midIdx >= (int)strikes.size() - 1) continue;

        double K_low  = strikes[midIdx - 1];
        double K_high = strikes[midIdx + 1];

        double C_low  = callPrice(K_low,  T);
        double C_mid  = callPrice(K_mid,  T);
        double C_high = callPrice(K_high, T);

        // Butterfly value = C(K_low) - 2*C(K_mid) + C(K_high)
        // Should be >= 0. If negative, we can sell it for a credit.
        double butterflyVal = C_low - 2.0 * C_mid + C_high;

        // If butterfly has negative theoretical value, exploiting means SELLING it
        // (receiving a net credit from the mispricing)
        if (butterflyVal >= -1e-10) continue;  // No arb here

        // The trade: SELL the butterfly (sell wings, buy middle)
        // Net credit = |butterflyVal|
        // Max payout at expiry = max spacing between strikes (worst case)
        double maxPayout = std::min(K_mid - K_low, K_high - K_mid);
        double netCredit = std::abs(butterflyVal);
        double maxLoss   = maxPayout - netCredit;  // If butterfly finishes ITM
        
        if (maxLoss <= 0) {
            // Pure arb: credit exceeds max payout → riskless profit
            maxLoss = 0.0;
        }

        TradeRecommendation rec;
        rec.strategy = "BUTTERFLY";
        
        std::ostringstream nameStream;
        nameStream << "Sell Butterfly K=" << std::fixed << std::setprecision(1)
                   << K_low << "/" << K_mid << "/" << K_high
                   << " T=" << std::setprecision(3) << T;
        rec.name = nameStream.str();
        
        std::ostringstream descStream;
        descStream << "The market overprices the K=" << std::setprecision(1) << K_mid
                   << " call relative to the wings. Sell the butterfly to collect "
                   << std::setprecision(4) << netCredit << " credit per unit. "
                   << "This exploits a negative risk-neutral density (magnitude: "
                   << std::setprecision(6) << std::abs(v.magnitude) << ").";
        rec.description = descStream.str();

        // Legs: Sell butterfly = Buy low wing, Sell 2x middle, Buy high wing
        rec.legs.push_back({TradeLeg::Action::BUY,  TradeLeg::OptionType::CALL,
                           K_low, T, 1, C_low});
        rec.legs.push_back({TradeLeg::Action::SELL, TradeLeg::OptionType::CALL,
                           K_mid, T, 2, C_mid});
        rec.legs.push_back({TradeLeg::Action::BUY,  TradeLeg::OptionType::CALL,
                           K_high, T, 1, C_high});

        rec.expectedProfit    = netCredit;
        rec.maxRisk           = maxLoss;
        rec.netCost           = -netCredit;  // Negative = credit received
        rec.profitRatio       = (maxLoss > 1e-10) ? netCredit / maxLoss : 999.0;
        rec.severity          = v.severityScore();
        rec.urgency           = classifyUrgency(rec.severity);
        rec.violationType     = v.type;
        rec.violationMagnitude = v.magnitude;

        trades.push_back(rec);
    }
    return trades;
}

// ── Calendar Trade Generator ─────────────────────────────────────────────────
// A calendar violation means call price decreases with maturity at some strike.
// Exploit: Buy the cheap far-dated option, sell the expensive near-dated option.
// This is a "reverse calendar spread" that captures the mispricing.

std::vector<TradeRecommendation> ProfitAdvisor::generateCalendarTrades() const {
    std::vector<TradeRecommendation> trades;
    const auto& expiries = surface_.expiries();

    for (const auto& v : violations_) {
        if (v.type != ArbType::CalendarViolation) continue;
        if (expiries.size() < 2) continue;

        double K = v.strike;
        double T_near = v.expiry;
        
        // Find the next expiry
        double T_far = 0;
        if (!findNextExpiry(T_near, T_far)) continue;

        double C_near = callPrice(K, T_near);
        double C_far  = callPrice(K, T_far);

        // Calendar violation: C_far < C_near (far option cheaper than near)
        // Exploit: Buy far, sell near → net credit = C_near - C_far
        double spread = C_near - C_far;
        if (spread <= 1e-10) continue;  // Not actually violated

        // The near-dated option will decay faster, so the spread converges
        // to the far-dated option value at near expiry
        double netCredit = spread;
        // Max risk: the far-dated option could lose all value, but we collected credit
        double maxLoss = C_far;  // Worst case: far option worthless, near finishes ITM

        TradeRecommendation rec;
        rec.strategy = "CALENDAR";

        std::ostringstream nameStream;
        nameStream << "Calendar Spread K=" << std::fixed << std::setprecision(1) << K
                   << " T=" << std::setprecision(3) << T_near << "/" << T_far;
        rec.name = nameStream.str();

        std::ostringstream descStream;
        descStream << "The near-dated option (T=" << std::setprecision(3) << T_near
                   << ") is priced higher than the far-dated (T=" << T_far
                   << ") at K=" << std::setprecision(1) << K
                   << ". Sell near, buy far to collect " << std::setprecision(4)
                   << netCredit << " credit. Calendar spread violation magnitude: "
                   << std::setprecision(6) << std::abs(v.magnitude) << ".";
        rec.description = descStream.str();

        rec.legs.push_back({TradeLeg::Action::SELL, TradeLeg::OptionType::CALL,
                           K, T_near, 1, C_near});
        rec.legs.push_back({TradeLeg::Action::BUY,  TradeLeg::OptionType::CALL,
                           K, T_far,  1, C_far});

        rec.expectedProfit    = netCredit;
        rec.maxRisk           = maxLoss;
        rec.netCost           = -netCredit;
        rec.profitRatio       = (maxLoss > 1e-10) ? netCredit / maxLoss : 999.0;
        rec.severity          = v.severityScore();
        rec.urgency           = classifyUrgency(rec.severity);
        rec.violationType     = v.type;
        rec.violationMagnitude = v.magnitude;

        trades.push_back(rec);
    }
    return trades;
}

// ── Vertical Spread Trade Generator ──────────────────────────────────────────
// A monotonicity violation means call price increases with strike at some point.
// Exploit: Buy the cheaper lower-strike call, sell the expensive higher-strike call.
// This is guaranteed profit since the lower-strike call dominates.

std::vector<TradeRecommendation> ProfitAdvisor::generateVerticalTrades() const {
    std::vector<TradeRecommendation> trades;
    const auto& strikes = surface_.strikes();

    for (const auto& v : violations_) {
        if (v.type != ArbType::MonotonicityViolation &&
            v.type != ArbType::VerticalSpreadViolation) continue;
        if (strikes.size() < 2) continue;

        double K = v.strike;
        double T = v.expiry;

        // Find adjacent strikes
        double K_low, K_high;
        if (!findAdjacentStrikes(K, K_low, K_high)) continue;

        double C_low  = callPrice(K_low, T);
        double C_high = callPrice(K_high, T);

        // Monotonicity: C_high > C_low would be a violation (should decrease with K)
        // But also check: C_low - C_high should not exceed (K_high - K_low)*DF
        double df = surface_.discountFactor(T);
        
        // Case 1: Call price increases with strike (directional violation)
        if (C_high > C_low + 1e-10) {
            double profit = C_high - C_low;
            // Buy cheap low-strike call, sell expensive high-strike call
            
            TradeRecommendation rec;
            rec.strategy = "VERTICAL";

            std::ostringstream nameStream;
            nameStream << "Bull Call Spread K=" << std::fixed << std::setprecision(1)
                       << K_low << "/" << K_high << " T=" << std::setprecision(3) << T;
            rec.name = nameStream.str();

            std::ostringstream descStream;
            descStream << "Call at K=" << std::setprecision(1) << K_high
                       << " is priced HIGHER than K=" << K_low
                       << " (should be lower). Buy low-strike, sell high-strike for "
                       << std::setprecision(4) << profit << " credit. "
                       << "This is a near-riskless trade.";
            rec.description = descStream.str();

            rec.legs.push_back({TradeLeg::Action::BUY,  TradeLeg::OptionType::CALL,
                               K_low, T, 1, C_low});
            rec.legs.push_back({TradeLeg::Action::SELL, TradeLeg::OptionType::CALL,
                               K_high, T, 1, C_high});

            // Max payout = K_high - K_low (if S > K_high)
            // We already received a credit, so profit is guaranteed
            rec.expectedProfit    = profit;
            rec.maxRisk           = 0.0;  // Riskless arb!
            rec.netCost           = -profit;
            rec.profitRatio       = 999.0;
            rec.severity          = v.severityScore();
            rec.urgency           = "HIGH";
            rec.violationType     = v.type;
            rec.violationMagnitude = v.magnitude;

            trades.push_back(rec);
        }
        // Case 2: Spread exceeds discounted strike difference
        else if (C_low - C_high > (K_high - K_low) * df + 1e-10) {
            double overpricing = (C_low - C_high) - (K_high - K_low) * df;
            
            TradeRecommendation rec;
            rec.strategy = "VERTICAL";

            std::ostringstream nameStream;
            nameStream << "Bear Call Spread K=" << std::fixed << std::setprecision(1)
                       << K_low << "/" << K_high << " T=" << std::setprecision(3) << T;
            rec.name = nameStream.str();

            std::ostringstream descStream;
            descStream << "Vertical spread C(" << std::setprecision(1) << K_low
                       << ")-C(" << K_high << ")=" << std::setprecision(4) << (C_low - C_high)
                       << " exceeds max value " << (K_high - K_low) * df
                       << ". Sell low-strike, buy high-strike for "
                       << overpricing << " excess credit.";
            rec.description = descStream.str();

            rec.legs.push_back({TradeLeg::Action::SELL, TradeLeg::OptionType::CALL,
                               K_low, T, 1, C_low});
            rec.legs.push_back({TradeLeg::Action::BUY,  TradeLeg::OptionType::CALL,
                               K_high, T, 1, C_high});

            rec.expectedProfit    = overpricing;
            rec.maxRisk           = 0.0;
            rec.netCost           = -(C_low - C_high);
            rec.profitRatio       = 999.0;
            rec.severity          = v.severityScore();
            rec.urgency           = "HIGH";
            rec.violationType     = v.type;
            rec.violationMagnitude = v.magnitude;

            trades.push_back(rec);
        }
    }
    return trades;
}

// ── Generate All Recommendations ─────────────────────────────────────────────

std::vector<TradeRecommendation> ProfitAdvisor::generateRecommendations() const {
    auto butterfly = generateButterflyTrades();
    auto calendar  = generateCalendarTrades();
    auto vertical  = generateVerticalTrades();

    // Combine all
    std::vector<TradeRecommendation> all;
    all.reserve(butterfly.size() + calendar.size() + vertical.size());
    all.insert(all.end(), butterfly.begin(), butterfly.end());
    all.insert(all.end(), calendar.begin(),  calendar.end());
    all.insert(all.end(), vertical.begin(),  vertical.end());

    // Sort by profit ratio (best first), then by severity
    std::sort(all.begin(), all.end(), [](const TradeRecommendation& a,
                                         const TradeRecommendation& b) {
        if (std::abs(a.profitRatio - b.profitRatio) > 1e-6)
            return a.profitRatio > b.profitRatio;
        return a.severity > b.severity;
    });

    return all;
}

// ── JSON Output ──────────────────────────────────────────────────────────────

nlohmann::json ProfitAdvisor::toJson(const std::vector<TradeRecommendation>& trades) {
    nlohmann::json j;
    j["totalRecommendations"] = trades.size();

    int highCount = 0, medCount = 0, lowCount = 0;
    double totalProfit = 0;
    for (const auto& t : trades) {
        if (t.urgency == "HIGH")   highCount++;
        else if (t.urgency == "MEDIUM") medCount++;
        else lowCount++;
        totalProfit += t.expectedProfit;
    }

    j["summary"] = {
        {"highUrgency",   highCount},
        {"mediumUrgency", medCount},
        {"lowUrgency",    lowCount},
        {"totalExpectedProfit", totalProfit}
    };

    j["recommendations"] = nlohmann::json::array();
    for (const auto& t : trades) {
        j["recommendations"].push_back(t.toJson());
    }

    return j;
}

// ── Human-Readable Report ────────────────────────────────────────────────────

void ProfitAdvisor::printReport(const std::vector<TradeRecommendation>& trades) {
    std::cout << "\n╔══════════════════════════════════════════════════════════════════╗\n";
    std::cout <<   "║              PROFIT ADVISOR — Trade Recommendations            ║\n";
    std::cout <<   "╠══════════════════════════════════════════════════════════════════╣\n";

    if (trades.empty()) {
        std::cout << "║  No exploitable arbitrage opportunities found.                 ║\n";
        std::cout << "╚══════════════════════════════════════════════════════════════════╝\n";
        return;
    }

    // Summary
    int highCount = 0, medCount = 0, lowCount = 0;
    double totalProfit = 0;
    for (const auto& t : trades) {
        if (t.urgency == "HIGH")   highCount++;
        else if (t.urgency == "MEDIUM") medCount++;
        else lowCount++;
        totalProfit += t.expectedProfit;
    }

    std::cout << "║  Found " << trades.size() << " trade opportunities";
    if (highCount > 0) std::cout << "  [" << highCount << " HIGH]";
    if (medCount > 0)  std::cout << " [" << medCount  << " MED]";
    if (lowCount > 0)  std::cout << " [" << lowCount  << " LOW]";
    std::cout << "\n";
    std::cout << "║  Total expected profit: " << std::fixed << std::setprecision(4)
              << totalProfit << "\n";
    std::cout << "╠══════════════════════════════════════════════════════════════════╣\n";

    int tradeNum = 1;
    for (const auto& trade : trades) {
        std::string urgencyIcon;
        if (trade.urgency == "HIGH")        urgencyIcon = "🔴";
        else if (trade.urgency == "MEDIUM") urgencyIcon = "🟡";
        else                                urgencyIcon = "🟢";

        std::cout << "\n── Trade #" << tradeNum++ << " " << urgencyIcon
                  << " [" << trade.urgency << "] ────────────────────────\n";
        std::cout << "   Strategy    : " << trade.strategy << "\n";
        std::cout << "   Name        : " << trade.name << "\n";
        std::cout << "   Description : " << trade.description << "\n\n";
        std::cout << "   Legs:\n";

        for (const auto& leg : trade.legs) {
            std::cout << "     " 
                      << (leg.action == TradeLeg::Action::BUY ? "BUY " : "SELL")
                      << " " << leg.quantity << "x "
                      << (leg.optionType == TradeLeg::OptionType::CALL ? "CALL" : "PUT ")
                      << "  K=" << std::setw(8) << std::setprecision(1) << leg.strike
                      << "  T=" << std::setprecision(3) << leg.expiry
                      << "  @ " << std::setprecision(4) << leg.price << "\n";
        }

        std::cout << "\n   P&L Analysis:\n";
        std::cout << "     Expected Profit : " << std::setprecision(4) << trade.expectedProfit << "\n";
        std::cout << "     Max Risk        : " << (trade.maxRisk < 1e-8 ? "NONE (riskless)" :
                     std::to_string(trade.maxRisk)) << "\n";
        std::cout << "     Net Cost        : " << trade.netCost
                  << (trade.netCost < 0 ? " (CREDIT)" : " (DEBIT)") << "\n";
        std::cout << "     Profit/Risk     : " << (trade.profitRatio > 100 ? "∞ (riskless)" :
                     std::to_string(trade.profitRatio)) << "\n";
    }

    std::cout << "\n╚══════════════════════════════════════════════════════════════════╝\n";
}
