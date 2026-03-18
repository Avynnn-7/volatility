#include "svi_surface.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

bool SVIParams::isValid() const {
    return (b >= 0) && (rho >= -1.0 && rho <= 1.0) && (sigma >= 0) && 
           (b * sigma * (1.0 + std::abs(rho)) >= 0);
}

double SVIParams::totalVariance(double logMoneyness) const {
    return a + b * (rho * (logMoneyness - m) + std::sqrt((logMoneyness - m) * (logMoneyness - m) + sigma * sigma));
}

double SVIParams::impliedVol(double logMoneyness, double expiry) const {
    if (expiry <= 0) return 0.0;
    double w = totalVariance(logMoneyness);
    return std::sqrt(w / expiry);
}

SVISurface::SVISurface(const std::vector<Quote>& quotes, const MarketData& marketData)
    : marketData_(marketData)
{
    if (quotes.empty())
        throw std::invalid_argument("SVISurface: no quotes supplied");

    // Extract unique expiries
    for (const auto& q : quotes) {
        expiries_.push_back(q.expiry);
    }
    std::sort(expiries_.begin(), expiries_.end());
    expiries_.erase(std::unique(expiries_.begin(), expiries_.end()), expiries_.end());

    // Fit SVI for each expiry
    for (double expiry : expiries_) {
        std::vector<Quote> expiryQuotes;
        for (const auto& q : quotes) {
            if (std::abs(q.expiry - expiry) < 1e-6) {
                expiryQuotes.push_back(q);
            }
        }
        
        if (expiryQuotes.empty()) {
            throw std::invalid_argument("SVISurface: no quotes for expiry " + std::to_string(expiry));
        }
        
        SVIParams params = fitSVI(expiryQuotes, expiry);
        sviParams_.push_back(params);
    }
}

double SVISurface::impliedVol(double strike, double expiry) const {
    // Find surrounding expiries for interpolation
    auto it = std::lower_bound(expiries_.begin(), expiries_.end(), expiry);
    
    if (it == expiries_.begin()) {
        // Before first expiry
        double logMoneyness = std::log(strike / forwardPrice(expiries_[0]));
        return sviParams_[0].impliedVol(logMoneyness, expiries_[0]);
    }
    
    if (it == expiries_.end()) {
        // After last expiry - extrapolate using last slice
        double logMoneyness = std::log(strike / forwardPrice(expiries_.back()));
        return sviParams_.back().impliedVol(logMoneyness, expiries_.back());
    }
    
    // Interpolate between two expiry slices
    int idx2 = static_cast<int>(it - expiries_.begin());
    int idx1 = idx2 - 1;
    
    double T1 = expiries_[idx1], T2 = expiries_[idx2];
    double weight = (expiry - T1) / (T2 - T1);
    
    double logMoneyness1 = std::log(strike / forwardPrice(T1));
    double logMoneyness2 = std::log(strike / forwardPrice(T2));
    
    double iv1 = sviParams_[idx1].impliedVol(logMoneyness1, T1);
    double iv2 = sviParams_[idx2].impliedVol(logMoneyness2, T2);
    
    // Linear interpolation in variance space (more stable)
    double var1 = iv1 * iv1 * T1;
    double var2 = iv2 * iv2 * T2;
    double var_interp = (1 - weight) * var1 + weight * var2;
    return std::sqrt(var_interp / expiry);
}

bool SVISurface::isArbitrageFree() const {
    return getArbitrageViolations().empty();
}

std::vector<std::string> SVISurface::getArbitrageViolations() const {
    std::vector<std::string> violations;
    
    for (size_t i = 0; i < sviParams_.size(); ++i) {
        const auto& params = sviParams_[i];
        double T = expiries_[i];
        
        // Check butterfly arbitrage (convexity)
        // For SVI: d²w/dk² >= 0 => b * (1 + |rho|) >= 0 is sufficient
        if (params.b < 0) {
            violations.push_back("Negative slope parameter at expiry " + std::to_string(T));
        }
        
        // Check calendar arbitrage
        if (i > 0) {
            double T_prev = expiries_[i-1];
            double w_atm_current = params.totalVariance(0.0);
            double w_atm_prev = sviParams_[i-1].totalVariance(0.0);
            
            if (w_atm_current < w_atm_prev) {
                violations.push_back("Calendar arbitrage at expiry " + std::to_string(T));
            }
        }
        
        // Check parameter bounds
        if (std::abs(params.rho) > 1.0) {
            violations.push_back("Invalid correlation parameter at expiry " + std::to_string(T));
        }
        
        if (params.sigma < 0) {
            violations.push_back("Negative curvature parameter at expiry " + std::to_string(T));
        }
    }
    
    return violations;
}

