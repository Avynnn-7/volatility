# Contributing to vol_arb

Thank you for considering contributing to vol_arb! This document provides
guidelines for contributing to the project.

## Code of Conduct

- Be respectful and inclusive
- Focus on constructive feedback
- Help others learn and grow

## Getting Started

1. Fork the repository
2. Clone your fork
3. Create a feature branch: `git checkout -b feature/your-feature`
4. Make your changes
5. Run tests: `cmake --build build --target vol_tests`
6. Commit: `git commit -m "Add your feature"`
7. Push: `git push origin feature/your-feature`
8. Open a Pull Request

## Development Setup

```bash
# Clone
git clone https://github.com/your-username/vol_arb.git
cd vol_arb

# Install dependencies (vcpkg)
vcpkg install eigen3 nlohmann-json osqp

# Build
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE="path/to/vcpkg.cmake"
cmake --build .

# Test
ctest --output-on-failure
```

## Coding Standards

### C++ Style

- **C++17** standard
- **4-space indentation** (no tabs)
- **camelCase** for functions: `impliedVol()`
- **PascalCase** for classes: `VolSurface`
- **snake_case** for variables: `strike_price`
- **UPPER_CASE** for constants: `MAX_ITERATIONS`

### Documentation

- All public APIs must have Doxygen comments
- Include `@brief`, `@param`, `@return`, `@throws`
- Add `@code` examples for complex functions

### Example

```cpp
/**
 * @brief Calculate implied volatility at given point
 *
 * @param strike Strike price (must be positive)
 * @param expiry Time to expiry in years (must be positive)
 * @return Implied volatility as decimal
 * @throws std::invalid_argument if inputs are invalid
 *
 * @code
 * double iv = surface.impliedVol(100.0, 0.25);
 * @endcode
 */
double impliedVol(double strike, double expiry) const;
```

## Testing

### Running Tests

```bash
cd build
ctest --output-on-failure

# Specific test
./Release/vol_tests.exe
```

### Writing Tests

```cpp
TestSuite suite("MyTests");

suite.addTest("test_name", []() {
    // Arrange
    auto quotes = MockDataGenerator::generateQuotes(100);
    
    // Act
    VolSurface surface(quotes, marketData);
    double iv = surface.impliedVol(100.0, 0.25);
    
    // Assert
    ASSERT_TRUE(iv > 0);
    ASSERT_NEAR(iv, 0.20, 0.01);
});
```

## Pull Request Process

1. **Title**: Clear, concise description
2. **Description**: Explain what and why
3. **Tests**: Include tests for new functionality
4. **Documentation**: Update docs for API changes
5. **Review**: Address reviewer feedback

### PR Checklist

- [ ] Code compiles without warnings
- [ ] All tests pass
- [ ] New tests added for new functionality
- [ ] Documentation updated
- [ ] No breaking API changes (or documented)

## Issue Reporting

### Bug Reports

Include:
- vol_arb version
- Operating system
- Steps to reproduce
- Expected vs actual behavior
- Minimal code example

### Feature Requests

Include:
- Use case description
- Proposed API (if applicable)
- Alternative approaches considered

## Architecture Decisions

Major changes should be discussed in an issue first:
- New public APIs
- Dependency additions
- Performance-critical changes
- Breaking changes

## License

By contributing, you agree that your contributions will be licensed
under the same MIT License as the project.
