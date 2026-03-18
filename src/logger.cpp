#include "logger.hpp"
#include <iostream>
#include <iomanip>
#include <cstdio>
#include <cstdarg>

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

void Logger::setLogFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (logFile_.is_open()) {
        logFile_.close();
    }
    logFile_.open(filename, std::ios::app);
}

void Logger::setLogLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentLevel_ = level;
}

void Logger::enableConsoleOutput(bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);
    consoleOutput_ = enable;
}

template<typename... Args>
void Logger::log(LogLevel level, const std::string& format, Args... args) {
    if (level < currentLevel_) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Format message
    int size = std::snprintf(nullptr, 0, format.c_str(), args...) + 1;
    std::unique_ptr<char[]> buf(new char[size]);
    std::snprintf(buf.get(), size, format.c_str(), args...);
    std::string message(buf.get(), buf.get() + size - 1);
    
    // Add timestamp and level
    std::string logEntry = "[" + getCurrentTimestamp() + "] " + 
                          levelToString(level) + ": " + message;
    
    // Output to console
    if (consoleOutput_) {
        std::cout << logEntry << std::endl;
    }
    
    // Output to file
    if (logFile_.is_open()) {
        logFile_ << logEntry << std::endl;
        logFile_.flush();
    }
}

void Logger::debug(const std::string& message) {
    log(LogLevel::DEBUG, "%s", message.c_str());
}

void Logger::info(const std::string& message) {
    log(LogLevel::INFO, "%s", message.c_str());
}

void Logger::warning(const std::string& message) {
    log(LogLevel::WARNING, "%s", message.c_str());
}

void Logger::error(const std::string& message) {
    log(LogLevel::ERROR, "%s", message.c_str());
}

void Logger::critical(const std::string& message) {
    log(LogLevel::CRITICAL, "%s", message.c_str());
}

std::string Logger::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

std::string Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:    return "DEBUG";
        case LogLevel::INFO:     return "INFO";
        case LogLevel::WARNING:  return "WARNING";
        case LogLevel::ERROR:    return "ERROR";
        case LogLevel::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}
