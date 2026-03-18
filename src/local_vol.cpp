#include "local_vol.hpp"
#include <cmath>
#include <iostream>
#include <iomanip>

LocalVolSurface::LocalVolSurface(const VolSurface& surface)
    : surface_(surface)
{
    buildGrid();
}

// ── Finite difference step sizes ─────────────────────────────────────────────
// Use 1% of the node spacing for numerical stability
static double dK_step(const VolSurface& s) {
    const auto& K = s.strikes();
    return (K.size() > 1) ? 0.01 * (K.back() - K.front()) : 1.0;
}
static double dT_step(const VolSurface& s) {
    const auto& T = s.expiries();
    return (T.size() > 1) ? 0.01 * (T.back() - T.front()) : 0.01;
}

double LocalVolSurface::dCdT(double K, double T) const {
    double h = dT_step(surface_);
    double Tlo = T - h, Thi = T + h;
    // One-sided if near boundary
    const auto& Ts = surface_.expiries();
    if (Tlo < Ts.front()) return (surface_.callPrice(K, T+h) - surface_.callPrice(K, T)) / h;
    if (Thi > Ts.back())  return (surface_.callPrice(K, T)   - surface_.callPrice(K, T-h)) / h;
    return (surface_.callPrice(K, T+h) - surface_.callPrice(K, T-h)) / (2.0*h);
}

double LocalVolSurface::d2CdK2(double K, double T) const {
    double h = dK_step(surface_);
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