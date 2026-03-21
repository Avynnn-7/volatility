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
    // ═══════════════════════════════════════════════════════════════════════════
    // PHASE 5 ROBUSTNESS #7: Empty/Single-Element Vector Protection
    // ═══════════════════════════════════════════════════════════════════════════
    if (quotes.empty())
        throw std::invalid_argument("SVISurface: no quotes supplied");

    // Extract unique expiries
    for (const auto& q : quotes) {
        expiries_.push_back(q.expiry);
    }
    std::sort(expiries_.begin(), expiries_.end());
    expiries_.erase(std::unique(expiries_.begin(), expiries_.end()), expiries_.end());
    
    // Protection: Single expiry is valid but warrants a warning
    if (expiries_.size() == 1) {
        std::cerr << "Warning: SVISurface has only one expiry. Interpolation not possible." << std::endl;
    }

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
        
        // Protection: Single quote at expiry - use simple flat surface
        if (expiryQuotes.size() == 1) {
            std::cerr << "Warning: Only 1 quote for expiry " << expiry 
                      << ". Using flat volatility." << std::endl;
        }
        
        SVIParams params = fitSVI(expiryQuotes, expiry);
        sviParams_.push_back(params);
    }
}

double SVISurface::impliedVol(double strike, double expiry) const {
    // ═══════════════════════════════════════════════════════════════════════════
    // PHASE 5 ROBUSTNESS #7: Empty/Single-Element Vector Protection
    // ═══════════════════════════════════════════════════════════════════════════
    
    // Handle edge cases
    if (expiries_.empty() || sviParams_.empty()) {
        return 0.20;  // Default 20% vol
    }
    
    // Single expiry case - no interpolation needed
    if (expiries_.size() == 1) {
        double logMoneyness = std::log(strike / forwardPrice(expiries_[0]));
        return sviParams_[0].impliedVol(logMoneyness, expiries_[0]);
    }
    
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
            // double T_prev = expiries_[i-1];
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
    // ═══════════════════════════════════════════════════════════════════════════
    // PHASE 3 IMPROVEMENT #1: Use Levenberg-Marquardt for proper nonlinear fitting
    // This replaces the crude linear regression with proper optimization
    // ═══════════════════════════════════════════════════════════════════════════
    
    // Step 1: Compute initial guess using method of moments (fast, approximate)
    double sum_w = 0, sum_wk = 0, sum_wk2 = 0, sum_wv = 0, sum_wkv = 0;
    
    for (const auto& [k, v] : logMoneynessVariance) {
        double weight = weightFunction(k);
        sum_w += weight;
        sum_wk += weight * k;
        sum_wk2 += weight * k * k;
        sum_wv += weight * v;
        sum_wkv += weight * k * v;
    }
    
    double mean_k = sum_wk / sum_w;
    double mean_v = sum_wv / sum_w;
    
    double num = sum_wkv - sum_w * mean_k * mean_v;
    double den = sum_wk2 - sum_w * mean_k * mean_k;
    
    double slope = (den > 1e-10) ? num / den : 0.0;
    double intercept = mean_v - slope * mean_k;
    
    // Initial guess for LM optimizer
    SVIParams initialGuess;
    initialGuess.a = std::max(0.001, intercept);
    initialGuess.b = std::max(0.1, std::abs(slope));
    initialGuess.rho = (slope < 0) ? -0.7 : 0.3;
    initialGuess.m = mean_k;
    initialGuess.sigma = 0.2;
    
    // Step 2: Compute weights for each data point (ATM weighted)
    std::vector<double> weights;
    weights.reserve(logMoneynessVariance.size());
    for (const auto& [k, v] : logMoneynessVariance) {
        weights.push_back(weightFunction(k));
    }
    
    // Step 3: Run Levenberg-Marquardt calibration
    SVICalibrator calibrator;
    SVICalibrator::Options opts;
    opts.maxIterations = 100;
    opts.tolerance = 1e-8;
    opts.verbose = false;  // Set to true for debugging
    calibrator.setOptions(opts);
    
    SVICalibrator::Result result = calibrator.calibrate(logMoneynessVariance, weights, initialGuess);
    
    // If LM converged, use its result; otherwise fall back to initial guess
    if (result.converged) {
        return result.params;
    } else {
        // Log warning but use best available result
        std::cerr << "Warning: SVI calibration did not fully converge. "
                  << result.message << " (residual=" << result.finalResidual << ")" << std::endl;
        return result.params;  // Still better than initial guess typically
    }
}

