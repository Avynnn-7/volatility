# Volatility Surface Arbitrage Detection & Correction

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)]()
[![Version](https://img.shields.io/badge/version-2.0.0-blue)]()
[![License](https://img.shields.io/badge/license-MIT-green)]()
[![C++](https://img.shields.io/badge/C%2B%2B-17-blue)]()
[![React](https://img.shields.io/badge/React-18-61dafb)]()
[![Live Demo](https://img.shields.io/badge/demo-live-brightgreen)](https://avynnn-7.github.io/volatility/)

> A full-stack system for detecting and correcting static arbitrage in implied volatility surfaces, combining a high-performance C++17 computational engine with a modern React visualization dashboard.

🔗 **[Live Demo](https://avynnn-7.github.io/volatility/)**

---

## Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
- [Project Structure](#project-structure)
- [Core Engine (C++)](#core-engine-c)
- [Frontend Dashboard (React)](#frontend-dashboard-react)
- [Getting Started](#getting-started)
- [Mathematics](#mathematics)
- [References](#references)
- [License](#license)

---

## Overview

Implied volatility surfaces derived from market option prices often contain **static arbitrage violations** — inconsistencies that would allow risk-free profit. These violations arise from bid-ask noise, illiquidity, and asynchronous quotes. This project provides:

1. **Detection** — Systematically identify butterfly, calendar, and monotonicity arbitrage violations across strike-expiry grids.
2. **Correction** — Project the violated surface onto the nearest arbitrage-free surface using constrained quadratic programming (OSQP).
3. **Visualization** — Explore raw and corrected surfaces, violation heatmaps, and local volatility in an interactive web dashboard.

**No equivalent open-source C++ implementation exists.**

---

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                    Frontend (React + Vite)                    │
│  ┌──────────┐ ┌────────────┐ ┌───────────┐ ┌──────────────┐ │
│  │  Wizard   │ │ Dashboard  │ │ Analysis  │ │ 3D Surface   │ │
│  │  (Upload) │ │ (Overview) │ │ (Details) │ │ (Three.js)   │ │
│  └──────────┘ └────────────┘ └───────────┘ └──────────────┘ │
│                         │  REST API  │                       │
└─────────────────────────┼────────────┼───────────────────────┘
                          ▼            ▼
┌──────────────────────────────────────────────────────────────┐
│                  Core Engine (C++17)                          │
│  ┌────────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────┐  │
│  │ SVI Surface│ │Arbitrage │ │QP Solver │ │ Local Vol    │  │
│  │ Fitting    │ │Detector  │ │(OSQP)    │ │ (Dupire)     │  │
│  └────────────┘ └──────────┘ └──────────┘ └──────────────┘  │
│  ┌────────────┐ ┌──────────┐ ┌──────────┐                   │
│  │ Dual Cert  │ │Data      │ │ REST API │                   │
│  │ (KKT)     │ │Handler   │ │ Server   │                   │
│  └────────────┘ └──────────┘ └──────────┘                   │
└──────────────────────────────────────────────────────────────┘
```

---

## Project Structure

```
volatility/
├── vol_arb/                 # C++17 Core Engine
│   ├── src/                 # Source files
│   │   ├── vol_api.cpp      # Unified API facade
│   │   ├── arbitrage_detector.cpp
│   │   ├── qp_solver.cpp    # OSQP-based correction
│   │   ├── svi_surface.cpp  # SVI parameterization
│   │   ├── local_vol.cpp    # Dupire local volatility
│   │   ├── dual_certificate.cpp
│   │   ├── data_handler.cpp
│   │   └── ...
│   ├── include/             # Header files
│   ├── data/                # Sample market data
│   ├── tests/               # Integration & unit tests
│   ├── CMakeLists.txt
│   └── README.md            # Core engine documentation
│
├── vol_arb_ui/              # React Frontend Dashboard
│   ├── src/
│   │   ├── features/        # Page-level feature modules
│   │   ├── components/      # Reusable UI components
│   │   ├── store/           # Redux state management
│   │   ├── services/        # API client layer
│   │   └── ...
│   ├── public/
│   ├── vite.config.ts
│   └── README.md            # Frontend documentation
│
└── README.md                # ← You are here
```

---

## Core Engine (C++)

The computational backbone is a **production-ready C++17 library** that performs:

| Module | Description |
|--------|-------------|
| **ArbitrageDetector** | Butterfly, calendar, and monotonicity violation scanning |
| **QPSolver** | L² projection onto the arbitrage-free cone via OSQP |
| **SVISurface** | SVI (Stochastic Volatility Inspired) smile parameterization |
| **DualCertificate** | KKT optimality verification proving solution correctness |
| **LocalVol** | Dupire local volatility extraction from corrected surfaces |
| **DataHandler** | JSON/CSV ingestion with multi-source support |
| **APIServer** | HTTP REST API for frontend integration |

### Performance

| Operation | Time (1,000 quotes) |
|-----------|---------------------|
| Surface Construction | ~5 ms |
| Arbitrage Detection | ~8 ms |
| QP Correction | ~45 ms |
| Full Pipeline | ~60 ms |

### Dependencies

- [Eigen3](https://eigen.tuxfamily.org/) — Linear algebra
- [nlohmann/json](https://github.com/nlohmann/json) — JSON parsing
- [OSQP](https://osqp.org/) — Quadratic programming solver
- OpenMP (optional) — Parallelization

📖 See [`vol_arb/README.md`](vol_arb/README.md) for full build instructions, API usage, and sample output.

---

## Frontend Dashboard (React)

A modern, interactive web application for visualizing volatility surfaces and arbitrage analysis results.

### Features

- **Wizard Workflow** — Step-by-step guided upload and configuration
- **3D Surface Viewer** — Interactive Plotly-based volatility surface rendering
- **Arbitrage Heatmap** — Visual indicators of violation type and severity
- **Local Volatility View** — Dupire local vol computed from corrected surfaces
- **Dark Theme** — Professional dark-mode UI with dynamic animations

### Tech Stack

| Layer | Technology |
|-------|------------|
| Framework | React 18 + TypeScript |
| Build | Vite |
| State | Redux Toolkit |
| Charts | Plotly.js |
| Styling | Tailwind CSS |
| Animation | Framer Motion |
| Icons | Lucide React |

📖 See [`vol_arb_ui/README.md`](vol_arb_ui/README.md) for frontend setup and development instructions.

---

## Getting Started

### Prerequisites

- **C++ Engine**: Visual Studio 2019+, CMake 3.20+, vcpkg (with Eigen3, nlohmann-json, OSQP)
- **Frontend**: Node.js 18+

### Quick Start

```powershell
# 1. Build the C++ engine
cd vol_arb
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake" -A x64
cmake --build . --config Release

# 2. Run the engine (standalone CLI)
.\Release\vol_arb.exe data\sample_quotes.json

# 3. Or start the API server
.\Release\vol_arb.exe --server --port 8080

# 4. Start the frontend (in a separate terminal)
cd vol_arb_ui
npm install
npm run dev
# → Opens at http://localhost:5173
```

---

## Mathematics

### No-Arbitrage Conditions

| Condition | Formula | Interpretation |
|-----------|---------|----------------|
| **Butterfly** | ∂²C/∂K² ≥ 0 | Risk-neutral density must be non-negative |
| **Calendar** | ∂(σ²T)/∂T ≥ 0 | Total variance must be non-decreasing in maturity |
| **Monotonicity** | ∂C/∂K ≤ 0 | Call prices must decrease with strike |

### QP Correction Formulation

```
min  ‖σ − σ_mkt‖² + λ · R(σ)

s.t. A · σ ≥ 0           (no-arbitrage linear constraints)
     σ_min ≤ σ ≤ σ_max   (box constraints)
```

The solver finds the **nearest arbitrage-free surface** (in L² norm) to the observed market surface, ensuring all no-arbitrage conditions hold simultaneously.

### Dupire Local Volatility

```
σ²_local(K, T) = ∂C/∂T  /  (½ K² ∂²C/∂K²)
```

---

## References

1. Fengler, M. (2009). *Arbitrage-free smoothing of the implied volatility surface.* Quantitative Finance, 9(4), 417–428.
2. Gatheral, J. & Jacquier, A. (2014). *Arbitrage-free SVI volatility surfaces.* Quantitative Finance, 14(1), 59–71.
3. Stellato, B. et al. (2020). *OSQP: An operator splitting solver for quadratic programs.* Mathematical Programming Computation.
4. Dupire, B. (1994). *Pricing with a smile.* Risk, 7(1), 18–20.

---

## License

MIT License — see [LICENSE](vol_arb/LICENSE) for details.
