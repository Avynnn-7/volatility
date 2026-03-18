#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <memory>
#include <chrono>
#include <sstream>

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3,
    CRITICAL = 4
};

class Logger {
public:
    static Logger& getInstance();
    
    void setLogFile(const std::string& filename);
    void setLogLevel(LogLevel level);
    void enableConsoleOutput(bool enable);
    
    template<typename... Args>
    void log(LogLevel level, const std::string& format, Args... args);
    
    void debug(const std::string& message);
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
    void critical(const std::string& message);
    
private:
    Logger() = default;
    std::mutex mutex_;
    std::ofstream logFile_;
    LogLevel currentLevel_ = LogLevel::INFO;
    bool consoleOutput_ = true;
    
    std::string getCurrentTimestamp();
    std::string levelToString(LogLevel level);
};
