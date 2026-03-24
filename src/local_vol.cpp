#include "local_vol.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <iomanip>

LocalVolSurface::LocalVolSurface(const VolSurface& surface)
    : surface_(surface)
{
    buildGrid();
}

// ═══════════════════════════════════════════════════════════════════════════
// PHASE 3 IMPROVEMENT #3: Adaptive Finite Difference Step Sizes
// Replace fixed step sizes with adaptive computation based on local curvature
// ═══════════════════════════════════════════════════════════════════════════

// Estimate optimal step size for dC/dT using Richardson extrapolation error analysis
static double computeOptimalStepT(const VolSurface& surface, double K, double T) {
    // Rule: h* = (3ε|f|/|f'''|)^(1/3) where ε is machine precision
    // For option prices: f ≈ O(sqrt(T)), f''' ≈ O(T^(-3/2))
    
    const double eps = std::numeric_limits<double>::epsilon();
    
    // Estimate |f| = |C(K,T)|
    double f_val = std::abs(surface.callPrice(K, T));
    if (f_val < 1e-10) f_val = 1e-10; // Prevent division by zero
    
    // Estimate |f'''| using 4th-order finite difference: f''' ≈ [f(t+2h) - 2f(t+h) + 2f(t-h) - f(t-2h)] / (2h³)
    double h_test = std::min(0.1 * T, 0.01); // Initial test step
    if (h_test < eps) h_test = eps;
    
    const auto& Ts = surface.expiries();
    double T_min = Ts.front();
    double T_max = Ts.back();
    
    // Clamp evaluation points to valid domain
    double T1 = std::clamp(T + 2*h_test, T_min, T_max);
    double T2 = std::clamp(T + h_test, T_min, T_max);
    double T3 = std::clamp(T - h_test, T_min, T_max);
    double T4 = std::clamp(T - 2*h_test, T_min, T_max);
    
    double f1 = surface.callPrice(K, T1);
    double f2 = surface.callPrice(K, T2);
    double f3 = surface.callPrice(K, T3);
    double f4 = surface.callPrice(K, T4);
    
    // Third derivative estimate (handle edge cases gracefully)
    double f_triple = std::abs(f1 - 2*f2 + 2*f3 - f4) / (2.0 * h_test * h_test * h_test);
    if (f_triple < 1e-10) f_triple = 1e-10; // Fallback for smooth surfaces
    
    // Optimal step: h* = (3ε|f|/|f'''|)^(1/3)
    double h_opt = std::pow(3.0 * eps * f_val / f_triple, 1.0/3.0);
    
    // Clamp to reasonable bounds
    double h_min = 10.0 * eps;
    double h_max = std::min(0.1 * T, (T_max - T_min) / 100.0);
    
    return std::clamp(h_opt, h_min, h_max);
}

// Estimate optimal step size for dC/dK using similar analysis
static double computeOptimalStepK(const VolSurface& surface, double K, double T) {
    const double eps = std::numeric_limits<double>::epsilon();
    
    // Estimate |f| = |C(K,T)|
    double f_val = std::abs(surface.callPrice(K, T));
    if (f_val < 1e-10) f_val = 1e-10;
    
    // Estimate |f'''| for strike direction
    double h_test = 0.01 * K; // 1% of current strike
    if (h_test < eps) h_test = eps;
    
    const auto& Ks = surface.strikes();
    double K_min = Ks.front();
    double K_max = Ks.back();
    
    // Clamp evaluation points
    double K1 = std::clamp(K + 2*h_test, K_min, K_max);
    double K2 = std::clamp(K + h_test, K_min, K_max);
    double K3 = std::clamp(K - h_test, K_min, K_max);
    double K4 = std::clamp(K - 2*h_test, K_min, K_max);
    
    double f1 = surface.callPrice(K1, T);
    double f2 = surface.callPrice(K2, T);
    double f3 = surface.callPrice(K3, T);
    double f4 = surface.callPrice(K4, T);
    
    // Third derivative estimate
    double f_triple = std::abs(f1 - 2*f2 + 2*f3 - f4) / (2.0 * h_test * h_test * h_test);
    if (f_triple < 1e-10) f_triple = 1e-10;
    
    // Optimal step
    double h_opt = std::pow(3.0 * eps * f_val / f_triple, 1.0/3.0);
    
    // Clamp to reasonable bounds
    double h_min = 10.0 * eps;
    double h_max = std::min(0.1 * K, (K_max - K_min) / 100.0);
    
    return std::clamp(h_opt, h_min, h_max);
}

// Legacy fixed step functions (fallback) removed to fix unused warnings

