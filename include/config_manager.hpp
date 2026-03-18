#pragma once
#include <string>
#include <map>
#include <variant>
#include <nlohmann/json.hpp>

using ConfigValue = std::variant<bool, int, double, std::string>;

class ConfigManager {
public:
    static ConfigManager& getInstance();
    
    // Load configuration from file
    bool loadFromFile(const std::string& filename);
    
    // Save configuration to file
    bool saveToFile(const std::string& filename) const;
    
    // Get configuration values
    template<typename T>
    T get(const std::string& key, const T& defaultValue = T{}) const;
    
    // Set configuration values
    template<typename T>
    void set(const std::string& key, const T& value);
    
    // Check if key exists
    bool has(const std::string& key) const;
    
    // Remove a key
    void remove(const std::string& key);
    
    // Get all keys
    std::vector<std::string> getKeys() const;
    
    // Load default configuration
    void loadDefaults();
    
    // Validate configuration
    bool validate() const;
    
    // Print configuration
    void print() const;

private:
    ConfigManager() = default;
    std::map<std::string, ConfigValue> configMap_;
    
    // Helper methods
    ConfigValue fromJson(const nlohmann::json& j) const;
    nlohmann::json toJson(const ConfigValue& value) const;
    std::string typeToString(const ConfigValue& value) const;
};
