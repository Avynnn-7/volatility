#include "qp_solver.hpp"
#include "validation.hpp"  // Phase 5: Input validation
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
    // CRITICAL: Hard no-arbitrage constraint must NEVER be weighted
    // Smoothness weight is for SOFT objective terms, not HARD constraints
    // Weighting this constraint can cause numerical tolerance issues in OSQP
    trips.emplace_back(row, idx(ei, ki-1),  1.0);
    trips.emplace_back(row, idx(ei, ki),   -2.0);
    trips.emplace_back(row, idx(ei, ki+1),  1.0);
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
// ═══════════════════════════════════════════════════════════════════════════
// PHASE 4 OPTIMIZATION #4: Sparse Matrix Optimizations - Pre-allocate sizes
// ═══════════════════════════════════════════════════════════════════════════
// ──────────────────────────────────────────────────────────────────────────────
void QPSolver::buildConstraints(
    Eigen::SparseMatrix<double>& A,
    Eigen::VectorXd& lbVec,
    Eigen::VectorXd& ubVec) const
{
    int nE = nExpiries(), nK = nStrikes(), n = nE * nK;
    
    // ═══════════════════════════════════════════════════════════════════════════
    // PHASE 4 OPTIMIZATION #4: Pre-calculate exact sizes
    // ═══════════════════════════════════════════════════════════════════════════
    int numButterflyRows = nE * (nK - 2);           // Each expiry, interior strikes
    int numCalendarRows = (nE - 1) * nK;            // Between consecutive expiries
    int numBoundRows = n;                           // One per variable
    int numSmoothnessRows = (config_.smoothnessWeight > 0) ? 
                            (nE - 1) * nK : 0;      // Between consecutive expiries
    
    int totalRows = numButterflyRows + numCalendarRows + numBoundRows + numSmoothnessRows;
    
    // Pre-calculate exact number of non-zeros
    int nnzButterfly = numButterflyRows * 3;        // 3 entries per butterfly
    int nnzCalendar = numCalendarRows * 2;          // 2 entries per calendar
    int nnzBounds = numBoundRows * 1;               // 1 entry per bound
    int nnzSmoothness = (config_.smoothnessWeight > 0) ?
                        numSmoothnessRows * 2 : 0;  // 2 entries per smoothness
    
    int totalNnz = nnzButterfly + nnzCalendar + nnzBounds + nnzSmoothness;
    
    // Reserve exact capacity (avoids reallocations)
    std::vector<Eigen::Triplet<double>> trips;
    trips.reserve(totalNnz);
    
    std::vector<double> lb, ub;
    lb.reserve(totalRows);
    ub.reserve(totalRows);
    // ═══════════════════════════════════════════════════════════════════════════
    
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

    // ═══════════════════════════════════════════════════════════════════════════
    // PHASE 4 OPTIMIZATION #4: Build sparse matrix (single allocation)
    // ═══════════════════════════════════════════════════════════════════════════
    A.resize(row, n);
    A.reserve(static_cast<int>(trips.size()));  // Pre-allocate internal storage
    A.setFromTriplets(trips.begin(), trips.end());
    A.makeCompressed();  // Ensure compressed column format for OSQP
    
    lbVec = Eigen::Map<Eigen::VectorXd>(lb.data(), (int)lb.size());
    ubVec = Eigen::Map<Eigen::VectorXd>(ub.data(), (int)ub.size());
}

