#include "qp_solver.hpp"
#include <osqp/osqp.h>
#include <Eigen/Sparse>
#include <cmath>
#include <iostream>
#include <cstring>

QPSolver::QPSolver(const VolSurface& surface) : surface_(surface) {}

int QPSolver::nStrikes()  const { return (int)surface_.strikes().size(); }
int QPSolver::nExpiries() const { return (int)surface_.expiries().size(); }
int QPSolver::idx(int ei, int ki) const { return ei * nStrikes() + ki; }

// ──────────────────────────────────────────────────────────────────────────────
// Butterfly: σ(K-1) - 2σ(K) + σ(K+1) >= 0
// ──────────────────────────────────────────────────────────────────────────────
void QPSolver::addButterflyRow(
    std::vector<Eigen::Triplet<double>>& trips,
    std::vector<double>& lb, std::vector<double>& ub,
    int& row, int ei, int ki) const
{
    trips.emplace_back(row, idx(ei, ki-1),  1.0);
    trips.emplace_back(row, idx(ei, ki),   -2.0);
    trips.emplace_back(row, idx(ei, ki+1),  1.0);
    lb.push_back(0.0);
    ub.push_back(OSQP_INFTY);   // FIX 1: use OSQP_INFTY, not 1e9
    ++row;
}

// ──────────────────────────────────────────────────────────────────────────────
// Calendar: σ(T+1,K) - σ(T,K) >= 0
// ──────────────────────────────────────────────────────────────────────────────
void QPSolver::addCalendarRow(
    std::vector<Eigen::Triplet<double>>& trips,
    std::vector<double>& lb, std::vector<double>& ub,
    int& row, int ei, int ki) const
{
    // No-calendar-arbitrage condition: total variance must be non-decreasing.
    // Total variance w(T) = σ²·T, so we need σ²(T+1)·T+1 >= σ²(T)·T.
    // Linearized: T+1·σ(T+1) >= T·σ(T)
    // This is much weaker than raw IV monotonicity and matches real market data.
    double T0 = surface_.expiries()[ei];
    double T1 = surface_.expiries()[ei + 1];
    trips.emplace_back(row, idx(ei+1, ki),  T1);
    trips.emplace_back(row, idx(ei,   ki), -T0);
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

    // Bounds: 0.001 <= σ <= 5.0
    for (int k = 0; k < n; ++k) {
        trips.emplace_back(row, k, 1.0);
        lb.push_back(0.001);
        ub.push_back(5.0);
        ++row;
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
// Solve  min (1/2)||x - iv_mkt||²  s.t. constraints
// OSQP solves (1/2)x'Px + q'x, so for ||x - iv_mkt||² = x'x - 2*iv_mkt'x + const
// we need P = 2*I (upper triangular) and q = -2*iv_mkt
// ──────────────────────────────────────────────────────────────────────────────
QPResult QPSolver::solve() const {
    int nE = nExpiries(), nK = nStrikes(), n = nE * nK;

    // Market IV vector
    Eigen::VectorXd ivMkt(n);
    const auto& grid = surface_.ivGrid();
    for (int i = 0; i < nE; ++i)
        for (int j = 0; j < nK; ++j)
            ivMkt(idx(i,j)) = grid(i,j);

    // FIX 2: OSQP solves (1/2)x'Px + q'x
    // To get ||x - iv_mkt||² = x'Ix - 2*iv_mkt'x + const,
    // set P = 2*I (upper triangular only) and q = -2*iv_mkt
    // This makes the OSQP objective equal (1/2)x'(2I)x + (-2*iv_mkt)'x
    //                                    = x'x - 2*iv_mkt'x = ||x - iv_mkt||² - const
    Eigen::SparseMatrix<double, Eigen::ColMajor> P(n, n);
    {
        std::vector<Eigen::Triplet<double>> t;
        for (int k = 0; k < n; ++k) t.emplace_back(k, k, 2.0);  // 2*I upper triangular
        P.setFromTriplets(t.begin(), t.end());
    }
    P.makeCompressed();

    // Constraint matrix
    Eigen::SparseMatrix<double> Adyn;
    Eigen::VectorXd lb, ub;
    buildConstraints(Adyn, lb, ub);
    Eigen::SparseMatrix<double, Eigen::ColMajor> A = Adyn;
    A.makeCompressed();

    int m = (int)lb.size();

    // Convert to OSQP CSC structs
    OSQPCscMatrix* P_csc = eigenToOsqp(P);
    OSQPCscMatrix* A_csc = eigenToOsqp(A);

    // q = -2 * iv_mkt
    std::vector<OSQPFloat> q(n);
    for (int k = 0; k < n; ++k) q[k] = (OSQPFloat)(-2.0 * ivMkt(k));  // FIX 2

    std::vector<OSQPFloat> l(m), u(m);
    for (int k = 0; k < m; ++k) { l[k] = (OSQPFloat)lb(k); u[k] = (OSQPFloat)ub(k); }

    // Settings
    OSQPSettings* settings = (OSQPSettings*)malloc(sizeof(OSQPSettings));
    osqp_set_default_settings(settings);
    settings->verbose  = 0;
    settings->eps_abs  = 1e-8;
    settings->eps_rel  = 1e-8;
    settings->max_iter = 10000;

    // OSQP v1.x: OSQPSolver*
    OSQPSolver* solver = nullptr;
    OSQPInt exitflag = osqp_setup(&solver, P_csc, q.data(),
                                  A_csc, l.data(), u.data(),
                                  (OSQPInt)m, (OSQPInt)n, settings);

    QPResult result;
    if (exitflag != 0 || !solver) {
        result.success = false;
        result.status  = "OSQP setup failed (code " + std::to_string((int)exitflag) + ")";
        freeOsqpCsc(P_csc); freeOsqpCsc(A_csc); free(settings);
        return result;
    }

    osqp_solve(solver);

    const OSQPSolution* sol = solver->solution;
    const OSQPInfo*     inf = solver->info;

    result.success        = (inf->status_val == OSQP_SOLVED);
    result.objectiveValue = (double)inf->obj_val;
    result.status         = std::string(inf->status);

    result.ivFlat.resize(n);
    for (int k = 0; k < n; ++k)
        result.ivFlat(k) = (sol && sol->x) ? (double)sol->x[k] : ivMkt(k);

    osqp_cleanup(solver);
    freeOsqpCsc(P_csc);
    freeOsqpCsc(A_csc);
    free(settings);

    return result;
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