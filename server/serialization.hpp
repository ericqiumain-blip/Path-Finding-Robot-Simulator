#pragma once
#include "warehouse/simulation.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace warehouse::api {
using Json = nlohmann::json;
Json metrics(const MetricsSnapshot& value);
Json config(const SimulationConfig& value, const std::string& layout);
Json state(const SimulationEngine& engine, const std::string& layout);
void configure(SimulationConfig& target, std::string& layout, const Json& values);
Warehouse load_layout(const std::string& name, const SimulationConfig& config);
std::string csv(const Json& document);
} // namespace warehouse::api
