#pragma once
#include "vol_surface.hpp"
#include "arbitrage_detector.hpp"
#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <vector>

struct QPResult {
    bool        success;
    Eigen::VectorXd ivFlat;
    double      objectiveValue;
    std::string status;
};

class QPSolver {
public:
    explicit QPSolver(const VolSurface& surface);
    QPResult   solve() const;
    VolSurface buildCorrectedSurface(const QPResult& result) const;

    // Public so DualCertifier and main can access the full constraint matrix
    void buildConstraints(
        Eigen::SparseMatrix<double>& A,
        Eigen::VectorXd& lb,
        Eigen::VectorXd& ub) const;

private:
    const VolSurface& surface_;
    int  nStrikes()  const;
    int  nExpiries() const;
    int  idx(int ei, int ki) const;
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
};