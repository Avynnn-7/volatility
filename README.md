# vol-arb — Volatility Surface Arbitrage Detection & Correction

A C++17 library that detects static arbitrage in options markets and projects
the implied volatility surface onto the arbitrage-free cone via Quadratic
Programming. Returns a formal KKT dual certificate proving the corrected
surface is arbitrage-free.

**No equivalent open-source C++ implementation exists.**

## What it does

Given market option quotes (strikes × expiries), the pipeline:

1. Builds an implied volatility surface with bilinear interpolation
2. Detects **butterfly**, **calendar**, and **monotonicity** violations via
   finite differences on call prices
3. Solves a **Quadratic Program** (OSQP v1.x) projecting the surface onto
   the arbitrage-free cone — the closest arbitrage-free surface in L² distance
4. Verifies the solution via **KKT dual certificate** (stationarity,
   complementary slackness, dual feasibility)
5. Computes **Dupire local volatility** from the corrected surface

## Mathematics

### No-Arbitrage Conditions

| Condition | Mathematical form | Economic meaning |
|---|---|---|
| Butterfly-free | $\partial^2 C / \partial K^2 \geq 0$ | Risk-neutral density is non-negative |
| Calendar-free | $\partial w / \partial T \geq 0$ where $w = \sigma^2 T$ | Total variance non-decreasing |
| Monotonicity | $\partial C / \partial K \leq 0$ | Call cheaper at higher strike |

### QP Formulation

$$\min_{\sigma} \|\sigma - \sigma_{\text{mkt}}\|^2 \quad \text{s.t.} \quad A\sigma \geq 0,\; 0.001 \leq \sigma \leq 5$$

where $A$ encodes discrete butterfly convexity and total-variance calendar constraints.

### Dupire Local Volatility (Breeden-Litzenberger)

$$\sigma^2_{\text{local}}(K,T) = \frac{\partial C/\partial T}{\frac{1}{2} K^2 \cdot \partial^2 C/\partial K^2}$$

Requires $\partial^2 C / \partial K^2 > 0$ — guaranteed by the QP correction.

## Build
```powershell
# Prerequisites: vcpkg with eigen3, nlohmann-json, osqp
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake" -A x64
cmake --build . --config Release
.\Release\vol_arb.exe                          # runs on sample data
.\Release\vol_arb.exe data\spy_quotes.json     # runs on real SPY data
.\Release\test_butterfly.exe                   # unit tests
```

## Sample Output
```
╔══════════════════════════════════════════════╗
║   Vol-Arb: Arbitrage Detection & QP Repair  ║
╚══════════════════════════════════════════════╝

── Step 3: Detecting arbitrage violations
=== Arbitrage Violations (2) ===
     [BUTTERFLY]  Butterfly: d²C/dK² = -0.023 < 0 at K=100.000, T=0.250
      [CALENDAR]  Calendar: dC/dT < 0 at K=100.000, T=0.375

── Step 4: Running QP projection onto arbitrage-free cone
   Status    : solved
   Objective : 0.014700   (L2 distance from market to corrected surface)

── Step 6: Re-checking corrected surface for violations
✓ No arbitrage violations detected.

── Step 7: Computing dual certificate (KKT conditions)
KKT Certificate:
  Stationarity residual  : 0.000003  ✓
  Dual feasibility viol  : 0.000001  ✓
  Certificate valid      : YES
```

## References

1. Fengler, M. (2009). Arbitrage-free smoothing of the implied volatility surface. *Quantitative Finance*, 9(4), 417–428.
2. Gatheral, J. & Jacquier, A. (2014). Arbitrage-free SVI volatility surfaces. *Quantitative Finance*, 14(1), 59–71.
3. Stellato, B. et al. (2020). OSQP: An operator splitting solver for quadratic programs. *Mathematical Programming Computation*.
4. Dupire, B. (1994). Pricing with a smile. *Risk*, 7(1), 18–20.
```

---

