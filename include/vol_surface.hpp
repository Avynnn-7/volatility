/**
 * @file vol_surface.hpp
 * @brief Implied volatility surface construction and interpolation
 * @author vol_arb Team
 * @version 2.0
 * @date 2024
 *
 * This file provides the core volatility surface functionality including:
 * - Market data structures (Quote, MarketData)
 * - Bilinear interpolation of implied volatility
 * - Black-Scholes option pricing with proper discounting
 * - LRU cache for high-frequency queries
 *
 * @note The surface assumes a regular grid structure. Scattered data should
 *       be pre-processed to a grid before construction.
 *
 * @see SVISurface for SVI parameterized surfaces
 * @see ArbitrageDetector for arbitrage checking
 */

#pragma once
#include <vector>
#include <string>
#include <Eigen/Dense>
#include <unordered_map>
#include <list>
#include <shared_mutex>

/**
 * @brief Market quote data for a single option
 *
 * Contains all relevant information for a quoted option including
 * strike, expiry, implied volatility, and optional market data.
 *
 * @code
 * Quote q;
 * q.strike = 100.0;
 * q.expiry = 0.25;  // 3 months
 * q.iv = 0.20;      // 20% volatility
 * q.bid = 0.19;
 * q.ask = 0.21;
 * q.volume = 1500;
 * @endcode
 */
struct Quote {
    double strike;      ///< Absolute strike price (must be positive)
    double expiry;      ///< Time to expiry in years (must be positive)
    double iv;          ///< Implied volatility as decimal (e.g., 0.20 = 20%)
    double bid = 0.0;   ///< Bid implied volatility (optional)
    double ask = 0.0;   ///< Ask implied volatility (optional)
    double volume = 0.0; ///< Trading volume for weighting (optional)
};

/**
 * @brief Market environment parameters
 *
 * Contains the global market parameters required for option pricing,
 * including spot price, interest rates, and dividend yield.
 *
 * @code
 * MarketData md;
 * md.spot = 100.0;
 * md.riskFreeRate = 0.05;     // 5% annual
 * md.dividendYield = 0.02;    // 2% dividend yield
 * md.valuationDate = "2024-01-15";
 * md.currency = "USD";
 * @endcode
 */
struct MarketData {
    double spot;            ///< Current spot price of the underlying
    double riskFreeRate;    ///< Continuously compounded risk-free rate
    double dividendYield;   ///< Continuously compounded dividend yield
    std::string valuationDate; ///< Valuation date in YYYY-MM-DD format
    std::string currency;   ///< ISO 4217 currency code (e.g., "USD", "EUR")
};

/**
 * @brief Implied volatility surface with bilinear interpolation
 *
 * The VolSurface class constructs a regular grid of implied volatilities
 * from market quotes and provides interpolation capabilities. It also
 * includes Black-Scholes pricing with proper discounting.
 *
 * ## Key Features
 * - Bilinear interpolation for smooth surface queries
 * - Black-Scholes call/put pricing with rates and dividends
 * - LRU cache for high-frequency queries (configurable size)
 * - Thread-safe read operations
 *
 * ## Example Usage
 * @code
 * std::vector<Quote> quotes = loadQuotes("market_data.json");
 * MarketData marketData = {100.0, 0.05, 0.02, "2024-01-15", "USD"};
 *
 * VolSurface surface(quotes, marketData);
 *
 * // Get implied volatility at arbitrary point
 * double iv = surface.impliedVol(105.0, 0.5);  // K=105, T=0.5yr
 *
 * // Get call price
 * double price = surface.callPrice(105.0, 0.5);
 * @endcode
 *
 * @warning This class does NOT enforce arbitrage-free conditions. Use
 *          ArbitrageDetector and QPSolver to ensure arbitrage-freeness.
 *
 * @see ArbitrageDetector for violation detection
 * @see QPSolver for surface correction
 */
class VolSurface {
public:
    /**
     * @brief LRU cache performance statistics
     *
     * Tracks cache hits, misses, and evictions for performance monitoring.
     */
    struct CacheStats {
        size_t hits = 0;       ///< Number of cache hits
        size_t misses = 0;     ///< Number of cache misses
        size_t evictions = 0;  ///< Number of cache evictions
        
        /**
         * @brief Calculate cache hit rate
         * @return Hit rate as fraction [0,1], or 0 if no queries
         */
        double hitRate() const { 
            return (hits + misses > 0) ? 
                   static_cast<double>(hits) / (hits + misses) : 0.0; 
        }
    };
    
    /**
     * @brief Get current cache statistics
     * @return CacheStats struct with performance metrics
     */
    CacheStats getCacheStats() const;
    
    /**
     * @brief Clear the interpolation cache
     *
     * Removes all cached values. Useful after surface modification
     * or to free memory.
     */
    void clearCache();
    
    /**
     * @brief Set maximum cache size
     * @param maxEntries Maximum number of cached interpolations
     *
     * When the cache exceeds this size, least-recently-used entries
     * are evicted. Default is 4096.
     */
    void setCacheSize(size_t maxEntries);

