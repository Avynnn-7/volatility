#pragma once
#include "vol_surface.hpp"
#include <vector>
#include <Eigen/Dense>

// SVI parameterization for a single expiry
struct SVIParams {
    double a;  // vertical shift
    double b;  // slope at infinity  
    double rho; // correlation (-1 <= rho <= 1)
    double m;  // log-moneyness at minimum variance
    double sigma; // curvature at minimum variance
    
    // Validate parameters for no-arbitrage
    bool isValid() const;
    double totalVariance(double logMoneyness) const;
    double impliedVol(double logMoneyness, double expiry) const;
};

// SVI volatility surface with proper arbitrage constraints
class SVISurface {
public:
    explicit SVISurface(const std::vector<Quote>& quotes, const MarketData& marketData);
    
    // Interpolate implied volatility at any point
    double impliedVol(double strike, double expiry) const;
    
    // Accessors
    const MarketData& marketData() const { return marketData_; }
    const std::vector<double>& expiries() const { return expiries_; }
    const std::vector<SVIParams>& sviParams() const { return sviParams_; }
    
    // Check arbitrage constraints
    bool isArbitrageFree() const;
    std::vector<std::string> getArbitrageViolations() const;
    
    // Print surface information
    void print() const;

private:
    MarketData marketData_;
    std::vector<double> expiries_;
    std::vector<SVIParams> sviParams_;
    
    // Fit SVI parameters for a single expiry
    SVIParams fitSVI(const std::vector<Quote>& quotes, double expiry) const;
    
    // Calibrate parameters using least squares
    SVIParams calibrateSVI(const std::vector<std::pair<double, double>>& logMoneynessVariance) const;
    
    // Enforce arbitrage constraints
    SVIParams enforceArbitrageConstraints(const SVIParams& params, double expiry) const;
    
    // Helper functions
    double weightFunction(double logMoneyness) const;
    double forwardPrice(double expiry) const;
};
