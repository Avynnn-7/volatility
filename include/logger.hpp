/**
 * @file logger.hpp
 * @brief Thread-safe logging system
 * @author vol_arb Team
 * @version 2.0
 * @date 2024
 *
 * Provides a singleton logging system with:
 * - Multiple log levels (DEBUG, INFO, WARNING, ERROR, CRITICAL)
 * - File and console output
 * - Thread-safe operation
 * - Timestamp formatting
 *
 * ## Example Usage
 * @code
 * auto& log = Logger::getInstance();
 * log.setLogFile("vol_arb.log");
 * log.setLogLevel(LogLevel::DEBUG);
 *
 * log.info("Processing started");
 * log.debug("Loaded 1000 quotes");
 * log.warning("Missing data for K=105");
 * log.error("QP solver failed to converge");
 * @endcode
 */

#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <memory>
#include <chrono>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <cstdio>

/**
 * @brief Log severity levels
 */
enum class LogLevel {
    DEBUG = 0,     ///< Detailed debugging information
    INFO = 1,      ///< General information
    WARNING = 2,   ///< Warning conditions
    ERROR = 3,     ///< Error conditions
    CRITICAL = 4   ///< Critical failures
};

/**
 * @brief Thread-safe singleton logger
 *
 * Provides centralized logging with configurable output destinations
 * and severity filtering.
 */
class Logger {
public:
    /**
     * @brief Get singleton instance
     * @return Reference to logger instance
     */
    static Logger& getInstance();
    
    /**
     * @brief Set log output file
     * @param filename Path to log file
     *
     * Opens file for appending. Previous file is closed.
     */
    void setLogFile(const std::string& filename);
    
    /**
     * @brief Set minimum log level
     * @param level Messages below this level are ignored
     */
    void setLogLevel(LogLevel level);
    
    /**
     * @brief Enable/disable console output
     * @param enable True to enable stdout output
     */
    void enableConsoleOutput(bool enable);
    
    /**
     * @brief Log message with format string
     * @tparam Args Format argument types
     * @param level Log level
     * @param format Format string
     * @param args Format arguments
     */
    template<typename... Args>
    void log(LogLevel level, const std::string& format, Args... args) {
        if (level < currentLevel_) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string message;
        if constexpr (sizeof...(args) == 0) {
            // No format args - use format string directly as message
            message = format;
        } else {
            // Format message with printf-style args
            int size = std::snprintf(nullptr, 0, format.c_str(), args...) + 1;
            std::unique_ptr<char[]> buf(new char[size]);
            std::snprintf(buf.get(), size, format.c_str(), args...);
            message = std::string(buf.get(), buf.get() + size - 1);
        }
        
        // Add timestamp and level
        std::string logEntry = "[" + getCurrentTimestamp() + "] " + 
                              levelToString(level) + ": " + message;
        
        // Output to console
        if (consoleOutput_) {
            std::cout << logEntry << std::endl;
        }
        
        // File I/O with verification
        if (logFile_.is_open() && logFile_.good()) {
            logFile_ << logEntry << std::endl;
            logFile_.flush();
            
            if (logFile_.fail()) {
                std::cerr << "ERROR: Failed to write to log file. Logging to console only." << std::endl;
                logFile_.close();
            }
        }
    }
    
    /**
     * @brief Log debug message
     * @param message Message text
     */
    void debug(const std::string& message);
    
    /**
     * @brief Log info message
     * @param message Message text
     */
    void info(const std::string& message);
    
    /**
     * @brief Log warning message
     * @param message Message text
     */
    void warning(const std::string& message);
    
    /**
     * @brief Log error message
     * @param message Message text
     */
    void error(const std::string& message);
    
    /**
     * @brief Log critical message
     * @param message Message text
     */
    void critical(const std::string& message);
    
private:
    Logger() = default;
    std::mutex mutex_;                      ///< Thread safety
    std::ofstream logFile_;                 ///< File output stream
    LogLevel currentLevel_ = LogLevel::INFO; ///< Minimum level
    bool consoleOutput_ = true;             ///< Console output flag
    
    std::string getCurrentTimestamp();
    std::string levelToString(LogLevel level);
};