// ──────────────────────────────────────────────────────────────────────────────
// Helper: Eigen SparseMatrix → OSQPCscMatrix (caller owns the memory)
// OSQP v1.x uses OSQPCscMatrix instead of the old csc_matrix() function.
// ═══════════════════════════════════════════════════════════════════════════════
// PHASE 5 ROBUSTNESS #2: Added null checks for all malloc calls
// ═══════════════════════════════════════════════════════════════════════════════
// ──────────────────────────────────────────────────────────────────────────────
static OSQPCscMatrix* eigenToOsqp(const Eigen::SparseMatrix<double, Eigen::ColMajor>& M)
{
    int nnz  = (int)M.nonZeros();
    int rows = (int)M.rows();
    int cols = (int)M.cols();
    
    // Allocate main structure with null check
    OSQPCscMatrix* out = (OSQPCscMatrix*)malloc(sizeof(OSQPCscMatrix));
    if (!out) {
        throw validation::AllocationError("OSQPCscMatrix", sizeof(OSQPCscMatrix));
    }

    // Allocate arrays with null checks
    OSQPFloat* x = (OSQPFloat*)malloc(nnz * sizeof(OSQPFloat));
    if (!x) {
        free(out);
        throw validation::AllocationError("OSQPCscMatrix::x", nnz * sizeof(OSQPFloat));
    }
    
    OSQPInt* i = (OSQPInt*)malloc(nnz * sizeof(OSQPInt));
    if (!i) {
        free(x);
        free(out);
        throw validation::AllocationError("OSQPCscMatrix::i", nnz * sizeof(OSQPInt));
    }
    
    OSQPInt* p = (OSQPInt*)malloc((cols+1) * sizeof(OSQPInt));
    if (!p) {
        free(i);
        free(x);
        free(out);
        throw validation::AllocationError("OSQPCscMatrix::p", (cols+1) * sizeof(OSQPInt));
    }

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
    out->owned = 0;    // We manage memory manually
    return out;
}

static void freeOsqpCsc(OSQPCscMatrix* m) {
    if (!m) return;
    free(m->x); free(m->i); free(m->p);
    free(m);
}