double LocalVolSurface::dCdT(double K, double T) const {
    // ═══════════════════════════════════════════════════════════════════════════
    // PHASE 3 IMPROVEMENT #3: Use adaptive step size instead of fixed
    // ═══════════════════════════════════════════════════════════════════════════
    double h = computeOptimalStepT(surface_, K, T);
    
    const auto& Ts = surface_.expiries();
    
    // Get valid time range
    double T_min = Ts.front();
    double T_max = Ts.back();
    
    // Clamp T to valid range to handle out-of-bounds queries gracefully
    double T_clamped = std::clamp(T, T_min, T_max);
    
    // Compute candidate points for finite difference
    double Tlo = T_clamped - h;
    double Thi = T_clamped + h;
    
    // Case 1: Near lower boundary - use forward difference
    if (Tlo < T_min) {
        // Adjust step size to stay in bounds if needed
        h = std::min(h, (T_max - T_clamped) / 2.0);
        if (h < 1e-8) h = 1e-8;  // Minimum step size
        
        double C0 = surface_.callPrice(K, T_clamped);
        double C1 = surface_.callPrice(K, T_clamped + h);
        return (C1 - C0) / h;
    }
    
    // Case 2: Near upper boundary - use backward difference
    if (Thi > T_max) {
        // Adjust step size to stay in bounds if needed
        h = std::min(h, (T_clamped - T_min) / 2.0);
        if (h < 1e-8) h = 1e-8;  // Minimum step size
        
        double C0 = surface_.callPrice(K, T_clamped);
        double C1 = surface_.callPrice(K, T_clamped - h);
        return (C0 - C1) / h;
    }
    
    // Case 3: Interior point - use centered difference (most accurate)
    double Clo = surface_.callPrice(K, Tlo);
    double Chi = surface_.callPrice(K, Thi);
    return (Chi - Clo) / (2.0 * h);
}

double LocalVolSurface::d2CdK2(double K, double T) const {
    // ═══════════════════════════════════════════════════════════════════════════
    // PHASE 3 IMPROVEMENT #3: Use adaptive step size for second derivative
    // ═══════════════════════════════════════════════════════════════════════════
    double h = computeOptimalStepK(surface_, K, T);
    double Cu = surface_.callPrice(K+h, T);
    double Cm = surface_.callPrice(K,   T);
    double Cd = surface_.callPrice(K-h, T);
    return (Cu - 2.0*Cm + Cd) / (h*h);
}

void LocalVolSurface::buildGrid() {
    const auto& Ks = surface_.strikes();
    const auto& Ts = surface_.expiries();
    int nE = (int)Ts.size(), nK = (int)Ks.size();
    lvGrid_.resize(nE, nK);

    for (int i = 0; i < nE; ++i) {
        for (int j = 0; j < nK; ++j) {
            double K = Ks[j], T = Ts[i];
            double num   = dCdT(K, T);
            double denom = 0.5 * K * K * d2CdK2(K, T);
            if (denom <= 1e-12) {
                lvGrid_(i, j) = std::numeric_limits<double>::quiet_NaN();
            } else {
                double lv2 = num / denom;
                lvGrid_(i, j) = (lv2 > 0) ? std::sqrt(lv2)
                                           : std::numeric_limits<double>::quiet_NaN();
            }
        }
    }
}

double LocalVolSurface::localVol(double strike, double expiry) const {
    // Bilinear interpolation of local vol grid (same logic as VolSurface)
    const auto& Ks = surface_.strikes();
    const auto& Ts = surface_.expiries();
    int nK = (int)Ks.size(), nE = (int)Ts.size();

    double K = std::clamp(strike, Ks.front(), Ks.back());
    double T = std::clamp(expiry, Ts.front(), Ts.back());

    int ki = (int)(std::lower_bound(Ks.begin(), Ks.end(), K) - Ks.begin());
    int ei = (int)(std::lower_bound(Ts.begin(), Ts.end(), T) - Ts.begin());
    ki = std::min(ki, nK-1);
    ei = std::min(ei, nE-1);
    int ki0 = std::max(ki-1,0), ki1 = ki;
    int ei0 = std::max(ei-1,0), ei1 = ei;

    double K0 = Ks[ki0], K1 = Ks[ki1];
    double T0 = Ts[ei0], T1 = Ts[ei1];
    double wK = (K1>K0) ? (K-K0)/(K1-K0) : 0.0;
    double wT = (T1>T0) ? (T-T0)/(T1-T0) : 0.0;

    auto lv = [&](int i, int j) -> double {
        double v = lvGrid_(i,j);
        return std::isnan(v) ? 0.20 : v;  // fallback to 20% if NaN
    };
    return (1-wT)*((1-wK)*lv(ei0,ki0) + wK*lv(ei0,ki1))
         +    wT *((1-wK)*lv(ei1,ki0) + wK*lv(ei1,ki1));
}

bool LocalVolSurface::allPositive() const {
    for (int i = 0; i < (int)lvGrid_.rows(); ++i)
        for (int j = 0; j < (int)lvGrid_.cols(); ++j)
            if (std::isnan(lvGrid_(i,j)) || lvGrid_(i,j) <= 0) return false;
    return true;
}

void LocalVolSurface::print() const {
    const auto& Ks = surface_.strikes();
    const auto& Ts = surface_.expiries();
    std::cout << "\n=== Dupire Local Volatility Surface (%) ===\n";
    std::cout << std::fixed << std::setprecision(1);
    std::cout << std::setw(8) << "T\\K";
    for (double K : Ks) std::cout << std::setw(8) << K;
    std::cout << "\n";
    for (int i = 0; i < (int)Ts.size(); ++i) {
        std::cout << std::setw(8) << Ts[i];
        for (int j = 0; j < (int)Ks.size(); ++j) {
            double v = lvGrid_(i,j);
            if (std::isnan(v)) std::cout << std::setw(8) << "NaN";
            else                std::cout << std::setw(8) << v * 100.0;
        }
        std::cout << "\n";
    }
    std::cout << "\n";
}