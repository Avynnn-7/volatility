# Volatility Arbitrage Detector (Engine)

A high-performance computational engine designed to analyze implied volatility surfaces and detect structural arbitrage opportunities across NSE and BSE options markets. This repository houses the core mathematical models that power the real-time volatility scanner.

**Live Application:** [volarbui.vercel.app](https://volarbui.vercel.app/live)

## Overview

The engine acts as the quantitative backend for identifying complex option mispricings. It takes market quotes, builds a continuous volatility surface using SVI (Stochastic Volatility Inspired) parameterization, and runs violation checks to spot actionable trades. The system is built for speed and mathematical precision.

## Core Capabilities

- **Surface Construction:** Interpolates and extrapolates sparse option chains into a robust, continuous implied volatility surface.
- **Arbitrage Detection:** Scans the assembled grid for core structural violations:
  - Calendar arbitrage (negative forward variance).
  - Butterfly arbitrage (negative risk-neutral density).
  - Volatility skew inversions and extreme IV dislocations.
- **Profit Modeling:** Uses actual market bid/ask spreads to simulate execution. It calculates maximum risk, maximum reward, lot-adjusted profitability, and dynamic breakevens.
- **After-Hours Support:** Includes a custom Black-Scholes solver capable of inferring implied volatilities directly from closing spot prices when the market is offline or option chain data is incomplete.

## Integration

While this was originally architected as a standalone C++ command-line tool, its core logic has been optimized and ported to run directly via Vercel Serverless functions, enabling the zero-latency, real-time web application. 

The standalone C++ implementation remains heavily utilized for off-chain backtesting, historical surface reconstruction, and integration with local QP solvers (osqp, eigen) to project mathematically invalid market data onto the nearest arbitrage-free cone.

## Usage (C++ CLI)

For researchers looking to run the engine locally for bulk historical analysis:

```bash
# Build the engine using CMake
mkdir build && cd build
cmake ..
cmake --build . --config Release

# Execute the scanner against a JSON market snapshot
./vol_arb config/market_data.json
```

## Architecture Map

- `/src`: Core arbitrage and pricing algorithms.
- `/include`: Header definitions for mathematical models and data structures.
- `/tests`: Unit tests for the theoretical pricing models.
- `/data`: Sample JSON structures for Upstox API payloads.

## License

This project is licensed under the MIT License.