// ═══════════════════════════════════════════════════════════════════════════
// PHASE 4 OPTIMIZATION #3: Optimized OSQP Settings
// PHASE 5 ROBUSTNESS #2: Added null check for malloc
// ═══════════════════════════════════════════════════════════════════════════
OSQPSettings* QPSolver::createOptimizedSettings() const {
    OSQPSettings* settings = (OSQPSettings*)malloc(sizeof(OSQPSettings));
    if (!settings) {
        throw validation::AllocationError("OSQPSettings", sizeof(OSQPSettings));
    }
    osqp_set_default_settings(settings);
    
    // ─────────────────────────────────────────────────────────────────────────
    // Tolerance settings - tighter than default for financial applications
    // ─────────────────────────────────────────────────────────────────────────
    settings->eps_abs = config_.tolerance;        // Default: 1e-3, we use: 1e-6
    settings->eps_rel = config_.tolerance;        // Match absolute tolerance
    settings->eps_prim_inf = 1e-6;               // Primal infeasibility detection
    settings->eps_dual_inf = 1e-6;               // Dual infeasibility detection
    
    // ─────────────────────────────────────────────────────────────────────────
    // Iteration limits
    // ─────────────────────────────────────────────────────────────────────────
    settings->max_iter = config_.maxIterations;   // Default: 4000, we use: 10000
    settings->check_termination = 25;             // Check every 25 iterations
    
    // ─────────────────────────────────────────────────────────────────────────
    // Performance settings - Note: 'polish' removed in OSQP v1.x
    // ─────────────────────────────────────────────────────────────────────────
    
    // Adaptive rho (step size parameter) - tune for faster convergence
    settings->adaptive_rho = true;                // Enable adaptive rho
    settings->adaptive_rho_interval = 50;         // Adapt every 50 iterations
    settings->adaptive_rho_tolerance = 2.0;       // Tolerance for adaptation
    settings->adaptive_rho_fraction = 0.4;        // Fraction of admm steps
    
    // ─────────────────────────────────────────────────────────────────────────
    // Linear system solver settings
    // ─────────────────────────────────────────────────────────────────────────
    settings->linsys_solver = OSQP_DIRECT_SOLVER;  // Direct sparse solver (QDLDL)
    settings->scaled_termination = true;          // Scale termination criteria
    
    // ─────────────────────────────────────────────────────────────────────────
    // Scaling settings (important for numerical stability)
    // ─────────────────────────────────────────────────────────────────────────
    settings->scaling = 10;                       // Number of scaling iterations
    
    // ─────────────────────────────────────────────────────────────────────────
    // Verbosity and warm starting
    // ─────────────────────────────────────────────────────────────────────────
    settings->verbose = config_.verbose ? 1 : 0;
    settings->warm_starting = true;               // Enable warm starting for repeated solves
    
    return settings;
}
// ═══════════════════════════════════════════════════════════════════════════
// END PHASE 4 OPTIMIZATION #3
// ═══════════════════════════════════════════════════════════════════════════

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

    // OSQP settings - use optimized settings from Phase 4
    OSQPSettings* settings = createOptimizedSettings();

    // Setup and solve
    OSQPSolver* solver = nullptr;
    OSQPInt exitflag = osqp_setup(&solver, P_csc, q_osqp.data(), A_csc,
                                   l_osqp.data(), u_osqp.data(), 
                                   A_csc->m, A_csc->n, settings);
    
    QPResult result;
    
    // Check setup success
    if (exitflag != 0 || solver == nullptr) {
        result.success = false;
        result.status = "SETUP_FAILED: Could not initialize QP solver";
        result.objectiveValue = 0.0;
        result.regularizationPenalty = 0.0;
        result.iterations = 0;
        std::cerr << "QP Solver setup failed with exit flag: " << exitflag << std::endl;
        
        freeOsqpCsc(P_csc);
        freeOsqpCsc(A_csc);
        free(settings);
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        result.solveTime = duration.count() / 1000.0;
        return result;
    }
    
    // Solve the QP
    exitflag = osqp_solve(solver);
    
    // ═══════════════════════════════════════════════════════════════════════
    // PHASE 2 FIX #3: Validate QP solver results
    // ═══════════════════════════════════════════════════════════════════════
    
    // Step 1: Check OSQP status code
    OSQPInt status_val = solver->info->status_val;
    if (status_val != OSQP_SOLVED && status_val != OSQP_SOLVED_INACCURATE) {
        result.success = false;
        result.objectiveValue = 0.0;
        result.regularizationPenalty = 0.0;
        result.iterations = solver->info->iter;
        
        // Map OSQP status to readable string
        switch (status_val) {
            case OSQP_PRIMAL_INFEASIBLE:
                result.status = "PRIMAL_INFEASIBLE: Constraints are contradictory, no solution exists";
                break;
            case OSQP_DUAL_INFEASIBLE:
                result.status = "DUAL_INFEASIBLE: Problem is unbounded";
                break;
            case OSQP_MAX_ITER_REACHED:
                result.status = "MAX_ITER_REACHED: Solver did not converge within iteration limit";
                break;
            default:
                result.status = "UNKNOWN_FAILURE: OSQP returned status code " + 
                              std::to_string(status_val);
        }
        
        std::cerr << "QP Solver failed: " << result.status << std::endl;
        osqp_cleanup(solver);
        freeOsqpCsc(P_csc);
        freeOsqpCsc(A_csc);
        free(settings);
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        result.solveTime = duration.count() / 1000.0;
        return result;
    }
    
    // Step 2: Validate solution vector (check for NaN/Inf)
    bool validSolution = true;
    for (int i = 0; i < n; ++i) {
        if (!std::isfinite(solver->solution->x[i])) {
            validSolution = false;
            break;
        }
    }
    
    if (!validSolution) {
        result.success = false;
        result.status = "INVALID_SOLUTION: Solution vector contains NaN or Inf values";
        result.objectiveValue = 0.0;
        result.regularizationPenalty = 0.0;
        result.iterations = solver->info->iter;
        
        std::cerr << "QP Solver produced invalid solution (NaN/Inf)" << std::endl;
        osqp_cleanup(solver);
        freeOsqpCsc(P_csc);
        freeOsqpCsc(A_csc);
        free(settings);
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        result.solveTime = duration.count() / 1000.0;
        return result;
    }
    
    // Step 3: Validate bounds - all volatilities must be in [minVol, maxVol]
    bool boundsValid = true;
    double tolerance = 1e-6;  // Small tolerance for numerical errors
    for (int i = 0; i < n; ++i) {
        if (solver->solution->x[i] < config_.minVol - tolerance || 
            solver->solution->x[i] > config_.maxVol + tolerance) {
            boundsValid = false;
            break;
        }
    }
    
    if (!boundsValid) {
        result.success = false;
        result.status = "BOUNDS_VIOLATED: Solution contains volatilities outside allowed range [" +
                      std::to_string(config_.minVol) + ", " + std::to_string(config_.maxVol) + "]";
        result.objectiveValue = 0.0;
        result.regularizationPenalty = 0.0;
        result.iterations = solver->info->iter;
        
        std::cerr << "QP Solution violates bounds" << std::endl;
        osqp_cleanup(solver);
        freeOsqpCsc(P_csc);
        freeOsqpCsc(A_csc);
        free(settings);
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        result.solveTime = duration.count() / 1000.0;
        return result;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // END PHASE 2 FIX #3 - All validation passed, extract results
    // ═══════════════════════════════════════════════════════════════════════
    
    result.success = true;
    result.ivFlat = Eigen::Map<Eigen::VectorXd>(solver->solution->x, n);
    result.objectiveValue = solver->info->obj_val;
    result.iterations = solver->info->iter;
    
    // Set status based on OSQP result
    if (status_val == OSQP_SOLVED) {
        result.status = "SOLVED: Optimal solution found";
    } else if (status_val == OSQP_SOLVED_INACCURATE) {
        result.status = "SOLVED_INACCURATE: Solution found but tolerances not fully met";
    }
    
    // Calculate regularization penalties
    result.regularizationPenalty = 
        calculateSmoothnessPenalty(result.ivFlat) + 
        calculateMarketPreservationPenalty(result.ivFlat);

    // ═══════════════════════════════════════════════════════════════════════
    // PHASE 3 IMPROVEMENT #2: Verify nonlinear calendar constraint
    // ═══════════════════════════════════════════════════════════════════════
    if (config_.enableNonlinearCalendarCheck) {
        std::vector<std::tuple<int, int, double>> violations;
        bool calendarOK = verifyCalendarConstraint(result.ivFlat, violations);
        
        if (!calendarOK) {
            std::cerr << "Warning: " << violations.size() 
                      << " calendar constraint violations detected (nonlinear check)" << std::endl;
            
            if (config_.enableCalendarRefinement) {
                result = refineCalendarConstraint(result, 3);
            } else {
                result.status += " [" + std::to_string(violations.size()) + " calendar violations]";
            }
        } else {
            result.status += " [Calendar verified]";
        }
    }

    // Cleanup
    osqp_cleanup(solver);
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
            double smoothness = std::abs(ivFlat(idx(i, j-1)) - 2.0*ivFlat(idx(i, j)) + ivFlat(idx(i, j+1)));
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
    return VolSurface(quotes, surface_.marketData());
}