    /**
     * @brief Construct volatility surface from market quotes
     *
     * @param quotes Vector of option quotes (strike, expiry, IV)
     * @param marketData Market environment (spot, rates, etc.)
     *
     * @throws std::invalid_argument if quotes is empty
     * @throws std::invalid_argument if spot price is non-positive
     *
     * The constructor extracts unique strikes and expiries, creating
     * a regular grid. Missing grid points are interpolated.
     */
    explicit VolSurface(const std::vector<Quote>& quotes, const MarketData& marketData);

    /**
     * @brief Interpolate implied volatility at arbitrary point
     *
     * @param strike Strike price (must be positive)
     * @param expiry Time to expiry in years (must be positive)
     * @return Implied volatility at (strike, expiry)
     *
     * Uses bilinear interpolation within the grid and linear
     * extrapolation outside. Results are cached for performance.
     *
     * @note Thread-safe for concurrent read access
     */
    double impliedVol(double strike, double expiry) const;

    /**
     * @brief Calculate Black-Scholes call price
     *
     * @param strike Strike price
     * @param expiry Time to expiry in years
     * @return Discounted call option price
     *
     * Computes C = S*exp(-qT)*N(d1) - K*exp(-rT)*N(d2)
     * using the interpolated implied volatility.
     */
    double callPrice(double strike, double expiry) const;
    
    /**
     * @brief Calculate Black-Scholes put price
     *
     * @param strike Strike price
     * @param expiry Time to expiry in years
     * @return Discounted put option price
     *
     * Computes P = K*exp(-rT)*N(-d2) - S*exp(-qT)*N(-d1)
     * using the interpolated implied volatility.
     */
    double putPrice(double strike, double expiry) const;

    /**
     * @brief Get sorted list of unique strikes
     * @return Const reference to strikes vector
     */
    const std::vector<double>& strikes() const { return strikes_; }
    
    /**
     * @brief Get sorted list of unique expiries
     * @return Const reference to expiries vector
     */
    const std::vector<double>& expiries() const { return expiries_; }
    
    /**
     * @brief Get spot price
     * @return Current spot price of the underlying
     */
    double spot() const { return marketData_.spot; }
    
    /**
     * @brief Get full market data
     * @return Const reference to MarketData struct
     */
    const MarketData& marketData() const { return marketData_; }

    /**
     * @brief Get raw IV grid matrix
     * @return Const reference to IV grid (rows=expiry, cols=strike)
     *
     * Element ivGrid_(i,j) contains the IV at (expiries_[i], strikes_[j])
     */
    const Eigen::MatrixXd& ivGrid() const { return ivGrid_; }

    /**
     * @brief Calculate forward price for given expiry
     *
     * @param expiry Time to expiry in years
     * @return Forward price F = S * exp((r-q)*T)
     */
    double forward(double expiry) const;
    
    /**
     * @brief Calculate discount factor for given expiry
     *
     * @param expiry Time to expiry in years
     * @return Discount factor D = exp(-r*T)
     */
    double discountFactor(double expiry) const;

    /**
     * @brief Print surface grid to stdout
     *
     * Displays a formatted table of the IV grid with strikes
     * as columns and expiries as rows.
     */
    void print() const;

private:
    MarketData marketData_;          ///< Market environment parameters
    std::vector<double> strikes_;    ///< Sorted unique strike prices
    std::vector<double> expiries_;   ///< Sorted unique expiry times
    Eigen::MatrixXd ivGrid_;         ///< IV grid: ivGrid_(i,j) = IV at (T_i, K_j)

    // Black-Scholes formulas with proper rates and dividends
    static double bsCall(double S, double K, double T, double sigma, double r, double q);
    static double bsPut(double S, double K, double T, double sigma, double r, double q);
    static double normalCDF(double x);
    static double normalPDF(double x);
    
    // LRU Cache Implementation
    struct CacheKey {
        double strike;
        double expiry;
        enum class Type { IV, CallPrice, PutPrice } type;
        
        bool operator==(const CacheKey& other) const {
            return strike == other.strike && 
                   expiry == other.expiry && 
                   type == other.type;
        }
    };
    
    struct CacheKeyHash {
        size_t operator()(const CacheKey& key) const {
            size_t h1 = std::hash<double>{}(key.strike);
            size_t h2 = std::hash<double>{}(key.expiry);
            size_t h3 = std::hash<int>{}(static_cast<int>(key.type));
            return h1 ^ (h2 * 0x9e3779b97f4a7c15ULL) ^ (h3 * 0x517cc1b727220a95ULL);
        }
    };
    
    using CacheList = std::list<std::pair<CacheKey, double>>;
    using CacheMap = std::unordered_map<CacheKey, CacheList::iterator, CacheKeyHash>;
    
    mutable CacheList cacheList_;           ///< LRU order (front = most recent)
    mutable CacheMap cacheMap_;             ///< O(1) key lookup
    mutable std::shared_mutex cacheMutex_;  ///< Thread safety for cache
    mutable CacheStats cacheStats_;         ///< Performance statistics
    size_t maxCacheSize_ = 4096;            ///< Maximum cache entries
    
    // Cache operations (thread-safe)
    bool getCached(const CacheKey& key, double& value) const;
    void putCache(const CacheKey& key, double value) const;
    
    // Uncached implementations (called on cache miss)
    double impliedVolUncached(double strike, double expiry) const;
    double callPriceUncached(double strike, double expiry) const;
    double putPriceUncached(double strike, double expiry) const;
};
