/**
 * @file svi_surface.hpp
 * @brief SVI (Stochastic Volatility Inspired) parameterization
 * @author vol_arb Team
 * @version 2.0
 * @date 2024
 *
 * Implements the SVI parameterization for implied volatility smiles:
 *
 *     w(k) = a + b(rho*(k-m) + sqrt((k-m)^2 + sigma^2))
 *
 * Where:
 * - w = sigma^2*T is the total variance
 * - k = log(K/F) is the log-moneyness
 * - a, b, rho, m, sigma are the SVI parameters
 *
 * ## References
 * - Gatheral, J. & Jacquier, A. (2014). "Arbitrage-free SVI volatility surfaces"
 * - Zeliade Systems (2009). "Quasi-explicit calibration of Gatheral's SVI model"
 *
 * @see VolSurface for bilinear interpolation alternative
 */

#pragma once
#include "vol_surface.hpp"
#include <vector>
#include <Eigen/Dense>

/**
 * @brief SVI parameters for a single expiry slice
 *
 * The five SVI parameters fully determine the smile shape at one expiry.
 *
 * @note Parameters must satisfy arbitrage-free conditions for valid surface.
 */
struct SVIParams {
    double a;      ///< Vertical shift (overall variance level)
    double b;      ///< Slope at infinity (wing steepness)
    double rho;    ///< Correlation parameter (-1 < rho < 1)
    double m;      ///< Log-moneyness at minimum variance
    double sigma;  ///< Curvature at minimum variance
    
    /**
     * @brief Check if parameters satisfy no-arbitrage conditions
     * @return True if arbitrage-free
     *
     * Conditions:
     * - b >= 0
     * - |rho| < 1
     * - sigma > 0
     * - a + b*sigma*sqrt(1-rho^2) >= 0 (non-negative variance at minimum)
     */
    bool isValid() const;
    
    /**
     * @brief Calculate total variance at log-moneyness k
     * @param logMoneyness k = log(K/F)
     * @return Total variance w = sigma^2*T
     */
    double totalVariance(double logMoneyness) const;
    
    /**
     * @brief Calculate implied volatility at log-moneyness k
     * @param logMoneyness k = log(K/F)
     * @param expiry Time to expiry T
     * @return Implied volatility sigma = sqrt(w/T)
     */
    double impliedVol(double logMoneyness, double expiry) const;
};

/**
 * @brief SVI calibrator using Levenberg-Marquardt algorithm
 *
 * Calibrates SVI parameters to market data using nonlinear least squares
 * with analytical Jacobian for efficiency.
 *
 * ## Example Usage
 * @code
 * SVICalibrator calibrator;
 *
 * // Configure
 * SVICalibrator::Options opts;
 * opts.maxIterations = 100;
 * opts.tolerance = 1e-8;
 * calibrator.setOptions(opts);
 *
 * // Calibrate
 * std::vector<std::pair<double, double>> data;  // (logMoneyness, totalVariance)
 * std::vector<double> weights;
 * SVIParams initialGuess = {0.04, 0.1, -0.3, 0.0, 0.3};
 *
 * auto result = calibrator.calibrate(data, weights, initialGuess);
 * if (result.converged) {
 *     std::cout << "Calibrated a=" << result.params.a << std::endl;
 * }
 * @endcode
 */
class SVICalibrator {
public:
    /**
     * @brief Calibration result
     */
    struct Result {
        SVIParams params;          ///< Calibrated parameters
        bool converged;            ///< True if optimization converged
        int iterations;            ///< Number of iterations used
        double finalResidual;      ///< Sum of squared residuals
        double finalResidualNorm;  ///< sqrt(sum of r_i^2)
        std::string message;       ///< Status message
    };
    
    /**
     * @brief Calibration options
     */
    struct Options {
        int maxIterations = 100;       ///< Maximum LM iterations
        double tolerance = 1e-8;       ///< Convergence tolerance on residual
        double paramTolerance = 1e-8;  ///< Convergence tolerance on parameters
        double initialDamping = 1e-3;  ///< Initial LM damping lambda
        double dampingDownFactor = 0.1; ///< lambda reduction factor on improvement
        double dampingUpFactor = 10.0; ///< lambda increase factor on worse fit
        double minDamping = 1e-10;     ///< Minimum allowed lambda
        double maxDamping = 1e10;      ///< Maximum allowed lambda
        bool verbose = false;          ///< Print iteration details
    };
    
