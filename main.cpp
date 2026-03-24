#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <string>
#include <sstream>
#include <cstdlib>  // std::getenv
#include <nlohmann/json.hpp>

#include "vol_surface.hpp"
#include "arbitrage_detector.hpp"
#include "qp_solver.hpp"
#include "dual_certificate.hpp"
#include "local_vol.hpp"
#include "profit_advisor.hpp"
#include "upstox_client.hpp"

// ── Command-line flags ────────────────────────────────────────────────────────
struct CLIFlags {
    std::string dataPath = "data/sample_quotes.json";
    bool useStdin    = false;    // --stdin  : read JSON from stdin
    bool jsonOutput  = false;    // --json-output : output structured JSON

    // Live Data configuration
    std::string liveSymbol = "";   // --live SYMBOL
    std::string exchange = "NSE_EQ"; // --exchange (NSE_EQ, BSE_EQ, NSE_INDEX)
    // NOTE: accessToken is intentionally NOT a CLI arg.
    // It is read from the UPSTOX_ACCESS_TOKEN environment variable (see loadLiveQuotes).
    // Passing secrets as CLI args exposes them in process listings (ps/wmic). Do NOT add it back.
};

static CLIFlags parseArgs(int argc, char* argv[]) {
    CLIFlags flags;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--stdin")                          flags.useStdin  = true;
        else if (arg == "--json-output" || arg == "--json") flags.jsonOutput = true;
        else if (arg == "--live" && i + 1 < argc)     flags.liveSymbol = argv[++i];
        else if (arg == "--exchange" && i + 1 < argc) flags.exchange   = argv[++i];
        else if (arg == "--token") {
            // --token is silently ignored; token comes from env for security.
            // If the next arg doesn't start with '-', skip it (backward compat).
            if (i + 1 < argc && argv[i+1][0] != '-') ++i;
            std::cerr << "[Security] --token flag is deprecated. "
                         "Set UPSTOX_ACCESS_TOKEN environment variable instead.\n";
        }
        else if (arg == "--help" || arg == "-h") {
            std::cout
                << "Usage: vol_arb [OPTIONS] [data_file]\n\n"
                << "Options:\n"
                << "  --stdin          Read JSON from stdin instead of file\n"
                << "  --json-output    Output results as structured JSON\n"
                << "  --live SYMBOL    Fetch live data from Upstox for SYMBOL (e.g., NIFTY)\n"
                << "  --exchange EXCH  Exchange (NSE_EQ, BSE_EQ, NSE_INDEX). Default: NSE_EQ\n"
                << "  --help, -h       Show this help message\n\n"
                << "Authentication:\n"
                << "  Set the UPSTOX_ACCESS_TOKEN environment variable.\n"
                << "  Do NOT pass the token as a CLI arg -- it will appear in process listings.\n\n"
                << "Examples:\n"
                << "  vol_arb data/sample_quotes.json\n"
                << "  UPSTOX_ACCESS_TOKEN=\"Bearer ey...\" vol_arb --live NIFTY --exchange NSE_INDEX\n";
            exit(0);
        }
        else if (arg[0] != '-') flags.dataPath = arg;
    }
    return flags;
}

// ── Load quotes from JSON string or file ──────────────────────────────────────
static std::pair<std::vector<Quote>, MarketData> loadQuotesFromJson(const nlohmann::json& j) {
    MarketData marketData{
        j["spot"].get<double>(),
        j.value("riskFreeRate", 0.05),
        j.value("dividendYield", 0.02),
        j.value("valuationDate", "2024-01-01"),
        j.value("currency", "INR")
    };

    std::vector<Quote> quotes;
    for (auto& q : j["quotes"]) {
        quotes.push_back({
            q["strike"].get<double>(),
            q["expiry"].get<double>(),
            q["iv"].get<double>(),
            q.value("bid", 0.0),
            q.value("ask", 0.0),
            q.value("volume", 0.0)
        });
    }
    return {quotes, marketData};
}

static std::pair<std::vector<Quote>, MarketData> loadQuotes(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("Cannot open file: " + path);
    nlohmann::json j;
    try { f >> j; } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error("JSON parse error: " + std::string(e.what()));
    }
    return loadQuotesFromJson(j);
}

