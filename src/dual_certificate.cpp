#include "dual_certificate.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

DualCertifier::DualCertifier(const Eigen::VectorXd& ivMarket,
                              const QPResult&        result,
                              const Eigen::SparseMatrix<double>& A,
                              const Eigen::VectorXd& lb,
                              const Eigen::VectorXd& ub)
    : ivMarket_(ivMarket), result_(result), A_(A), lb_(lb), ub_(ub) {}

DualCertificate DualCertifier::certify(double tol) const {
    DualCertificate cert;
    cert.valid  = false;

    if (!result_.success) {
        cert.summary = "QP did not solve to optimality — dual certificate unavailable.";
        cert.stationarityResidual = -1;
        cert.compSlackResidual    = -1;
        cert.dualFeasResidual     = -1;
        return cert;
    }

    const Eigen::VectorXd& x = result_.ivFlat;   // primal solution x*
    int n = (int)x.size();
    int m = (int)lb_.size();

    // ── Reconstruct dual variables via stationarity: A^T λ = -(x* - iv_mkt)
    // For our identity-P QP: gradient of Lagrangian = x - iv_mkt + A^T λ = 0
    // So A^T λ = iv_mkt - x*
    // We solve this least-squares system for λ.
    Eigen::VectorXd rhs = ivMarket_ - x;
    // λ = (A A^T)^{-1} A * rhs  — use Eigen's sparse least squares
    Eigen::LeastSquaresConjugateGradient<Eigen::SparseMatrix<double>> lsq;
    lsq.compute(A_.transpose());
    cert.lambda = lsq.solve(rhs);

    // ── KKT Check 1: Stationarity  ||x* - iv_mkt + A^T λ||
    Eigen::VectorXd stationarity = x - ivMarket_ + A_.transpose() * cert.lambda;
    cert.stationarityResidual = stationarity.norm();

    // ── KKT Check 2: Complementary slackness  λ_i * (Ax*)_i = 0
    Eigen::VectorXd Ax = A_ * x;
    double csResid = 0.0;
    for (int i = 0; i < m; ++i) {
        // Constraint is lb <= Ax <= ub; slack from lower bound = Ax_i - lb_i
        double slack_lo = Ax(i) - lb_(i);
        double slack_hi = ub_(i) - Ax(i);
        // Active constraint slack should pair with nonzero lambda
        double lam_i = (i < (int)cert.lambda.size()) ? cert.lambda(i) : 0.0;
        csResid += std::abs(lam_i * std::min(slack_lo, slack_hi));
    }
    cert.compSlackResidual = csResid;

    // ── KKT Check 3: Dual feasibility  λ >= 0 (for >= constraints)
    double dualViol = 0.0;
    for (int i = 0; i < (int)cert.lambda.size(); ++i)
        if (cert.lambda(i) < -tol) dualViol += std::abs(cert.lambda(i));
    cert.dualFeasResidual = dualViol;

    cert.valid = (cert.stationarityResidual < tol * 10)
              && (cert.dualFeasResidual      < tol * 10);

    // ── Build summary string
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(6);
    ss << "KKT Certificate:\n";
    ss << "  Stationarity residual  : " << cert.stationarityResidual
       << (cert.stationarityResidual < tol*10 ? "  ✓" : "  ✗") << "\n";
    ss << "  Comp. slackness resid  : " << cert.compSlackResidual    << "\n";
    ss << "  Dual feasibility viol  : " << cert.dualFeasResidual
       << (cert.dualFeasResidual < tol*10 ? "  ✓" : "  ✗") << "\n";
    ss << "  Certificate valid      : " << (cert.valid ? "YES" : "NO") << "\n";

    // Report the 3 most active constraints (largest |lambda|)
    if (cert.lambda.size() > 0) {
        std::vector<std::pair<double,int>> lams;
        for (int i = 0; i < (int)cert.lambda.size(); ++i)
            lams.push_back({std::abs(cert.lambda(i)), i});
        std::sort(lams.rbegin(), lams.rend());
        ss << "  Most active constraints (by |λ|):\n";
        for (int k = 0; k < std::min(3, (int)lams.size()); ++k)
            ss << "    constraint[" << lams[k].second << "]  λ = "
               << cert.lambda(lams[k].second) << "\n";
    }
    cert.summary = ss.str();
    return cert;
}

void DualCertifier::print(const DualCertificate& cert) const {
    std::cout << "\n=== Dual Certificate ===\n" << cert.summary << "\n";
}