// ═══════════════════════════════════════════════════════════════════════════
// PHASE 3 IMPROVEMENT #2: Enhanced Calendar Constraint with Nonlinear Verification
// ═══════════════════════════════════════════════════════════════════════════

double QPSolver::computeCalendarViolation(
    const Eigen::VectorXd& ivFlat,
    int expiry0_idx,
    int expiry1_idx,
    int strike_idx) const 
{
    const auto& Ts = surface_.expiries();
    double T0 = Ts[expiry0_idx];
    double T1 = Ts[expiry1_idx];
    
    double sigma0 = ivFlat[idx(expiry0_idx, strike_idx)];
    double sigma1 = ivFlat[idx(expiry1_idx, strike_idx)];
    
    // True constraint: w₁ ≥ w₀ where w = σ²T (total variance)
    double w0 = sigma0 * sigma0 * T0;
    double w1 = sigma1 * sigma1 * T1;
    
    return w0 - w1;  // > 0 means violation
}

bool QPSolver::verifyCalendarConstraint(
    const Eigen::VectorXd& ivFlat,
    std::vector<std::tuple<int, int, double>>& violations) const 
{
    violations.clear();
    
    const auto& Ts = surface_.expiries();
    int nE = nExpiries();
    int nK = nStrikes();
    
    bool allSatisfied = true;
    
    // Check all calendar pairs
    for (int ei0 = 0; ei0 + 1 < nE; ++ei0) {
        for (int ei1 = ei0 + 1; ei1 < nE; ++ei1) {
            double T0 = Ts[ei0];
            double T1 = Ts[ei1];
            
            for (int ki = 0; ki < nK; ++ki) {
                double sigma0 = ivFlat[idx(ei0, ki)];
                double sigma1 = ivFlat[idx(ei1, ki)];
                
                // True constraint: σ₁²T₁ ≥ σ₀²T₀
                double w0 = sigma0 * sigma0 * T0;
                double w1 = sigma1 * sigma1 * T1;
                
                double violation = w0 - w1;  // Should be ≤ 0
                
                if (violation > config_.calendarViolationTol) {
                    violations.emplace_back(ei0, ki, violation);
                    allSatisfied = false;
                }
            }
        }
    }
    
    return allSatisfied;
}

