#pragma once

#include "warehouse/path_planner.hpp"

#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace warehouse {

enum class SchedulingStrategy { Random, Nearest, Cost, Hungarian };
SchedulingStrategy parse_scheduler(const std::string& name);
std::string to_string(SchedulingStrategy strategy);

struct Assignment {
    int robot_id{};
    int order_id{};
    double cost{};
};

using AssignmentFeasibility = std::function<bool(const Robot&, const Order&, double)>;

class Scheduler {
public:
    virtual ~Scheduler() = default;
    virtual std::vector<Assignment> assign(const Warehouse& map,
        const std::vector<Robot>& robots, const std::vector<Order>& orders,
        PathPlanner& planner, std::mt19937& rng,
        const AssignmentFeasibility& feasible = {}) = 0;
};

std::unique_ptr<Scheduler> make_scheduler(SchedulingStrategy strategy);

// Minimum-cost rectangular assignment. Returns the chosen column for each row,
// or -1 for an unmatched row. Infinity denotes a forbidden pairing.
std::vector<int> hungarian_assignment(const std::vector<std::vector<double>>& costs);

} // namespace warehouse
