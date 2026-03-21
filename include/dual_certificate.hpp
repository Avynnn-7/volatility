/**
 * @file dual_certificate.hpp
 * @brief KKT dual certificate verification for QP solutions
 * @author vol_arb Team
 * @version 2.0
 * @date 2024
 *
 * Verifies that a QP solution is optimal by checking the Karush-Kuhn-Tucker
 * (KKT) conditions:
 *
 * 1. **Stationarity**: ∇L = x* - σ_mkt + A^T λ = 0
 * 2. **Complementary Slackness**: λᵢ · (Ax*)ᵢ = 0 for all i
 * 3. **Dual Feasibility**: λ ≥ 0
 *
 * A valid certificate provides formal proof that the corrected surface
 * is the closest arbitrage-free surface to the market surface.
 *
 * @see QPSolver for generating the solution
 */

#pragma once
#include "qp_solver.hpp"
#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <vector>
#include <string>

/**
 * @brief KKT dual certificate result
 *
 * Contains verification results for all KKT conditions.
 */
struct DualCertificate {
    bool valid;                    ///< True if all conditions satisfied
    double stationarityResidual;   ///< ||∇L||₂ (should be ~0)
    double compSlackResidual;      ///< Σ|λᵢ · (Ax*)ᵢ| (should be ~0)
    double dualFeasResidual;       ///< Σmax(0, -λᵢ) (should be 0)
    Eigen::VectorXd lambda;        ///< Dual variables
    std::string summary;           ///< Human-readable summary
};

/**
 * @brief KKT condition verifier for QP solutions
 *
 * Computes and verifies the KKT conditions to certify optimality.
 *
 * ## Example Usage
 * @code
 * // After solving QP
 * QPResult result = solver.solve();
 *
 * // Get constraint matrix
 * Eigen::SparseMatrix<double> A;
 * Eigen::VectorXd lb, ub;
 * solver.buildConstraints(A, lb, ub);
 *
 * // Verify solution
 * DualCertifier certifier(ivMarket, result, A, lb, ub);
 * DualCertificate cert = certifier.certify();
 *
 * if (cert.valid) {
 *     std::cout << "Solution is provably optimal" << std::endl;
 * }
 * certifier.print(cert);
 * @endcode
 */
class DualCertifier {
public:
    /**
     * @brief Construct certifier
     *
     * @param ivMarket Original market IV vector
     * @param result QP solution result
     * @param A Constraint matrix
     * @param lb Lower bounds
     * @param ub Upper bounds
     */
    DualCertifier(const Eigen::VectorXd& ivMarket,
                  const QPResult& result,
                  const Eigen::SparseMatrix<double>& A,
                  const Eigen::VectorXd& lb,
                  const Eigen::VectorXd& ub);

    /**
     * @brief Verify KKT conditions
     *
     * @param tol Tolerance for residual checks (default 1e-6)
     * @return DualCertificate with verification results
     *
     * Computes dual variables and checks all KKT conditions.
     */
    DualCertificate certify(double tol = 1e-6) const;
    
    /**
     * @brief Print certificate to stdout
     * @param cert Certificate to print
     */
    void print(const DualCertificate& cert) const;

private:
    const Eigen::VectorXd& ivMarket_;
    const QPResult& result_;
    const Eigen::SparseMatrix<double>& A_;
    const Eigen::VectorXd& lb_;
    const Eigen::VectorXd& ub_;
};