QPResult QPSolver::refineCalendarConstraint(
    const QPResult& initialResult,
    int maxRefinementIterations) const 
{
    if (!initialResult.success) {
        return initialResult;  // Don't refine if initial solve failed
    }
    
    QPResult refined = initialResult;
    Eigen::VectorXd ivFlat = initialResult.ivFlat;
    
    std::vector<std::tuple<int, int, double>> violations;
    
    for (int iter = 0; iter < maxRefinementIterations; ++iter) {
        // Check current solution
        bool satisfied = verifyCalendarConstraint(ivFlat, violations);
        
        if (satisfied) {
            refined.status += " [Calendar verified]";
            return refined;
        }
        
        // For each violation, project to feasible region
        // Simple projection: increase σ₁ or decrease σ₀ to satisfy w₁ ≥ w₀
        const auto& Ts = surface_.expiries();
        
        for (const auto& [ei0, ki, viol] : violations) {
            int ei1 = ei0 + 1;
            double T0 = Ts[ei0];
            double T1 = Ts[ei1];
            double sigma0 = ivFlat[idx(ei0, ki)];
            double sigma1 = ivFlat[idx(ei1, ki)];
            
            // Need: σ₁²T₁ ≥ σ₀²T₀
            // Target: σ₁_new² = σ₀²T₀/T₁ + ε for some small ε
            double w0 = sigma0 * sigma0 * T0;
            double sigma1_min = std::sqrt((w0 + 1e-6) / T1);
            
            // Blend: increase sigma1 towards minimum needed
            if (sigma1 < sigma1_min) {
                double alpha = 0.5;  // Relaxation factor
                ivFlat[idx(ei1, ki)] = sigma1 + alpha * (sigma1_min - sigma1);
            }
        }
        
        // Update refined result
        refined.ivFlat = ivFlat;
        
        std::cerr << "Calendar refinement iteration " << iter + 1 
                  << ": " << violations.size() << " violations" << std::endl;
    }
    
    // Final check
    bool finalSatisfied = verifyCalendarConstraint(ivFlat, violations);
    if (finalSatisfied) {
        refined.status += " [Calendar verified after refinement]";
    } else {
        refined.status += " [Calendar partially refined, " + 
                         std::to_string(violations.size()) + " violations remain]";
    }
    
    return refined;
}