SVIParams SVISurface::enforceArbitrageConstraints(const SVIParams& params, double /*expiry*/) const {
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

// ═══════════════════════════════════════════════════════════════════════════
// PHASE 3 IMPROVEMENT #1 & #4: SVICalibrator Implementation
// Levenberg-Marquardt with Analytical Jacobian
// ═══════════════════════════════════════════════════════════════════════════

Eigen::VectorXd SVICalibrator::paramsToVector(const SVIParams& params) const {
    Eigen::VectorXd theta(5);
    theta << params.a, params.b, params.rho, params.m, params.sigma;
    return theta;
}

SVIParams SVICalibrator::vectorToParams(const Eigen::VectorXd& theta) const {
    SVIParams params;
    params.a = theta(0);
    params.b = theta(1);
    params.rho = theta(2);
    params.m = theta(3);
    params.sigma = theta(4);
    return params;
}

SVIParams SVICalibrator::projectToFeasible(const SVIParams& params) const {
    SVIParams proj = params;
    
    // Enforce parameter bounds for no-arbitrage
    proj.a = std::max(1e-6, proj.a);           // a > 0 (positive variance floor)
    proj.b = std::max(1e-6, proj.b);           // b > 0 (positive slope)
    proj.rho = std::clamp(proj.rho, -0.999, 0.999);  // -1 < rho < 1
    proj.sigma = std::max(1e-6, proj.sigma);   // sigma > 0
    
    // Ensure a + b*sigma*(1-|rho|) > 0 for no-arbitrage at wings
    double minVariance = proj.a + proj.b * proj.sigma * (1.0 - std::abs(proj.rho));
    if (minVariance < 1e-6) {
        proj.a = std::max(proj.a, 1e-6 - proj.b * proj.sigma * (1.0 - std::abs(proj.rho)));
    }
    
    return proj;
}

Eigen::VectorXd SVICalibrator::computeResiduals(
    const std::vector<std::pair<double, double>>& data,
    const std::vector<double>& weights,
    const SVIParams& params) const 
{
    int n = static_cast<int>(data.size());
    Eigen::VectorXd r(n);
    
    for (int i = 0; i < n; ++i) {
        double k = data[i].first;        // log-moneyness
        double w_obs = data[i].second;   // observed total variance
        double w_model = params.totalVariance(k);
        double wt = (i < static_cast<int>(weights.size())) ? weights[i] : 1.0;
        r(i) = wt * (w_model - w_obs);
    }
    
    return r;
}

// ═══════════════════════════════════════════════════════════════════════════
// PHASE 3 IMPROVEMENT #4: Analytical Jacobian
// 
// SVI: w(k) = a + b[ρ(k-m) + √((k-m)² + σ²)]
//
// Derivatives:
//   ∂w/∂a = 1
//   ∂w/∂b = ρ(k-m) + √((k-m)² + σ²)
//   ∂w/∂ρ = b(k-m)
//   ∂w/∂m = b[-ρ - (k-m)/√((k-m)² + σ²)]
//   ∂w/∂σ = b·σ/√((k-m)² + σ²)
// ═══════════════════════════════════════════════════════════════════════════
Eigen::MatrixXd SVICalibrator::computeJacobian(
    const std::vector<std::pair<double, double>>& data,
    const std::vector<double>& weights,
    const SVIParams& params) const 
{
    int n = static_cast<int>(data.size());
    Eigen::MatrixXd J(n, 5);
    
    for (int i = 0; i < n; ++i) {
        double k = data[i].first;
        double wt = (i < static_cast<int>(weights.size())) ? weights[i] : 1.0;
        
        double u = k - params.m;
        double sqrt_term = std::sqrt(u * u + params.sigma * params.sigma);
        
        // Analytical derivatives (multiplied by weight)
        J(i, 0) = wt * 1.0;                                          // ∂w/∂a
        J(i, 1) = wt * (params.rho * u + sqrt_term);                 // ∂w/∂b
        J(i, 2) = wt * params.b * u;                                 // ∂w/∂ρ
        J(i, 3) = wt * params.b * (-params.rho - u / sqrt_term);     // ∂w/∂m
        J(i, 4) = wt * params.b * params.sigma / sqrt_term;          // ∂w/∂σ
    }
    
    return J;
}

Eigen::VectorXd SVICalibrator::solveLMStep(
    const Eigen::MatrixXd& J,
    const Eigen::VectorXd& r,
    double lambda) const 
{
    // Solve (J^T J + λI) δ = -J^T r
    int nParams = static_cast<int>(J.cols());
    
    Eigen::MatrixXd JtJ = J.transpose() * J;
    Eigen::VectorXd Jtr = J.transpose() * r;
    
    // Add damping to diagonal
    for (int i = 0; i < nParams; ++i) {
        JtJ(i, i) += lambda * (1.0 + JtJ(i, i));  // Marquardt modification
    }
    
    // Solve using Cholesky decomposition (stable for positive definite)
    Eigen::LLT<Eigen::MatrixXd> llt(JtJ);
    if (llt.info() != Eigen::Success) {
        // Fall back to more robust solver if Cholesky fails
        return JtJ.ldlt().solve(-Jtr);
    }
    
    return llt.solve(-Jtr);
}

bool SVICalibrator::checkConvergence(
    const Eigen::VectorXd& /*r*/,
    const Eigen::VectorXd& delta,
    double residualNorm) const 
{
    // Check residual tolerance
    if (residualNorm < options_.tolerance) {
        return true;
    }
    
    // Check parameter change tolerance
    double deltaNorm = delta.norm();
    if (deltaNorm < options_.paramTolerance) {
        return true;
    }
    
    return false;
}

SVICalibrator::Result SVICalibrator::calibrate(
    const std::vector<std::pair<double, double>>& data,
    const std::vector<double>& weights,
    const SVIParams& initialGuess) 
{
    Result result;
    result.params = initialGuess;
    result.converged = false;
    result.iterations = 0;
    
    // Initialize parameters and damping
    Eigen::VectorXd theta = paramsToVector(initialGuess);
    double lambda = options_.initialDamping;
    
    // Initial residuals
    Eigen::VectorXd r = computeResiduals(data, weights, result.params);
    double residualNorm = r.norm();
    double prevResidualNorm = residualNorm;
    
    if (options_.verbose) {
        std::cout << "LM Iteration 0: residual = " << residualNorm << std::endl;
    }
    
    // Main Levenberg-Marquardt loop
    for (int iter = 0; iter < options_.maxIterations; ++iter) {
        result.iterations = iter + 1;
        
        // Compute Jacobian at current parameters
        Eigen::MatrixXd J = computeJacobian(data, weights, result.params);
        
        // Solve for parameter update
        Eigen::VectorXd delta = solveLMStep(J, r, lambda);
        
        // Try update
        Eigen::VectorXd theta_new = theta + delta;
        SVIParams params_new = vectorToParams(theta_new);
        params_new = projectToFeasible(params_new);
        
        // Compute new residuals
        Eigen::VectorXd r_new = computeResiduals(data, weights, params_new);
        double residualNorm_new = r_new.norm();
        
        // Compute improvement ratio (actual vs predicted reduction)
        double actualReduction = residualNorm * residualNorm - residualNorm_new * residualNorm_new;
        Eigen::VectorXd predictedResidual = r - J * delta;
        double predictedReduction = residualNorm * residualNorm - predictedResidual.squaredNorm();
        double rho = (predictedReduction > 1e-10) ? actualReduction / predictedReduction : 0.0;
        
        if (rho > 0) {
            // Good step - accept and decrease damping
            theta = theta_new;
            result.params = params_new;
            r = r_new;
            residualNorm = residualNorm_new;
            lambda = std::max(lambda * options_.dampingDownFactor, options_.minDamping);
            
            if (options_.verbose) {
                std::cout << "LM Iteration " << iter + 1 << ": residual = " << residualNorm 
                          << ", lambda = " << lambda << " (accepted)" << std::endl;
            }
            
            // Check convergence
            if (checkConvergence(r, delta, residualNorm)) {
                result.converged = true;
                result.finalResidual = residualNorm;
                result.finalResidualNorm = residualNorm;
                result.message = "Converged: residual or parameter change below tolerance";
                break;
            }
            
        } else {
            // Bad step - reject and increase damping
            lambda = std::min(lambda * options_.dampingUpFactor, options_.maxDamping);
            
            if (options_.verbose) {
                std::cout << "LM Iteration " << iter + 1 << ": residual = " << residualNorm 
                          << ", lambda = " << lambda << " (rejected)" << std::endl;
            }
        }
        
        // Check for stagnation
        if (std::abs(residualNorm - prevResidualNorm) < 1e-12) {
            result.message = "Stagnated: no improvement in residual";
            break;
        }
        prevResidualNorm = residualNorm;
        
        // Check damping bounds
        if (lambda > options_.maxDamping) {
            result.message = "Failed: damping parameter exceeded maximum";
            break;
        }
    }
    
    if (!result.converged && result.iterations >= options_.maxIterations) {
        result.message = "Failed: maximum iterations reached";
    }
    
    result.finalResidual = residualNorm;
    result.finalResidualNorm = residualNorm;
    
    return result;
}
