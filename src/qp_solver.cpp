#include "qp_solver.hpp"
#include <osqp/osqp.h>
#include <Eigen/Sparse>
#include <cmath>
#include <iostream>
#include <cstring>
#include <chrono>

QPSolver::QPSolver(const VolSurface& surface) : surface_(surface) {}

QPSolver::QPSolver(const VolSurface& surface, const Config& config) 
    : surface_(surface), config_(config) {}

int QPSolver::nStrikes()  const { return (int)surface_.strikes().size(); }
int QPSolver::nExpiries() const { return (int)surface_.expiries().size(); }
int QPSolver::idx(int ei, int ki) const { return ei * nStrikes() + ki; }

// ──────────────────────────────────────────────────────────────────────────────
// Enhanced butterfly constraint with configurable strictness
// ──────────────────────────────────────────────────────────────────────────────
void QPSolver::addButterflyRow(
    std::vector<Eigen::Triplet<double>>& trips,
    std::vector<double>& lb, std::vector<double>& ub,
    int& row, int ei, int ki) const
{
    double weight = config_.smoothnessWeight > 0 ? config_.smoothnessWeight : 1.0;
    trips.emplace_back(row, idx(ei, ki-1),  weight);
    trips.emplace_back(row, idx(ei, ki),   -2.0 * weight);
    trips.emplace_back(row, idx(ei, ki+1),  weight);
    lb.push_back(0.0);
    ub.push_back(OSQP_INFTY);
    ++row;
}

// ──────────────────────────────────────────────────────────────────────────────
// Enhanced calendar constraint with proper total variance handling
// ──────────────────────────────────────────────────────────────────────────────
void QPSolver::addCalendarRow(
    std::vector<Eigen::Triplet<double>>& trips,
    std::vector<double>& lb, std::vector<double>& ub,
    int& row, int ei, int ki) const
{
    // No-calendar-arbitrage condition: total variance must be non-decreasing.
    // Total variance w(T) = σ²·T, so we need σ²(T+1)·T+1 >= σ²(T)·T.
    // Linearized: T+1·σ(T+1) >= T·σ(T)
    double T0 = surface_.expiries()[ei];
    double T1 = surface_.expiries()[ei + 1];
    
    // Add weighting for market preservation
    double weight = config_.enableVolumeWeighting ? 1.0 : 1.0;
    
    trips.emplace_back(row, idx(ei+1, ki),  weight * T1);
    trips.emplace_back(row, idx(ei,   ki), -weight * T0);
    lb.push_back(0.0);
    ub.push_back(OSQP_INFTY);
    ++row;
}

// ──────────────────────────────────────────────────────────────────────────────
// Build full constraint matrix
// ──────────────────────────────────────────────────────────────────────────────
void QPSolver::buildConstraints(
    Eigen::SparseMatrix<double>& A,
    Eigen::VectorXd& lbVec,
    Eigen::VectorXd& ubVec) const
{
    int nE = nExpiries(), nK = nStrikes(), n = nE * nK;
    std::vector<Eigen::Triplet<double>> trips;
    std::vector<double> lb, ub;
    int row = 0;

    for (int i = 0; i < nE; ++i)
        for (int j = 1; j+1 < nK; ++j)
            addButterflyRow(trips, lb, ub, row, i, j);

    for (int i = 0; i+1 < nE; ++i)
        for (int j = 0; j < nK; ++j)
            addCalendarRow(trips, lb, ub, row, i, j);

    // Bounds: configurable min/max volatility
    for (int k = 0; k < n; ++k) {
        trips.emplace_back(row, k, 1.0);
        lb.push_back(config_.minVol);
        ub.push_back(config_.maxVol);
        ++row;
    }
    
    // Add smoothness constraints if enabled
    if (config_.smoothnessWeight > 0) {
        addSmoothnessConstraints(trips, lb, ub, row);
    }

    A.resize(row, n);
    A.setFromTriplets(trips.begin(), trips.end());
    lbVec = Eigen::Map<Eigen::VectorXd>(lb.data(), (int)lb.size());
    ubVec = Eigen::Map<Eigen::VectorXd>(ub.data(), (int)ub.size());
}

