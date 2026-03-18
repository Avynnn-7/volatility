cmake_minimum_required(VERSION 3.20)
project(vol_arb VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies ──────────────────────────────────────────────────────────────
find_package(Eigen3   CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(osqp     CONFIG REQUIRED)

# ── Shared library sources ────────────────────────────────────────────────────
set(LIB_SOURCES
    src/vol_surface.cpp
    src/arbitrage_detector.cpp
    src/qp_solver.cpp
    src/dual_certificate.cpp
    src/local_vol.cpp
)

# ── Main executable ───────────────────────────────────────────────────────────
add_executable(vol_arb main.cpp ${LIB_SOURCES})
target_include_directories(vol_arb PRIVATE include)
target_link_libraries(vol_arb PRIVATE
    Eigen3::Eigen
    nlohmann_json::nlohmann_json
    osqp::osqp
)

# ── Test executable ───────────────────────────────────────────────────────────
add_executable(test_butterfly tests/test_butterfly.cpp ${LIB_SOURCES})
target_include_directories(test_butterfly PRIVATE include)
target_link_libraries(test_butterfly PRIVATE
    Eigen3::Eigen
    nlohmann_json::nlohmann_json
    osqp::osqp
)