static std::pair<std::vector<Quote>, MarketData> loadQuotesFromStdin() {
    std::ostringstream ss;
    ss << std::cin.rdbuf();
    std::string input = ss.str();
    if (input.empty())
        throw std::runtime_error("No input received on stdin");
    nlohmann::json j;
    try { j = nlohmann::json::parse(input); } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error("JSON parse error from stdin: " + std::string(e.what()));
    }
    return loadQuotesFromJson(j);
}

static std::pair<std::vector<Quote>, MarketData> loadLiveQuotes(const CLIFlags& flags) {
    // SECURITY: Read token from environment variable only.
    // The Node.js parent injects it via child process env (spawn({ env: { UPSTOX_ACCESS_TOKEN: ... } })).
    // This ensures the token never appears in ps / wmic / task manager outputs.
    const char* envToken = std::getenv("UPSTOX_ACCESS_TOKEN");
    std::string accessToken = (envToken && *envToken) ? std::string(envToken) : "";

    if (accessToken.empty() && flags.liveSymbol != "MOCK") {
        throw std::runtime_error(
            "UPSTOX_ACCESS_TOKEN environment variable is not set. "
            "Set it in your .env file and restart the server."
        );
    }

    upstox::Config config;
    config.accessToken = accessToken;
    upstox::Client upstoxClient(config);

    std::string instrumentKey = upstoxClient.getInstrumentKey(flags.liveSymbol, flags.exchange);
    return upstoxClient.fetchOptionChain(instrumentKey);
}

// ── JSON Output Builder ──────────────────────────────────────────────────────
static nlohmann::json buildJsonOutput(
    const std::vector<Quote>& quotes,
    const MarketData& marketData,
    const VolSurface& marketSurface,
    const std::vector<ArbViolation>& violations,
    const QPResult& qpResult,
    bool qpSuccess,
    double trueCost,
    const VolSurface* correctedSurface,
    const std::vector<ArbViolation>& correctedViols,
    const std::vector<TradeRecommendation>& trades)
{
    nlohmann::json out;

    // Market info
    out["market"] = {
        {"spot", marketData.spot},
        {"riskFreeRate", marketData.riskFreeRate},
        {"dividendYield", marketData.dividendYield},
        {"currency", marketData.currency},
        {"quotesCount", quotes.size()}
    };

    // Surface grid
    out["surface"]["strikes"] = marketSurface.strikes();
    out["surface"]["expiries"] = marketSurface.expiries();
    {
        const auto& grid = marketSurface.ivGrid();
        nlohmann::json gridArr = nlohmann::json::array();
        for (int i = 0; i < grid.rows(); ++i) {
            nlohmann::json row = nlohmann::json::array();
            for (int j = 0; j < grid.cols(); ++j)
                row.push_back(grid(i, j));
            gridArr.push_back(row);
        }
        out["surface"]["ivGrid"] = gridArr;
    }

    // Violations
    out["violations"]["count"] = violations.size();
    out["violations"]["items"] = nlohmann::json::array();
    for (const auto& v : violations) {
        out["violations"]["items"].push_back({
            {"type",        v.type == ArbType::ButterflyViolation      ? "BUTTERFLY" :
                            v.type == ArbType::CalendarViolation       ? "CALENDAR" :
                            v.type == ArbType::MonotonicityViolation   ? "MONOTONICITY" :
                            v.type == ArbType::VerticalSpreadViolation ? "VERTICAL_SPREAD" :
                            v.type == ArbType::ExtremeValueViolation   ? "EXTREME_VALUE" :
                            v.type == ArbType::DensityIntegrityViolation ? "DENSITY" :
                                                                         "OTHER"},
            {"strike",      v.strike},
            {"expiry",      v.expiry},
            {"magnitude",   v.magnitude},
            {"severity",    v.severityScore()},
            {"critical",    v.isCritical()},
            {"description", v.description}
        });
    }

    // QP correction results
    out["correction"]["success"]        = qpSuccess;
    out["correction"]["l2Cost"]         = trueCost;
    out["correction"]["postViolations"] = correctedViols.size();
    out["correction"]["status"]         = qpResult.status;

    if (correctedSurface) {
        const auto& cGrid = correctedSurface->ivGrid();
        nlohmann::json cGridArr = nlohmann::json::array();
        for (int i = 0; i < cGrid.rows(); ++i) {
            nlohmann::json row = nlohmann::json::array();
            for (int j = 0; j < cGrid.cols(); ++j)
                row.push_back(cGrid(i, j));
            cGridArr.push_back(row);
        }
        out["correction"]["correctedIvGrid"] = cGridArr;
    }

    // Trade recommendations
    out["trades"] = ProfitAdvisor::toJson(trades);

    return out;
}