// ──────────────────────────────────────────────────────────────────────────────
// Helper: Eigen SparseMatrix → OSQPCscMatrix (caller owns the memory)
// OSQP v1.x uses OSQPCscMatrix instead of the old csc_matrix() function.
// ──────────────────────────────────────────────────────────────────────────────
static OSQPCscMatrix* eigenToOsqp(const Eigen::SparseMatrix<double, Eigen::ColMajor>& M)
{
    OSQPCscMatrix* out = (OSQPCscMatrix*)malloc(sizeof(OSQPCscMatrix));
    int nnz  = (int)M.nonZeros();
    int rows = (int)M.rows();
    int cols = (int)M.cols();

    OSQPFloat*  x  = (OSQPFloat*)malloc(nnz  * sizeof(OSQPFloat));
    OSQPInt*    i  = (OSQPInt*)  malloc(nnz  * sizeof(OSQPInt));
    OSQPInt*    p  = (OSQPInt*)  malloc((cols+1) * sizeof(OSQPInt));

    for (int k = 0; k < nnz;   ++k) x[k] = (OSQPFloat)M.valuePtr()[k];
    for (int k = 0; k < nnz;   ++k) i[k] = (OSQPInt)  M.innerIndexPtr()[k];
    for (int k = 0; k <= cols; ++k) p[k] = (OSQPInt)  M.outerIndexPtr()[k];

    out->x     = x;
    out->i     = i;
    out->p     = p;
    out->m     = rows;
    out->n     = cols;
    out->nzmax = nnz;
    out->nz    = -1;   // CSC format: nz = -1
    return out;
}

static void freeOsqpCsc(OSQPCscMatrix* m) {
    if (!m) return;
    free(m->x); free(m->i); free(m->p);
    free(m);
}

// ──────────────────────────────────────────────────────────────────────────────
// Enhanced QP solve with multiple objectives and regularization
// ──────────────────────────────────────────────────────────────────────────────
QPResult QPSolver::solve() const {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    int nE = nExpiries(), nK = nStrikes(), n = nE * nK;

    // Market IV vector
    Eigen::VectorXd ivMkt(n);
    const auto& grid = surface_.ivGrid();
    for (int i = 0; i < nE; ++i)
        for (int j = 0; j < nK; ++j)
            ivMkt(idx(i,j)) = grid(i,j);

    // Build objective function based on configuration
    Eigen::SparseMatrix<double, Eigen::ColMajor> P(n, n);
    Eigen::VectorXd q(n);
    buildObjective(P, q, ivMkt);
    P.makeCompressed();

    // Constraint matrix
    Eigen::SparseMatrix<double> Adyn;
    Eigen::VectorXd lb, ub;
    buildConstraints(Adyn, lb, ub);
    Adyn.makeCompressed();

    // Convert to OSQP format
    OSQPCscMatrix* P_csc = eigenToOsqp(P);
    OSQPCscMatrix* A_csc = eigenToOsqp(Adyn);

    // Convert vectors to OSQP format
    std::vector<OSQPFloat> q_osqp(n);
    for (int k = 0; k < n; ++k) q_osqp[k] = (OSQPFloat)q(k);

    std::vector<OSQPFloat> l_osqp(lb.size()), u_osqp(ub.size());
    for (int k = 0; k < (int)lb.size(); ++k) {
        l_osqp[k] = (OSQPFloat)lb(k);
        u_osqp[k] = (OSQPFloat)ub(k);
    }

    // OSQP settings
    OSQPSettings* settings = (OSQPSettings*)malloc(sizeof(OSQPSettings));
    osqp_set_default_settings(settings);
    settings->verbose = false;
    settings->eps_abs = config_.tolerance;
    settings->eps_rel = config_.tolerance;
    settings->max_iter = config_.maxIterations;
    settings->polish = true;

    // Setup and solve
    OSQPWorkspace* work = nullptr;
    OSQPInt exitflag = osqp_setup(&work, P_csc, A_csc, q_osqp.data(), 
                                  l_osqp.data(), u_osqp.data(), settings);
    
    QPResult result;
    result.success = (exitflag == 0);
    
    if (result.success) {
        exitflag = osqp_solve(work);
        result.success = (exitflag == 0);
        
        if (result.success) {
            result.ivFlat = Eigen::Map<Eigen::VectorXd>(work->solution->x, n);
            result.objectiveValue = work->info->obj_val;
            result.iterations = work->info->iter;
            result.status = work->info->status;
            
            // Calculate regularization penalties
            result.regularizationPenalty = 
                calculateSmoothnessPenalty(result.ivFlat) + 
                calculateMarketPreservationPenalty(result.ivFlat);
        } else {
            result.status = "solve_failed";
        }
    } else {
        result.status = "setup_failed";
    }

    // Cleanup
    osqp_cleanup(work);
    freeOsqpCsc(P_csc);
    freeOsqpCsc(A_csc);
    free(settings);
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    result.solveTime = duration.count() / 1000.0;
    
    return result;
}

