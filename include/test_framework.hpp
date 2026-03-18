#pragma once
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <iostream>
#include <iomanip>

// Test result structure
struct TestResult {
    std::string testName;
    bool passed;
    double executionTime;
    std::string errorMessage;
    
    TestResult(const std::string& name, bool pass, double time, const std::string& error = "")
        : testName(name), passed(pass), executionTime(time), errorMessage(error) {}
};

// Test suite
class TestSuite {
public:
    TestSuite(const std::string& name);
    
    // Add test functions
    void addTest(const std::string& testName, std::function<void()> testFunc);
    void addTest(const std::string& testName, std::function<void()> testFunc, double timeoutSeconds);
    
    // Run all tests
    std::vector<TestResult> runAllTests();
    
    // Run specific test
    TestResult runTest(const std::string& testName);
    
    // Print results
    void printResults(const std::vector<TestResult>& results) const;
    
    // Get suite statistics
    int getTotalTests() const { return static_cast<int>(testFunctions_.size()); }
    int getPassedTests(const std::vector<TestResult>& results) const;
    int getFailedTests(const std::vector<TestResult>& results) const;
    double getTotalTime(const std::vector<TestResult>& results) const;

private:
    std::string suiteName_;
    struct TestInfo {
        std::string name;
        std::function<void()> function;
        double timeoutSeconds;
    };
    std::vector<TestInfo> testFunctions_;
    
    void runTestWithTimeout(const TestInfo& testInfo, TestResult& result);
};

// Assertion macros
#define ASSERT_TRUE(condition) \
    do { \
        if (!(condition)) { \
            throw std::runtime_error("Assertion failed: " #condition); \
        } \
    } while(0)

#define ASSERT_FALSE(condition) ASSERT_TRUE(!(condition))

#define ASSERT_EQ(expected, actual) \
    do { \
        if ((expected) != (actual)) { \
            throw std::runtime_error("Assertion failed: expected " + std::to_string(expected) + \
                                   " but got " + std::to_string(actual)); \
        } \
    } while(0)

#define ASSERT_NE(expected, actual) \
    do { \
        if ((expected) == (actual)) { \
            throw std::runtime_error("Assertion failed: expected not equal to " + \
                                   std::to_string(expected) + " but got equal"); \
        } \
    } while(0)

#define ASSERT_NEAR(expected, actual, tolerance) \
    do { \
        double diff = std::abs((expected) - (actual)); \
        if (diff > (tolerance)) { \
            throw std::runtime_error("Assertion failed: expected " + std::to_string(expected) + \
                                   " but got " + std::to_string(actual) + \
                                   " (diff=" + std::to_string(diff) + \
                                   " > tolerance=" + std::to_string(tolerance) + ")"); \
        } \
    } while(0)

#define ASSERT_THROWS(expression) \
    do { \
        bool threw = false; \
        try { \
            expression; \
        } catch (...) { \
            threw = true; \
        } \
        if (!threw) { \
            throw std::runtime_error("Assertion failed: expected exception but none was thrown"); \
        } \
    } while(0)

// Performance testing utilities
class PerformanceTester {
public:
    static double measureExecutionTime(std::function<void()> func);
    static void benchmarkFunction(const std::string& name, std::function<void()> func, int iterations = 1000);
    static void memoryUsageTest(const std::string& name, std::function<void()> func);
};

// Mock data generators
class MockDataGenerator {
public:
    static std::vector<Quote> generateQuotes(int numQuotes, double spot = 100.0);
    static std::vector<Quote> generateArbitrageFreeQuotes(int numQuotes, double spot = 100.0);
    static std::vector<Quote> generateButterflyArbitrageQuotes(int numQuotes, double spot = 100.0);
    static std::vector<Quote> generateCalendarArbitrageQuotes(int numQuotes, double spot = 100.0);
    static MarketData generateMarketData(double spot = 100.0);
};

// Test runner for all suites
class TestRunner {
public:
    static TestRunner& getInstance();
    
    void addSuite(std::unique_ptr<TestSuite> suite);
    int runAllSuites();
    void runSuite(const std::string& suiteName);
    
private:
    TestRunner() = default;
    std::vector<std::unique_ptr<TestSuite>> testSuites_;
};
