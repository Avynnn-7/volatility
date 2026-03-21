# vol_arb — Volatility Surface Arbitrage Detection & Correction

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)]()
[![Version](https://img.shields.io/badge/version-2.0.0-blue)]()
[![License](https://img.shields.io/badge/license-MIT-green)]()
[![C++](https://img.shields.io/badge/C%2B%2B-17-blue)]()

A production-ready C++17 library for detecting and correcting static arbitrage
in implied volatility surfaces using quadratic programming.

**No equivalent open-source C++ implementation exists.**

## Features

- **Static Arbitrage Detection**: Butterfly, calendar, monotonicity, and more
- **QP-Based Correction**: L² projection onto arbitrage-free cone using OSQP
- **Dual Certificate**: KKT verification proving solution optimality
- **SVI Parameterization**: Industry-standard volatility smile fitting
- **Local Volatility**: Dupire local vol from corrected surfaces
- **High-Performance**: SIMD optimization, OpenMP parallelization, LRU caching
- **Production-Ready**: Thread-safe, comprehensive validation, extensive testing

## Quick Start

```powershell
# Prerequisites: vcpkg with eigen3, nlohmann-json, osqp
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake" -A x64
cmake --build . --config Release

# Run
.\Release\vol_arb.exe data\sample_quotes.json
```

## Sample Output

```
╔══════════════════════════════════════════════╗
║   Vol-Arb: Arbitrage Detection & QP Repair  ║
╚══════════════════════════════════════════════╝

── Step 3: Detecting arbitrage violations
=== Arbitrage Violations (2) ===
     [BUTTERFLY]  d²C/dK² = -0.023 < 0 at K=100.000, T=0.250
      [CALENDAR]  dC/dT < 0 at K=100.000, T=0.375

── Step 4: Running QP projection onto arbitrage-free cone
   Status    : solved
   Objective : 0.014700

── Step 6: Re-checking corrected surface for violations
✓ No arbitrage violations detected.

── Step 7: Computing dual certificate (KKT conditions)
KKT Certificate:
  Stationarity residual  : 0.000003  ✓
  Dual feasibility viol  : 0.000001  ✓
  Certificate valid      : YES
```

## Mathematics

### No-Arbitrage Conditions

| Condition | Formula | Meaning |
|-----------|---------|---------|
| Butterfly | ∂²C/∂K² ≥ 0 | Non-negative risk-neutral density |
| Calendar | ∂(σ²T)/∂T ≥ 0 | Total variance non-decreasing |
| Monotonicity | ∂C/∂K ≤ 0 | Call price decreasing in strike |

### QP Formulation

```
min  ||σ - σ_mkt||² + λ·R(σ)
s.t. A·σ ≥ 0           (no-arbitrage constraints)
     σ_min ≤ σ ≤ σ_max (box constraints)
```

### Dupire Local Volatility

```
σ²_local(K,T) = (∂C/∂T) / (½ K² ∂²C/∂K²)
```

## Usage

```cpp
#include "vol_api.hpp"

// Load data
DataHandler handler({{.source = DataSource::JSON_FILE, .filePath = "quotes.json"}});
auto [quotes, marketData] = handler.loadData();

// Check arbitrage
ArbitrageCheckRequest request;
request.quotes = quotes;
request.marketData = marketData;
request.enableQPCorrection = true;

auto& api = VolatilityArbitrageAPI::getInstance();
auto response = api.checkArbitrage(request);

if (response.success) {
    std::cout << "Quality: " << response.data << std::endl;
}
```

## Documentation

- [Architecture Overview](docs/ARCHITECTURE.md)
- [User Guide](docs/USER_GUIDE.md)
- [API Reference](docs/API_REFERENCE.md)
- [FAQ](docs/FAQ.md)

## Performance

| Operation | 1,000 quotes | Time |
|-----------|--------------|------|
| Surface Construction | ✓ | 5ms |
| Arbitrage Detection | ✓ | 8ms |
| QP Correction | ✓ | 45ms |
| Total Pipeline | ✓ | 60ms |

## Dependencies

- [Eigen3](https://eigen.tuxfamily.org/) - Linear algebra
- [nlohmann/json](https://github.com/nlohmann/json) - JSON parsing
- [OSQP](https://osqp.org/) - Quadratic programming
- OpenMP (optional) - Parallelization

## References

1. Fengler, M. (2009). Arbitrage-free smoothing of the implied volatility surface.
   *Quantitative Finance*, 9(4), 417–428.
2. Gatheral, J. & Jacquier, A. (2014). Arbitrage-free SVI volatility surfaces.
   *Quantitative Finance*, 14(1), 59–71.
3. Stellato, B. et al. (2020). OSQP: An operator splitting solver for quadratic
   programs. *Mathematical Programming Computation*.
4. Dupire, B. (1994). Pricing with a smile. *Risk*, 7(1), 18–20.

## License

MIT License - see [LICENSE](LICENSE) for details.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.