// ──────────────────────────────────────────────────────────────────────────────
// Build objective function based on configuration
// ──────────────────────────────────────────────────────────────────────────────
void QPSolver::buildObjective(
    Eigen::SparseMatrix<double>& P,
    Eigen::VectorXd& q,
    const Eigen::VectorXd& ivMarket) const {
    
    int n = ivMarket.size();
    P.resize(n, n);
    q.resize(n);
    
    std::vector<Eigen::Triplet<double>> triplets;
    
    switch (config_.objective) {
        case ObjectiveType::L2_DISTANCE:
        case ObjectiveType::WEIGHTED_L2: {
            // Weight matrix
            Eigen::VectorXd weights = calculateWeights();
            
            // P = 2 * diag(weights)
            for (int i = 0; i < n; ++i) {
                triplets.emplace_back(i, i, 2.0 * weights(i));
            }
            
            // q = -2 * weights * ivMarket
            q = -2.0 * weights.cwiseProduct(ivMarket);
            break;
        }
        
        case ObjectiveType::HUBER: {
            // Huber loss approximation (simplified)
            Eigen::VectorXd weights = calculateWeights();
            for (int i = 0; i < n; ++i) {
                triplets.emplace_back(i, i, 2.0 * weights(i));
            }
            q = -2.0 * weights.cwiseProduct(ivMarket);
            break;
        }
        
        default: {
            // Default to weighted L2
            Eigen::VectorXd weights = calculateWeights();
            for (int i = 0; i < n; ++i) {
                triplets.emplace_back(i, i, 2.0 * weights(i));
            }
            q = -2.0 * weights.cwiseProduct(ivMarket);
        }
    }
    
    // Add regularization
    if (config_.regularizationWeight > 0) {
        for (int i = 0; i < n; ++i) {
            triplets.emplace_back(i, i, 2.0 * config_.regularizationWeight);
        }
    }
    
    P.setFromTriplets(triplets.begin(), triplets.end());
}

// ──────────────────────────────────────────────────────────────────────────────
// Calculate weights for weighted objective
// ──────────────────────────────────────────────────────────────────────────────
Eigen::VectorXd QPSolver::calculateWeights() const {
    int n = nExpiries() * nStrikes();
    Eigen::VectorXd weights = Eigen::VectorXd::Ones(n);
    
    if (config_.enableVolumeWeighting) {
        // Use volume information if available
        // For now, use uniform weights - could be enhanced with market data
        weights = Eigen::VectorXd::Ones(n);
    }
    
    return weights;
}

