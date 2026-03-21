/**
 * @file qp_solver.hpp
 * @brief Quadratic programming solver for arbitrage-free surface projection
 * @author vol_arb Team
 * @version 2.0
 * @date 2024
 *
 * This file provides the QP-based arbitrage repair functionality:
 * - L2 projection onto the arbitrage-free cone
 * - Multiple objective functions (L2, Weighted L2, Huber)
 * - Regularization for numerical stability
 * - Constraint verification and refinement
 *
 * ## Mathematical Formulation
 *
 * We solve the following QP:
 *
 *     min  ||sigma - sigma_mkt||^2 + lambda*R(sigma)
 *     s.t. A*sigma >= 0           (no-arbitrage constraints)
 *          sigma_min <= sigma <= sigma_max (box constraints)
 *
 * Where:
 * - sigma is the flattened IV vector
 * - sigma_mkt is the market IV
 * - R(sigma) is a regularization term
 * - A encodes butterfly and calendar constraints
 *
 * @see ArbitrageDetector for constraint derivation
 * @see DualCertifier for solution verification
 */

#pragma once
#include "vol_surface.hpp"
#include "arbitrage_detector.hpp"
#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <vector>
#include <tuple>
#include <osqp/osqp.h>

/**
 * @brief QP solver result structure
 *
 * Contains the solution and diagnostic information from the QP solver.
 */
struct QPResult {
    bool success;              ///< True if solver converged to optimal
    Eigen::VectorXd ivFlat;    ///< Corrected IV vector (flattened)
    double objectiveValue;     ///< Final objective function value
    double regularizationPenalty; ///< Regularization term contribution
    int iterations;            ///< Number of solver iterations
    std::string status;        ///< Solver status message
    double solveTime;          ///< Wall-clock solve time in seconds
};

/**
 * @brief QP solver for arbitrage-free surface projection
 *
 * Projects the market volatility surface onto the closest arbitrage-free
 * surface using quadratic programming with OSQP.
 *
 * ## Example Usage
 * @code
 * VolSurface surface(quotes, marketData);
 * QPSolver solver(surface);
 *
 * // Configure solver
 * QPSolver::Config config;
 * config.tolerance = 1e-9;
 * config.maxIterations = 10000;
 * solver.setConfig(config);
 *
 * // Solve
 * QPResult result = solver.solve();
 * if (result.success) {
 *     VolSurface corrected = solver.buildCorrectedSurface(result);
 *     std::cout << "Objective: " << result.objectiveValue << std::endl;
 * }
 * @endcode
 *
 * @see DualCertifier for KKT condition verification
 */
class QPSolver {
public:
    /**
     * @brief Objective function types
     */
    enum class ObjectiveType {
        L2_DISTANCE,          ///< Minimize ||sigma - sigma_mkt||^2
        WEIGHTED_L2,          ///< Minimize sum of w_i*(sigma_i - sigma_mkt_i)^2
        HUBER,                ///< Huber loss (robust to outliers)
        FAIRNESS_PENALTY,     ///< Add smoothness penalty
        MARKET_PRESERVATION   ///< Preserve market characteristics
    };
    
    /**
     * @brief QP solver configuration
     */
    struct Config {
        ObjectiveType objective = ObjectiveType::WEIGHTED_L2;
        double regularizationWeight = 1e-6;     ///< L2 regularization
        double smoothnessWeight = 1e-4;         ///< Smoothness penalty weight
        double huberThreshold = 0.01;           ///< Huber loss threshold
        bool enableVolumeWeighting = true;      ///< Weight by volume
        double minVol = 0.001;                  ///< Minimum IV (0.1%)
        double maxVol = 5.0;                    ///< Maximum IV (500%)
        double tolerance = 1e-9;                ///< Solver tolerance
        int maxIterations = 10000;              ///< Max iterations
        bool enableAdaptiveRegularization = true;
        bool verbose = false;                   ///< Print solver output
        
        // Calendar constraint settings
        bool enableNonlinearCalendarCheck = true;   ///< Verify sigma^2*T constraint
        bool enableCalendarRefinement = false;      ///< Iterative refinement
        double calendarViolationTol = 1e-6;         ///< Violation tolerance
    };
    
    /**
     * @brief Construct solver with default configuration
     * @param surface Input volatility surface
     */
    explicit QPSolver(const VolSurface& surface);
    