// ═════════════════════════════════════════════════════════════════════════════
// MAIN
// ═════════════════════════════════════════════════════════════════════════════

int main(int argc, char* argv[]) {
    CLIFlags flags = parseArgs(argc, argv);

    // ── 1. Load market data ───────────────────────────────────────────────────
    std::vector<Quote> quotes;
    MarketData marketData;

    try {
        if (!flags.liveSymbol.empty()) {
            if (!flags.jsonOutput) {
                std::cout << "╔══════════════════════════════════════════════╗\n";
                std::cout << "║   Vol-Arb: Arbitrage Detection & QP Repair  ║\n";
                std::cout << "╚══════════════════════════════════════════════╝\n\n";
                std::cout << "── Step 1: Fetching live option chain for " << flags.liveSymbol 
                          << " (" << flags.exchange << ") from Upstox API...\n";
            }
            std::tie(quotes, marketData) = loadLiveQuotes(flags);
        } else if (flags.useStdin) {
            if (!flags.jsonOutput) std::cerr << "── Reading JSON from stdin...\n";
            std::tie(quotes, marketData) = loadQuotesFromStdin();
        } else {
            if (!flags.jsonOutput) {
                std::cout << "╔══════════════════════════════════════════════╗\n";
                std::cout << "║   Vol-Arb: Arbitrage Detection & QP Repair  ║\n";
                std::cout << "╚══════════════════════════════════════════════╝\n\n";
                std::cout << "── Step 1: Loading market quotes from " << flags.dataPath << "\n";
            }
            std::tie(quotes, marketData) = loadQuotes(flags.dataPath);
        }
    } catch (const std::exception& e) {
        if (flags.jsonOutput) {
            nlohmann::json err;
            err["error"] = e.what();
            err["success"] = false;
            std::cout << err.dump(2) << std::endl;
        } else {
            std::cerr << "ERROR: " << e.what() << "\n";
        }
        return 1;
    }

    if (!flags.jsonOutput) {
        std::cout << "   Loaded " << quotes.size() << " quotes\n";
        std::cout << "   Spot:           " << marketData.spot << "\n";
        std::cout << "   Risk-free rate: " << (marketData.riskFreeRate * 100) << "%\n";
        std::cout << "   Div yield:      " << (marketData.dividendYield * 100) << "%\n";
    }

    // ── 2. Build market surface ───────────────────────────────────────────────
    if (!flags.jsonOutput) std::cout << "\n── Step 2: Building implied volatility surface\n";
    VolSurface marketSurface(quotes, marketData);
    if (!flags.jsonOutput) marketSurface.print();

    // Build flat ivMarketVec
    int nE = (int)marketSurface.expiries().size();
    int nK = (int)marketSurface.strikes().size();
    Eigen::VectorXd ivMarketVec(nE * nK);
    for (int i = 0; i < nE; ++i)
        for (int j = 0; j < nK; ++j)
            ivMarketVec(i * nK + j) = marketSurface.ivGrid()(i, j);

    // ── 3. Detect arbitrage on raw market surface ─────────────────────────────
    if (!flags.jsonOutput) std::cout << "── Step 3: Detecting arbitrage violations\n";
    ArbitrageDetector detector(marketSurface);
    auto violations = detector.detect();
    if (!flags.jsonOutput) ArbitrageDetector::report(violations);

    // Variables for QP / corrected surface
    QPResult qpResult;
    bool qpSuccess = false;
    double trueCost = 0.0;
    // Use pointer to heap-allocated VolSurface (avoid copy of non-copyable type)
    VolSurface* correctedSurfacePtr = nullptr;
    std::vector<ArbViolation> correctedViols;

    if (violations.empty()) {
        if (!flags.jsonOutput)
            std::cout << "   Market surface is already arbitrage-free.\n";
    } else {
        // ── 4. Run QP projection ──────────────────────────────────────────────
        if (!flags.jsonOutput) std::cout << "── Step 4: Running QP projection onto arbitrage-free cone\n";
        QPSolver qpSolver(marketSurface);
        qpResult = qpSolver.solve();
        trueCost = (qpResult.ivFlat - ivMarketVec).squaredNorm();
        qpSuccess = qpResult.success;

        if (!flags.jsonOutput) {
            std::cout << "   Status    : " << qpResult.status << "\n";
            std::cout << "   Objective : " << std::fixed << std::setprecision(6) << trueCost << "\n";
        }

        if (qpResult.success) {
            // ── 5. Build corrected surface ────────────────────────────────────
            if (!flags.jsonOutput) std::cout << "\n── Step 5: Corrected (arbitrage-free) surface\n";
            // Build corrected surface: extract quotes from QP result, reconstruct
            auto correctedSurface = qpSolver.buildCorrectedSurface(qpResult);
            correctedSurfacePtr = &correctedSurface;
            if (!flags.jsonOutput) correctedSurface.print();

            // ── 6. Verify corrected surface ───────────────────────────────────
            if (!flags.jsonOutput) std::cout << "── Step 6: Re-checking corrected surface for violations\n";
            ArbitrageDetector correctedDetector(correctedSurface);
            correctedViols = correctedDetector.detect();
            if (!flags.jsonOutput) ArbitrageDetector::report(correctedViols);

            // ── 6b. Dupire local volatility ───────────────────────────────────
            if (!flags.jsonOutput) {
                std::cout << "── Step 6b: Computing Dupire local volatility\n";
                LocalVolSurface localVol(correctedSurface);
                localVol.print();
                bool lvOk = localVol.allPositive();
                std::cout << "   Local vol all positive: "
                          << (lvOk ? "YES ✓" : "NO (NaN at boundaries — expected)") << "\n\n";
            }

            // ── 7. Dual certificate ───────────────────────────────────────────
            if (!flags.jsonOutput) {
                std::cout << "── Step 7: Computing dual certificate (KKT conditions)\n";
                Eigen::SparseMatrix<double> A;
                Eigen::VectorXd lb, ub;
                qpSolver.buildConstraints(A, lb, ub);
                DualCertifier certifier(ivMarketVec, qpResult, A, lb, ub);
                DualCertificate cert = certifier.certify();
                certifier.print(cert);
            }

            // Now generate JSON with the corrected surface if needed
            if (flags.jsonOutput) {
                // Build JSON output inside this scope where correctedSurface is still alive
                ProfitAdvisor advisor(marketSurface, violations);
                auto trades = advisor.generateRecommendations();
                nlohmann::json output = buildJsonOutput(
                    quotes, marketData, marketSurface,
                    violations, qpResult, qpSuccess, trueCost,
                    &correctedSurface, correctedViols, trades);
                output["success"] = true;
                std::cout << output.dump(2) << std::endl;
                return 0;
            }
        } else {
            if (!flags.jsonOutput)
                std::cout << "   QP failed — surface may be too distorted for projection.\n";
        }
    }

    // ── 8. Profit Advisor — Trade Recommendations ─────────────────────────────
    // (This path is reached when: no violations, or QP failed, or non-JSON mode)
    if (!flags.jsonOutput) std::cout << "── Step 8: Generating trade recommendations\n";
    ProfitAdvisor advisor(marketSurface, violations);
    auto trades = advisor.generateRecommendations();

    if (flags.jsonOutput) {
        // JSON output for cases without a corrected surface (no violations or QP failed)
        nlohmann::json output = buildJsonOutput(
            quotes, marketData, marketSurface,
            violations, qpResult, qpSuccess, trueCost,
            nullptr, correctedViols, trades);
        output["success"] = true;
        std::cout << output.dump(2) << std::endl;
    } else {
        // Print human-readable trade recommendations
        ProfitAdvisor::printReport(trades);

        // ── Summary ─────────────────────────────────────────────────────────
        std::cout << "╔══════════════════════════════════════════════╗\n";
        std::cout << "║                   SUMMARY                   ║\n";
        std::cout << "╠══════════════════════════════════════════════╣\n";
        std::cout << "║  Raw violations found  : " << std::setw(3) << violations.size()
                  << "                     ║\n";
        std::cout << "║  Post-QP violations    : " << std::setw(3) << correctedViols.size()
                  << "                     ║\n";
        std::cout << "║  QP L2 correction cost : "
                  << std::fixed << std::setprecision(6) << trueCost
                  << "          ║\n";
        std::cout << "║  Trade recommendations : " << std::setw(3) << trades.size()
                  << "                     ║\n";
        std::cout << "╚══════════════════════════════════════════════╝\n";
    }

    return 0;
}