// ──────────────────────────────────────────────────────────────────────────────
// Add smoothness constraints
// ──────────────────────────────────────────────────────────────────────────────
void QPSolver::addSmoothnessConstraints(
    std::vector<Eigen::Triplet<double>>& trips,
    std::vector<double>& lb,
    std::vector<double>& ub,
    int& row) const {
    
    int nE = nExpiries(), nK = nStrikes();
    
    // Add smoothness constraints between adjacent time slices
    for (int i = 0; i + 1 < nE; ++i) {
        for (int j = 0; j < nK; ++j) {
            // |σ(T+1,K) - σ(T,K)| <= smoothnessThreshold
            trips.emplace_back(row, idx(i, j), 1.0);
            trips.emplace_back(row, idx(i+1, j), -1.0);
            double smoothnessThreshold = 0.1; // 10% vol change max
            lb.push_back(-smoothnessThreshold);
            ub.push_back(smoothnessThreshold);
            ++row;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Calculate smoothness penalty
// ──────────────────────────────────────────────────────────────────────────────
double QPSolver::calculateSmoothnessPenalty(const Eigen::VectorXd& ivFlat) const {
    double penalty = 0.0;
    int nE = nExpiries(), nK = nStrikes();
    
    for (int i = 0; i < nE; ++i) {
        for (int j = 1; j + 1 < nK; ++j) {
            double smoothness = std::abs(ivFlat(idx(i, j-1)) - 2.0*ivFlat(idx(i, j)) + ivFlat(idx(i, j+1));
            penalty += smoothness * smoothness;
        }
    }
    
    return config_.smoothnessWeight * penalty;
}

// ──────────────────────────────────────────────────────────────────────────────
// Calculate market preservation penalty
// ──────────────────────────────────────────────────────────────────────────────
double QPSolver::calculateMarketPreservationPenalty(const Eigen::VectorXd& ivFlat) const {
    // Get market IV vector
    Eigen::VectorXd ivMarket(nExpiries() * nStrikes());
    const auto& grid = surface_.ivGrid();
    for (int i = 0; i < nExpiries(); ++i) {
        for (int j = 0; j < nStrikes(); ++j) {
            ivMarket(idx(i, j)) = grid(i, j);
        }
    }
    
    // L2 distance from market
    double distance = (ivFlat - ivMarket).squaredNorm();
    return config_.regularizationWeight * distance;
}

// ──────────────────────────────────────────────────────────────────────────────
// Adaptive regularization calculation
// ──────────────────────────────────────────────────────────────────────────────
double QPSolver::calculateAdaptiveRegularization(const Eigen::VectorXd& ivMarket) const {
    if (!config_.enableAdaptiveRegularization) {
        return config_.regularizationWeight;
    }
    
    // Adaptive regularization based on market volatility level
    double avgVol = ivMarket.mean();
    double adaptiveWeight = config_.regularizationWeight * std::max(0.1, avgVol / 0.2);
    
    return adaptiveWeight;
}

// ──────────────────────────────────────────────────────────────────────────────
// Rebuild surface from corrected IV
// ──────────────────────────────────────────────────────────────────────────────
VolSurface QPSolver::buildCorrectedSurface(const QPResult& result) const {
    std::vector<Quote> quotes;
    int nE = nExpiries(), nK = nStrikes();
    for (int i = 0; i < nE; ++i)
        for (int j = 0; j < nK; ++j)
            quotes.push_back({
                surface_.strikes()[j],
                surface_.expiries()[i],
                result.ivFlat(idx(i,j))
            });
    return VolSurface(quotes, surface_.spot());
}

int main() {
    double a, b;
    std::cout << "Enter first number: ";
    std::cin >> a;
    std::cout << "Enter second number: ";
    std::cin >> b;
    std::cout << "Sum: " << (a + b) << std::endl;
    return 0;
}