#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>

#include "vol_surface.hpp"
#include "arbitrage_detector.hpp"
#include "qp_solver.hpp"
#include "dual_certificate.hpp"
#include "local_vol.hpp"   // put this at the top with the other includes

// ── Load quotes from JSON file ────────────────────────────────────────────────
static std::pair<std::vector<Quote>, MarketData> loadQuotes(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("Cannot open file: " + path);
    
    nlohmann::json j;
    try {
        f >> j;
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error("JSON parse error: " + std::string(e.what()));
    }

    // Extract market data with defaults if not present
    MarketData marketData{
        j["spot"].get<double>(),
        j.value("riskFreeRate", 0.05),      // Default 5% if not in JSON
        j.value("dividendYield", 0.02),     // Default 2% if not in JSON
        j.value("valuationDate", "2024-01-01"),
        j.value("currency", "USD")
    };
    
    // Load quotes
    std::vector<Quote> quotes;
    for (auto& q : j["quotes"]) {
        quotes.push_back({
            q["strike"].get<double>(),
            q["expiry"].get<double>(),
            q["iv"].get<double>()
        });
    }
    
    return {quotes, marketData};
}

int main(int argc, char* argv[]) {
    std::string dataPath = (argc > 1) ? argv[1] : "data/sample_quotes.json";

    std::cout << "╔══════════════════════════════════════════════╗\n";
    std::cout << "║   Vol-Arb: Arbitrage Detection & QP Repair  ║\n";
    std::cout << "╚══════════════════════════════════════════════╝\n\n";

    // ── 1. Load market data ───────────────────────────────────────────────────
    std::cout << "── Step 1: Loading market quotes from " << dataPath << "\n";
    auto [quotes, marketData] = loadQuotes(dataPath);
    std::cout << "   Loaded " << quotes.size() << " quotes\n";
    std::cout << "   Spot:           " << marketData.spot << "\n";
    std::cout << "   Risk-free rate: " << (marketData.riskFreeRate * 100) << "%\n";
    std::cout << "   Div yield:      " << (marketData.dividendYield * 100) << "%\n";

    // ── 2. Build market surface ───────────────────────────────────────────────
    std::cout << "\n── Step 2: Building implied volatility surface\n";
    VolSurface marketSurface(quotes, marketData);
    marketSurface.print();

    // ── Build flat ivMarketVec once — used in Steps 4 and 7 ──────────────────
    // CHANGE 1: compute this here so it's in scope for both the objective
    // calculation (Step 4) and the dual certifier (Step 7).
    int nE = (int)marketSurface.expiries().size();
    int nK = (int)marketSurface.strikes().size();
    Eigen::VectorXd ivMarketVec(nE * nK);
    for (int i = 0; i < nE; ++i)
        for (int j = 0; j < nK; ++j)
            ivMarketVec(i * nK + j) = marketSurface.ivGrid()(i, j);

    // ── 3. Detect arbitrage on raw market surface ─────────────────────────────
    std::cout << "── Step 3: Detecting arbitrage violations\n";
    ArbitrageDetector detector(marketSurface);
    auto violations = detector.detect();
    ArbitrageDetector::report(violations);

    if (violations.empty()) {
        std::cout << "   Market surface is already arbitrage-free. QP projection skipped.\n";
        return 0;
    }

    // ── 4. Run QP projection ──────────────────────────────────────────────────
    std::cout << "── Step 4: Running QP projection onto arbitrage-free cone\n";
    QPSolver qpSolver(marketSurface);
    QPResult qpResult = qpSolver.solve();

    // CHANGE 2: compute the true L2 cost ||σ_corrected - σ_market||²
    // instead of using OSQP's internal obj_val (which reports (1/2)x'Px + q'x
    // and comes out negative due to the linear term dominating).
    double trueCost = (qpResult.ivFlat - ivMarketVec).squaredNorm();

    std::cout << "   Status    : " << qpResult.status << "\n";
    std::cout << "   Objective : " << std::fixed << std::setprecision(6) << trueCost << "\n";

    if (!qpResult.success) {
        std::cout << "   QP failed — surface may be too distorted for projection.\n";
        return 1;
    }

    // ── 5. Build corrected surface ────────────────────────────────────────────
    std::cout << "\n── Step 5: Corrected (arbitrage-free) surface\n";
    VolSurface correctedSurface = qpSolver.buildCorrectedSurface(qpResult);
    correctedSurface.print();

    // ── 6. Verify corrected surface is clean ──────────────────────────────────
    std::cout << "── Step 6: Re-checking corrected surface for violations\n";
    ArbitrageDetector correctedDetector(correctedSurface);
    auto correctedViols = correctedDetector.detect();
    ArbitrageDetector::report(correctedViols);
    
    // ── 6b. Dupire local volatility on corrected surface ─────────────────────────
    std::cout << "── Step 6b: Computing Dupire local volatility\n";
    LocalVolSurface localVol(correctedSurface);
    localVol.print();
    bool lvOk = localVol.allPositive();
    std::cout << "   Local vol all positive: " << (lvOk ? "YES ✓" : "NO (NaN at boundaries — expected)") << "\n\n";


    // ── 7. Dual certificate (KKT verification) ────────────────────────────────
    std::cout << "── Step 7: Computing dual certificate (KKT conditions)\n";
    {
        // CHANGE 3: use qpSolver.buildConstraints() directly (now public)
        // to get the real constraint matrix instead of the identity proxy.
        // This gives accurate dual variables and a meaningful certificate.
        Eigen::SparseMatrix<double> A;
        Eigen::VectorXd lb, ub;
        qpSolver.buildConstraints(A, lb, ub);

        DualCertifier certifier(ivMarketVec, qpResult, A, lb, ub);
        DualCertificate cert = certifier.certify();
        certifier.print(cert);
    }

    // ── 8. Summary ────────────────────────────────────────────────────────────
    std::cout << "╔══════════════════════════════════════════════╗\n";
    std::cout << "║                   SUMMARY                   ║\n";
    std::cout << "╠══════════════════════════════════════════════╣\n";
    std::cout << "║  Raw violations found  : " << std::setw(3) << violations.size()
              << "                     ║\n";
    std::cout << "║  Post-QP violations    : " << std::setw(3) << correctedViols.size()
              << "                     ║\n";
    // CHANGE 2 (continued): use trueCost here too instead of qpResult.objectiveValue
    std::cout << "║  QP L2 correction cost : "
              << std::fixed << std::setprecision(6) << trueCost
              << "          ║\n";
    std::cout << "╚══════════════════════════════════════════════╝\n";

    return 0;
}
