/**
 * @file config_manager.hpp
 * @brief Thread-safe configuration management
 * @author vol_arb Team
 * @version 2.0
 * @date 2024
 *
 * Provides JSON-based configuration management with:
 * - Type-safe value access
 * - File persistence
 * - Thread-safe singleton pattern
 * - Default value support
 *
 * ## Example Usage
 * @code
 * auto& config = ConfigManager::getInstance();
 * config.loadFromFile("config.json");
 *
 * // Get values with defaults
 * double tol = config.get<double>("qp.tolerance", 1e-9);
 * int maxIter = config.get<int>("qp.max_iterations", 10000);
 *
 * // Set values
 * config.set("qp.verbose", true);
 * config.saveToFile("config.json");
 * @endcode
 */

#pragma once
#include <string>
#include <map>
#include <variant>
#include <mutex>
#include <nlohmann/json.hpp>

/// Configuration value variant type
using ConfigValue = std::variant<bool, int, double, std::string>;

/**
 * @brief Thread-safe configuration manager singleton
 */
class ConfigManager {
public:
    /**
     * @brief Get singleton instance
     * @return Reference to config manager
     */
    static ConfigManager& getInstance();
    
    /**
     * @brief Load configuration from JSON file
     * @param filename Path to config file
     * @return True on success
     */
    bool loadFromFile(const std::string& filename);
    
    /**
     * @brief Save configuration to JSON file
     * @param filename Path to config file
     * @return True on success
     */
    bool saveToFile(const std::string& filename) const;
    
    /**
     * @brief Get configuration value
     * @tparam T Value type (bool, int, double, string)
     * @param key Configuration key (dot-separated path)
     * @param defaultValue Value if key not found
     * @return Configuration value or default
     */
    template<typename T>
    T get(const std::string& key, const T& defaultValue = T{}) const;
    
    /**
     * @brief Set configuration value
     * @tparam T Value type
     * @param key Configuration key
     * @param value Value to set
     */
    template<typename T>
    void set(const std::string& key, const T& value);
    
    /**
     * @brief Check if key exists
     * @param key Configuration key
     * @return True if key exists
     */
    bool has(const std::string& key) const;
    
    /**
     * @brief Remove configuration key
     * @param key Key to remove
     */
    void remove(const std::string& key);
    
    /**
     * @brief Get all configuration keys
     * @return Vector of all keys
     */
    std::vector<std::string> getKeys() const;
    
    /**
     * @brief Load default configuration values
     *
     * Sets sensible defaults for all library parameters.
     */
    void loadDefaults();
    
    /**
     * @brief Validate current configuration
     * @return True if configuration is valid
     */
    bool validate() const;
    
    /**
     * @brief Print configuration to stdout
     */
    void print() const;

private:
    ConfigManager() = default;
    std::map<std::string, ConfigValue> configMap_;
    mutable std::mutex mutex_;  ///< Thread safety
    
    ConfigValue fromJson(const nlohmann::json& j) const;
    nlohmann::json toJson(const ConfigValue& value) const;
    std::string typeToString(const ConfigValue& value) const;
};
