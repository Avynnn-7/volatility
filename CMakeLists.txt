cmake_minimum_required(VERSION 3.20)
project(vol_arb VERSION 2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies ──────────────────────────────────────────────────────────────
find_package(Eigen3   CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(osqp     CONFIG REQUIRED)

# ── Compiler options ───────────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ── Library sources ────────────────────────────────────────────────────────────
set(LIB_SOURCES
    src/vol_surface.cpp
    src/arbitrage_detector.cpp
    src/qp_solver.cpp
    src/dual_certificate.cpp
    src/local_vol.cpp
    src/svi_surface.cpp
    src/data_handler.cpp
    src/logger.cpp
    src/config_manager.cpp
    src/test_framework.cpp
    src/vol_api.cpp
)

# ── Include directories ─────────────────────────────────────────────────────────
include_directories(include)

# ── Main executable ───────────────────────────────────────────────────────────
add_executable(vol_arb main.cpp ${LIB_SOURCES})
target_include_directories(vol_arb PRIVATE include)
target_link_libraries(vol_arb PRIVATE
    Eigen3::Eigen
    nlohmann_json::nlohmann_json
    osqp::osqp
)

# ── Test executable ───────────────────────────────────────────────────────────
add_executable(test_comprehensive tests/comprehensive_tests.cpp ${LIB_SOURCES})
target_include_directories(test_comprehensive PRIVATE include)
target_link_libraries(test_comprehensive PRIVATE
    Eigen3::Eigen
    nlohmann_json::nlohmann_json
    osqp::osqp
)

# ── API server executable (optional) ───────────────────────────────────────────
add_executable(vol_api_server src/api_server_main.cpp ${LIB_SOURCES})
target_include_directories(vol_api_server PRIVATE include)
target_link_libraries(vol_api_server PRIVATE
    Eigen3::Eigen
    nlohmann_json::nlohmann_json
    osqp::osqp
)

# ── Performance benchmark executable ────────────────────────────────────────
add_executable(vol_benchmark src/benchmark_main.cpp ${LIB_SOURCES})
target_include_directories(vol_benchmark PRIVATE include)
target_link_libraries(vol_benchmark PRIVATE
    Eigen3::Eigen
    nlohmann_json::nlohmann_json
    osqp::osqp
)

# ── Installation ───────────────────────────────────────────────────────────────
install(TARGETS vol_arb vol_api_server
    RUNTIME DESTINATION bin
)

install(FILES include/vol_surface.hpp
              include/arbitrage_detector.hpp
              include/qp_solver.hpp
              include/dual_certificate.hpp
              include/local_vol.hpp
              include/svi_surface.hpp
              include/data_handler.hpp
              include/logger.hpp
              include/config_manager.hpp
              include/test_framework.hpp
              include/vol_api.hpp
    DESTINATION include/vol_arb
)

# ── Testing ───────────────────────────────────────────────────────────────────
enable_testing()
add_test(NAME ComprehensiveTests COMMAND test_comprehensive)

# ── Documentation (optional) ───────────────────────────────────────────────────
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    set(DOXYGEN_INPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set(DOXYGEN_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/docs)
    
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in
                   ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
    
    add_custom_target(docs
        ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
endif()

# ── Package configuration ─────────────────────────────────────────────────────
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/VolArbConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VolArbConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/VolArbConfig.cmake"
    INSTALL_DESTINATION lib/cmake/VolArb
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/VolArbConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/VolArbConfigVersion.cmake"
    DESTINATION lib/cmake/VolArb
)