/**
 * @file local_vol.hpp
 * @brief Dupire local volatility surface computation
 * @author vol_arb Team
 * @version 2.0
 * @date 2024
 *
 * Computes the Dupire local volatility surface from an implied volatility
 * surface using the Breeden-Litzenberger formula:
 *
 *     σ²_local(K,T) = (∂C/∂T) / (0.5 · K² · ∂²C/∂K²)
 *
 * ## Prerequisites
 * The input surface MUST be arbitrage-free (specifically, ∂²C/∂K² > 0)
 * for the local volatility to be well-defined.
 *
 * ## References
 * - Dupire, B. (1994). "Pricing with a Smile"
 * - Gatheral, J. (2006). "The Volatility Surface"
 *
 * @warning Only use this with an arbitrage-free surface (post-QP correction).
 * @see QPSolver for arbitrage repair
 */

#pragma once
#include "vol_surface.hpp"
#include <Eigen/Dense>

/**
 * @brief Dupire local volatility surface
 *
 * Computes local volatility using central finite differences on call prices.
 * The local volatility is undefined (NaN) where the denominator is non-positive.
 *
 * ## Example Usage
 * @code
 * // First ensure surface is arbitrage-free
 * VolSurface corrected = solver.buildCorrectedSurface(qpResult);
 *
 * // Compute local vol
 * LocalVolSurface localVol(corrected);
 *
 * // Query local vol at specific point
 * double lv = localVol.localVol(100.0, 0.5);
 * if (!std::isnan(lv)) {
 *     std::cout << "Local vol at (100, 0.5): " << lv << std::endl;
 * }
 *
 * // Check all positive
 * if (localVol.allPositive()) {
 *     std::cout << "Surface is arbitrage-free" << std::endl;
 * }
 * @endcode
 */
class LocalVolSurface {
public:
    /**
     * @brief Construct local vol surface from implied vol surface
     *
     * @param surface Input volatility surface (should be arbitrage-free)
     *
     * Precomputes local volatility at all grid points.
     */
    explicit LocalVolSurface(const VolSurface& surface);

    /**
     * @brief Get local volatility at arbitrary point
     *
     * @param strike Strike price
     * @param expiry Time to expiry
     * @return Local volatility, or NaN if undefined
     *
     * Uses interpolation between grid points.
     * Returns NaN if denominator (∂²C/∂K²) is non-positive.
     */
    double localVol(double strike, double expiry) const;

    /**
     * @brief Get precomputed local vol grid
     * @return Const reference to grid (rows=expiry, cols=strike)
     */
    const Eigen::MatrixXd& localVolGrid() const { return lvGrid_; }

    /**
     * @brief Print local vol surface to stdout
     */
    void print() const;

    /**
     * @brief Check if all local vols are positive
     * @return True if no NaN or non-positive values
     *
     * All-positive local vol indicates an arbitrage-free surface.
     */
    bool allPositive() const;

private:
    const VolSurface& surface_;   ///< Reference to input surface
    Eigen::MatrixXd lvGrid_;      ///< Local vol grid (rows=expiry, cols=strike)

    void buildGrid();             ///< Compute grid values

    double dCdT(double K, double T) const;    ///< ∂C/∂T via finite diff
    double d2CdK2(double K, double T) const;  ///< ∂²C/∂K² via finite diff
};
