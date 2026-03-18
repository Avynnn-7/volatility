#pragma once
#include "qp_solver.hpp"
#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <vector>
#include <string>

struct DualCertificate {
    bool   valid;
    double stationarityResidual;
    double compSlackResidual;
    double dualFeasResidual;
    Eigen::VectorXd lambda;
    std::string summary;
};

// Verifies KKT conditions for the QP solution.
// At optimum x*:   x* - iv_mkt + A^T λ = 0  (stationarity)
//                  λ_i (Ax*)_i = 0            (complementary slackness)
//                  λ >= 0                      (dual feasibility)
class DualCertifier {
public:
    DualCertifier(const Eigen::VectorXd& ivMarket,
                  const QPResult&        result,
                  const Eigen::SparseMatrix<double>& A,
                  const Eigen::VectorXd& lb,
                  const Eigen::VectorXd& ub);

    DualCertificate certify(double tol = 1e-6) const;
    void print(const DualCertificate& cert) const;

private:
    const Eigen::VectorXd&             ivMarket_;
    const QPResult&                    result_;
    const Eigen::SparseMatrix<double>& A_;
    const Eigen::VectorXd&             lb_;
    const Eigen::VectorXd&             ub_;
};
