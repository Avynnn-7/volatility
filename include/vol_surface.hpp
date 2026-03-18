#pragma once
#include <vector>
#include <string>
#include <Eigen/Dense>

// Market data with proper financial parameters
struct Quote {
    double strike;     // absolute strike price
    double expiry;     // time to expiry in years
    double iv;         // implied volatility (e.g. 0.20 = 20%)
    double bid = 0.0;  // bid price (optional)
    double ask = 0.0;  // ask price (optional)
    double volume = 0.0; // trading volume
};

// Market environment parameters
struct MarketData {
    double spot;           // current spot price
    double riskFreeRate;   // continuously compounded risk-free rate
    double dividendYield;  // continuously compounded dividend yield
    std::string valuationDate; // valuation date (YYYY-MM-DD)
    std::string currency;  // currency code
};

// Volatility surface: a grid of implied vols indexed by (expiry, strike)
class VolSurface {
public:
    // Build surface from raw quotes + market data
    explicit VolSurface(const std::vector<Quote>& quotes, const MarketData& marketData);

    // Bilinear interpolation of IV at arbitrary (K, T)
    double impliedVol(double strike, double expiry) const;

    // Convert IV to properly discounted call price via Black-Scholes
    double callPrice(double strike, double expiry) const;
    double putPrice(double strike, double expiry) const;

    // Accessors
    const std::vector<double>& strikes()  const { return strikes_; }
    const std::vector<double>& expiries() const { return expiries_; }
    double spot() const { return marketData_.spot; }
    const MarketData& marketData() const { return marketData_; }

    // Surface matrix: rows = expiry index, cols = strike index
    const Eigen::MatrixXd& ivGrid() const { return ivGrid_; }

    // Forward price and discount factor
    double forward(double expiry) const;
    double discountFactor(double expiry) const;

    // Pretty-print the surface grid
    void print() const;

private:
    MarketData marketData_;
    std::vector<double> strikes_;   // sorted unique strikes
    std::vector<double> expiries_;  // sorted unique expiries
    Eigen::MatrixXd ivGrid_;        // ivGrid_(i,j) = IV at (expiries_[i], strikes_[j])

    // Black-Scholes formulas with proper rates and dividends
    static double bsCall(double S, double K, double T, double sigma, double r, double q);
    static double bsPut(double S, double K, double T, double sigma, double r, double q);
    static double normalCDF(double x);
    static double normalPDF(double x);
};