    /**
     * @brief Construct calibrator with default options
     */
    explicit SVICalibrator(const Options& opts = Options()) : options_(opts) {}
    
    /**
     * @brief Calibrate SVI parameters to market data
     *
     * @param data Vector of (logMoneyness, totalVariance) pairs
     * @param weights Weight for each data point (typically inverse variance)
     * @param initialGuess Starting parameters for optimization
     * @return Calibration result with fitted parameters
     *
     * Uses Levenberg-Marquardt with analytical Jacobian.
     */
    Result calibrate(
        const std::vector<std::pair<double, double>>& data,
        const std::vector<double>& weights,
        const SVIParams& initialGuess);
    
    /**
     * @brief Set calibration options
     */
    void setOptions(const Options& opts) { options_ = opts; }
    
    /**
     * @brief Get current options
     */
    const Options& getOptions() const { return options_; }

private:
    Options options_;
    
    // Parameter vector conversion
    Eigen::VectorXd paramsToVector(const SVIParams& params) const;
    SVIParams vectorToParams(const Eigen::VectorXd& theta) const;
    
    // Residual computation
    Eigen::VectorXd computeResiduals(
        const std::vector<std::pair<double, double>>& data,
        const std::vector<double>& weights,
        const SVIParams& params) const;
    
    // Analytical Jacobian
    Eigen::MatrixXd computeJacobian(
        const std::vector<std::pair<double, double>>& data,
        const std::vector<double>& weights,
        const SVIParams& params) const;
    
    // LM step solver
    Eigen::VectorXd solveLMStep(
        const Eigen::MatrixXd& J,
        const Eigen::VectorXd& r,
        double lambda) const;
    
    // Parameter projection
    SVIParams projectToFeasible(const SVIParams& params) const;
    
    // Convergence check
    bool checkConvergence(
        const Eigen::VectorXd& r,
        const Eigen::VectorXd& delta,
        double residualNorm) const;
};

/**
 * @brief SVI-based volatility surface
 *
 * Constructs a volatility surface by fitting SVI parameters at each
 * expiry slice, with arbitrage constraints enforced.
 *
 * @see VolSurface for grid-based interpolation
 */
class SVISurface {
public:
    /**
     * @brief Construct SVI surface from market quotes
     *
     * @param quotes Option quotes
     * @param marketData Market environment
     *
     * Fits SVI parameters at each unique expiry.
     */
    explicit SVISurface(const std::vector<Quote>& quotes, const MarketData& marketData);
    
    /**
     * @brief Interpolate implied volatility
     *
     * @param strike Strike price
     * @param expiry Time to expiry
     * @return Implied volatility
     *
     * Uses SVI formula at each expiry with interpolation between expiries.
     */
    double impliedVol(double strike, double expiry) const;
    
    /**
     * @brief Get market data
     */
    const MarketData& marketData() const { return marketData_; }
    
    /**
     * @brief Get calibrated expiries
     */
    const std::vector<double>& expiries() const { return expiries_; }
    
    /**
     * @brief Get SVI parameters for all expiries
     */
    const std::vector<SVIParams>& sviParams() const { return sviParams_; }
    
    /**
     * @brief Check if surface is arbitrage-free
     * @return True if no arbitrage conditions violated
     */
    bool isArbitrageFree() const;
    
    /**
     * @brief Get descriptions of arbitrage violations
     * @return Vector of violation descriptions
     */
    std::vector<std::string> getArbitrageViolations() const;
    
    /**
     * @brief Print surface information
     */
    void print() const;

private:
    MarketData marketData_;
    std::vector<double> expiries_;
    std::vector<SVIParams> sviParams_;
    
    SVIParams fitSVI(const std::vector<Quote>& quotes, double expiry) const;
    SVIParams calibrateSVI(const std::vector<std::pair<double, double>>& logMoneynessVariance) const;
    SVIParams enforceArbitrageConstraints(const SVIParams& params, double expiry) const;
    
    double weightFunction(double logMoneyness) const;
    double forwardPrice(double expiry) const;
};
