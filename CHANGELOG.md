# Changelog

All notable changes to vol_arb are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.0.0] - 2024-XX-XX

### Added

- **SVI Surface**: Stochastic Volatility Inspired parameterization with
  Levenberg-Marquardt calibration and analytical Jacobian
- **Enhanced Arbitrage Detection**: 6 violation types (butterfly, calendar,
  monotonicity, vertical spread, extreme values, density integrity)
- **Adaptive Finite Differences**: Optimal step size computation for numerical
  derivatives
- **QP Solver Improvements**: Multiple objective functions (L2, Weighted L2,
  Huber), adaptive regularization, calendar constraint refinement
- **Dual Certificate**: KKT condition verification for optimality proof
- **High-Level API**: VolatilityArbitrageAPI singleton with thread-safe operations
- **REST API**: HTTP endpoint handlers for web integration
- **Data Handler**: Multi-source loading (JSON, CSV), validation, cleaning
- **Configuration Manager**: JSON-based settings with persistence
- **Logger**: Thread-safe logging with file and console output
- **Validation Framework**: Centralized input validation with custom exceptions
- **Memory Pool**: Fast allocation with bulk deallocation
- **SIMD Math**: AVX/AVX2 vectorized Black-Scholes pricing
- **OpenMP Parallelization**: Parallel arbitrage detection
- **LRU Cache**: Configurable caching for surface queries
- **Comprehensive Documentation**: Doxygen comments, architecture docs, user guide

### Changed

- **Black-Scholes**: Now includes proper rates (r) and dividend yield (q)
- **VolSurface**: Thread-safe with shared_mutex for concurrent reads
- **QPSolver**: Uses OSQP 1.x API with optimized settings
- **Project Structure**: Separated headers, sources, tests, docs

### Fixed

- Butterfly constraint formula (use call prices, not IVs)
- Calendar constraint (use total variance σ²T, not just IV)
- Black-Scholes d1/d2 formulas with dividend yield
- OSQP API compatibility (v0.6 → v1.x migration)
- Memory leaks in OSQP data structures
- Thread safety issues in singleton patterns

### Removed

- Zero-rate assumption in Black-Scholes
- Hard-coded grid sizes
- Global state in arbitrage detection

## [1.0.0] - 2023-XX-XX

### Added

- Initial implementation
- Basic volatility surface construction
- Butterfly and calendar arbitrage detection
- QP-based surface correction using OSQP
- Simple command-line interface

---

## Version History Summary

| Version | Date | Description |
|---------|------|-------------|
| 2.0.0 | 2024 | Production-ready release with full feature set |
| 1.0.0 | 2023 | Initial proof-of-concept |