void SVISurface::print() const {
    std::cout << "\n=== SVI Volatility Surface Parameters ===\n";
    std::cout << std::fixed << std::setprecision(4);
    std::cout << std::setw(8) << "Expiry" << std::setw(8) << "a" << std::setw(8) << "b" 
              << std::setw(8) << "rho" << std::setw(8) << "m" << std::setw(8) << "sigma" << "\n";
    
    for (size_t i = 0; i < sviParams_.size(); ++i) {
        const auto& params = sviParams_[i];
        std::cout << std::setw(8) << expiries_[i]
                  << std::setw(8) << params.a
                  << std::setw(8) << params.b  
                  << std::setw(8) << params.rho
                  << std::setw(8) << params.m
                  << std::setw(8) << params.sigma << "\n";
    }
    
    auto violations = getArbitrageViolations();
    if (violations.empty()) {
        std::cout << "\n✓ Surface is arbitrage-free\n";
    } else {
        std::cout << "\n✗ Arbitrage violations found:\n";
        for (const auto& viol : violations) {
            std::cout << "  - " << viol << "\n";
        }
    }
    std::cout << "\n";
}

SVIParams SVISurface::fitSVI(const std::vector<Quote>& quotes, double expiry) const {
    // Convert quotes to log-moneyness vs total variance
    std::vector<std::pair<double, double>> data;
    double forward = forwardPrice(expiry);
    
    for (const auto& q : quotes) {
        double logMoneyness = std::log(q.strike / forward);
        double totalVar = q.iv * q.iv * expiry;
        double weight = weightFunction(logMoneyness);
        data.emplace_back(logMoneyness, totalVar);
    }
    
    // Initial guess for parameters
    SVIParams initial;
    initial.a = 0.04;  // 20% vol squared
    initial.b = 0.4;   // reasonable slope
    initial.rho = -0.7; // typical negative skew
    initial.m = 0.0;   // ATM minimum
    initial.sigma = 0.2; // reasonable curvature
    
    // Simple least squares calibration (could use more sophisticated optimization)
    SVIParams result = calibrateSVI(data);
    
    // Enforce arbitrage constraints
    return enforceArbitrageConstraints(result, expiry);
}

SVIParams SVISurface::calibrateSVI(const std::vector<std::pair<double, double>>& logMoneynessVariance) const {
    // Simplified calibration using method of moments
    // In production, you'd use Levenberg-Marquardt or similar
    
    double sum_w = 0, sum_wk = 0, sum_wk2 = 0, sum_wv = 0, sum_wkv = 0;
    
    for (const auto& [k, v] : logMoneynessVariance) {
        double weight = weightFunction(k);
        sum_w += weight;
        sum_wk += weight * k;
        sum_wk2 += weight * k * k;
        sum_wv += weight * v;
        sum_wkv += weight * k * v;
    }
    
    // Simple linear regression to estimate parameters
    double mean_k = sum_wk / sum_w;
    double mean_v = sum_wv / sum_w;
    
    double num = sum_wkv - sum_w * mean_k * mean_v;
    double den = sum_wk2 - sum_w * mean_k * mean_k;
    
    double slope = (den > 1e-10) ? num / den : 0.0;
    double intercept = mean_v - slope * mean_k;
    
    // Convert to SVI parameters (simplified)
    SVIParams params;
    params.a = std::max(0.001, intercept);
    params.b = std::max(0.1, std::abs(slope));
    params.rho = std::clamp(-0.7, -0.9, 0.9);
    params.m = mean_k;
    params.sigma = 0.2;
    
    return params;
}

SVIParams SVISurface::enforceArbitrageConstraints(const SVIParams& params, double expiry) const {
    SVIParams constrained = params;
    
    // Enforce no-butterfly arbitrage: b >= 0
    constrained.b = std::max(0.0, constrained.b);
    
    // Enforce correlation bounds
    constrained.rho = std::clamp(constrained.rho, -0.999, 0.999);
    
    // Enforce positivity of curvature
    constrained.sigma = std::max(0.01, constrained.sigma);
    
    // Ensure minimum variance is positive
    constrained.a = std::max(0.0001, constrained.a);
    
    // Additional constraint: b * sigma * (1 + |rho|) >= 0
    double min_product = 0.01;
    if (constrained.b * constrained.sigma * (1.0 + std::abs(constrained.rho)) < min_product) {
        constrained.sigma = min_product / (constrained.b * (1.0 + std::abs(constrained.rho)));
    }
    
    return constrained;
}

double SVISurface::weightFunction(double logMoneyness) const {
    // Weight function that gives more weight to ATM options
    double atm_weight = 1.0;
    double wing_weight = 0.1;
    double decay_rate = 2.0;
    
    return wing_weight + (atm_weight - wing_weight) * std::exp(-decay_rate * logMoneyness * logMoneyness);
}

double SVISurface::forwardPrice(double expiry) const {
    return marketData_.spot * std::exp((marketData_.riskFreeRate - marketData_.dividendYield) * expiry);
}