    /**
     * @brief Construct solver with custom configuration
     * @param surface Input volatility surface
     * @param config Solver configuration
     */
    QPSolver(const VolSurface& surface, const Config& config);
    
    /**
     * @brief Solve the QP and return corrected IV
     * @return QPResult with solution and diagnostics
     *
     * Solves the constrained optimization problem using OSQP.
     */
    QPResult solve() const;
    
    /**
     * @brief Build corrected surface from QP result
     * @param result Solution from solve()
     * @return New VolSurface with corrected IVs
     *
     * Creates a new surface using the corrected volatilities.
     */
    VolSurface buildCorrectedSurface(const QPResult& result) const;
    
    /**
     * @brief Set solver configuration
     * @param config New configuration
     */
    void setConfig(const Config& config) { config_ = config; }
    
    /**
     * @brief Get current configuration
     * @return Const reference to config
     */
    const Config& getConfig() const { return config_; }

    /**
     * @brief Build constraint matrix and bounds
     *
     * @param[out] A Sparse constraint matrix
     * @param[out] lb Lower bounds
     * @param[out] ub Upper bounds
     *
     * Constructs A such that A*sigma >= 0 encodes no-arbitrage conditions.
     */
    void buildConstraints(
        Eigen::SparseMatrix<double>& A,
        Eigen::VectorXd& lb,
        Eigen::VectorXd& ub) const;
    
    /**
     * @brief Add smoothness constraints to system
     */
    void addSmoothnessConstraints(
        std::vector<Eigen::Triplet<double>>& trips,
        std::vector<double>& lb,
        std::vector<double>& ub,
        int& row) const;
    
    /**
     * @brief Calculate smoothness penalty
     * @param ivFlat IV vector
     * @return Smoothness penalty value
     */
    double calculateSmoothnessPenalty(const Eigen::VectorXd& ivFlat) const;
    
    /**
     * @brief Calculate market preservation penalty
     * @param ivFlat IV vector
     * @return Preservation penalty value
     */
    double calculateMarketPreservationPenalty(const Eigen::VectorXd& ivFlat) const;
    
    /**
     * @brief Verify calendar constraint on solution
     *
     * @param ivFlat Solution IV vector
     * @param[out] violations Detected violations (expiry_idx, strike_idx, amount)
     * @return True if all constraints satisfied
     *
     * Verifies that sigma^2*T is non-decreasing in T for each strike.
     */
    bool verifyCalendarConstraint(
        const Eigen::VectorXd& ivFlat,
        std::vector<std::tuple<int, int, double>>& violations) const;
    
    /**
     * @brief Iteratively refine calendar violations
     *
     * @param initialResult Initial QP solution
     * @param maxRefinementIterations Maximum refinement iterations
     * @return Refined QPResult
     *
     * Uses sequential quadratic programming to fix remaining
     * calendar violations after initial QP solve.
     */
    QPResult refineCalendarConstraint(
        const QPResult& initialResult,
        int maxRefinementIterations = 5) const;

private:
    const VolSurface& surface_;
    Config config_;
    
    // Grid accessors
    int nStrikes() const;
    int nExpiries() const;
    int idx(int ei, int ki) const;  ///< Flatten (expiry, strike) index
    
    // Constraint row builders
    void addButterflyRow(
        std::vector<Eigen::Triplet<double>>& trips,
        std::vector<double>& lb,
        std::vector<double>& ub,
        int& row, int ei, int ki) const;
        
    void addCalendarRow(
        std::vector<Eigen::Triplet<double>>& trips,
        std::vector<double>& lb,
        std::vector<double>& ub,
        int& row, int ei, int ki) const;
    
    // Objective function construction
    void buildObjective(
        Eigen::SparseMatrix<double>& P,
        Eigen::VectorXd& q,
        const Eigen::VectorXd& ivMarket) const;
    
    // Volume-based weighting
    Eigen::VectorXd calculateWeights() const;
    
    // Adaptive regularization
    double calculateAdaptiveRegularization(const Eigen::VectorXd& ivMarket) const;
    
    // OSQP settings optimization
    OSQPSettings* createOptimizedSettings() const;
    
    // Calendar violation computation
    double computeCalendarViolation(
        const Eigen::VectorXd& ivFlat,
        int expiry0_idx,
        int expiry1_idx,
        int strike_idx) const;
};
