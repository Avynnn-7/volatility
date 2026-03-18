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
    double      regularizationPenalty;
    int         iterations;
    std::string status;
    double      solveTime;
};

// Enhanced QP solver with regularization and multiple objectives
class QPSolver {
public:
    // Objective function types
    enum class ObjectiveType {
        L2_DISTANCE,           // Minimize L2 distance to market surface
        WEIGHTED_L2,           // Weighted L2 with volume weighting
        HUBER,                // Huber loss (robust to outliers)
        FAIRNESS_PENALTY,     // Add smoothness penalty
        MARKET_PRESERVATION   // Preserve market characteristics
    };
    
    // Configuration for QP solver
    struct Config {
        ObjectiveType objective = ObjectiveType::WEIGHTED_L2;
        double regularizationWeight = 1e-6;     // L2 regularization strength
        double smoothnessWeight = 1e-4;         // Smoothness penalty weight
        double huberThreshold = 0.01;           // Huber loss threshold
        bool enableVolumeWeighting = true;       // Use volume for weighting
        double minVol = 0.001;                  // Minimum allowed volatility
        double maxVol = 5.0;                    // Maximum allowed volatility
        double tolerance = 1e-9;                // Solver tolerance
        int maxIterations = 10000;              // Maximum solver iterations
        bool enableAdaptiveRegularization = true; // Adaptive regularization
    };
    
    explicit QPSolver(const VolSurface& surface);
    QPSolver(const VolSurface& surface, const Config& config);
    
    QPResult solve() const;
    VolSurface buildCorrectedSurface(const QPResult& result) const;
    
    // Configuration
    void setConfig(const Config& config) { config_ = config; }
    const Config& getConfig() const { return config_; }

    // Public so DualCertifier and main can access the full constraint matrix
    void buildConstraints(
        Eigen::SparseMatrix<double>& A,
        Eigen::VectorXd& lb,
        Eigen::VectorXd& ub) const;
    
    // Additional constraint types
    void addSmoothnessConstraints(
        std::vector<Eigen::Triplet<double>>& trips,
        std::vector<double>& lb,
        std::vector<double>& ub,
        int& row) const;
    
    // Quality assessment
    double calculateSmoothnessPenalty(const Eigen::VectorXd& ivFlat) const;
    double calculateMarketPreservationPenalty(const Eigen::VectorXd& ivFlat) const;

private:
    const VolSurface& surface_;
    Config config_;
    
    // Helper methods
    int  nStrikes()  const;
    int  nExpiries() const;
    int  idx(int ei, int ki) const;
    
    // Constraint builders
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
    
    // Objective function builders
    void buildObjective(
        Eigen::SparseMatrix<double>& P,
        Eigen::VectorXd& q,
        const Eigen::VectorXd& ivMarket) const;
    
    // Weight calculation
    Eigen::VectorXd calculateWeights() const;
    
    // Adaptive regularization
    double calculateAdaptiveRegularization(const Eigen::VectorXd& ivMarket) const;
};