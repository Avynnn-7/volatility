#pragma once
#include "vol_surface.hpp"
#include <Eigen/Dense>

// Computes Dupire local volatility from a fitted (arbitrage-free) vol surface.
// Uses central finite differences on call prices.
// σ²_local(K,T) = (∂C/∂T) / (0.5 · K² · ∂²C/∂K²)
//
// IMPORTANT: Only call this on an arbitrage-free surface (post-QP correction).
// If ∂²C/∂K² <= 0 anywhere, local vol is undefined — this returns NaN there.
class LocalVolSurface {
public:
    explicit LocalVolSurface(const VolSurface& surface);

    // Local vol at arbitrary (K,T) — returns NaN if denominator <= 0
    double localVol(double strike, double expiry) const;

    // Grid of local vols at the surface's own nodes
    const Eigen::MatrixXd& localVolGrid() const { return lvGrid_; }

    // Pretty-print the local vol surface
    void print() const;

    // Check: local vol should be positive everywhere on a clean surface
    bool allPositive() const;

private:
    const VolSurface& surface_;
    Eigen::MatrixXd   lvGrid_;   // rows = expiry, cols = strike

    void buildGrid();

    double dCdT  (double K, double T) const;
    double d2CdK2(double K, double T) const;
};