#include "warehouse/scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <unordered_map>

namespace warehouse {
SchedulingStrategy parse_scheduler(const std::string& name) {
    if (name == "random") return SchedulingStrategy::Random;
    if (name == "nearest") return SchedulingStrategy::Nearest;
    if (name == "cost" || name == "cost-based") return SchedulingStrategy::Cost;
    if (name == "hungarian" || name == "optimized") return SchedulingStrategy::Hungarian;
    throw std::invalid_argument("Unknown scheduler: " + name);
}
std::string to_string(SchedulingStrategy strategy) {
    switch (strategy) {
    case SchedulingStrategy::Random: return "random";
    case SchedulingStrategy::Nearest: return "nearest";
    case SchedulingStrategy::Cost: return "cost";
    case SchedulingStrategy::Hungarian: return "hungarian";
    }
    throw std::logic_error("Invalid scheduling strategy");
}
std::vector<int> hungarian_assignment(const std::vector<std::vector<double>>& costs) {
    if (costs.empty()) return {};
    const std::size_t rows = costs.size();
    const std::size_t columns = costs.front().size();
    double magnitude = 1.0;
    for (const auto& row : costs) {
        if (row.size() != columns) throw std::invalid_argument("Ragged assignment matrix");
        for (double value : row) {
            if (std::isnan(value) || value == -std::numeric_limits<double>::infinity())
                throw std::invalid_argument("Assignment costs must be finite or positive infinity");
            if (std::isfinite(value)) magnitude = std::max(magnitude, std::abs(value));
        }
    }
    // Dummy columns make an infeasible row unmatched. Their penalty prioritizes
    // maximum feasible cardinality before minimum total route cost.
    const double unmatched = magnitude * static_cast<double>(2 * rows + 2 * columns + 1);
    if (!std::isfinite(unmatched * 4.0)) throw std::invalid_argument("Assignment costs too large");
    const std::size_t total_columns = columns + rows;
    const auto value = [&](std::size_t row, std::size_t column) {
        if (column >= columns) return unmatched;
        return std::isfinite(costs[row][column]) ? costs[row][column] : unmatched * 4.0;
    };
    std::vector<double> row_potential(rows + 1), column_potential(total_columns + 1);
    std::vector<std::size_t> matched_row(total_columns + 1), predecessor(total_columns + 1);
    for (std::size_t row = 1; row <= rows; ++row) {
        matched_row[0] = row;
        std::size_t column = 0;
        std::vector<double> slack(total_columns + 1, std::numeric_limits<double>::infinity());
        std::vector<bool> visited(total_columns + 1, false);
        do {
            visited[column] = true;
            const auto active_row = matched_row[column];
            double delta = std::numeric_limits<double>::infinity();
            std::size_t next_column = 0;
            for (std::size_t candidate = 1; candidate <= total_columns; ++candidate) {
                if (visited[candidate]) continue;
                const double reduced = value(active_row - 1, candidate - 1) -
                    row_potential[active_row] - column_potential[candidate];
                if (reduced < slack[candidate]) {
                    slack[candidate] = reduced;
                    predecessor[candidate] = column;
                }
                if (slack[candidate] < delta) { delta = slack[candidate]; next_column = candidate; }
            }
            for (std::size_t candidate = 0; candidate <= total_columns; ++candidate) {
                if (visited[candidate]) {
                    row_potential[matched_row[candidate]] += delta;
                    column_potential[candidate] -= delta;
                } else slack[candidate] -= delta;
            }
            column = next_column;
        } while (matched_row[column] != 0);
        do {
            const auto previous = predecessor[column];
            matched_row[column] = matched_row[previous];
            column = previous;
        } while (column != 0);
    }
    std::vector<int> result(rows, -1);
    for (std::size_t column = 1; column <= columns; ++column) {
        const auto row = matched_row[column];
        if (row != 0 && std::isfinite(costs[row - 1][column - 1]))
            result[row - 1] = static_cast<int>(column - 1);
    }
    return result;
}
namespace {
class GridScheduler final : public Scheduler {
public:
    explicit GridScheduler(SchedulingStrategy strategy) : strategy_(strategy) {}
    std::vector<Assignment> assign(const Warehouse& map, const std::vector<Robot>& robots,
        const std::vector<Order>& orders, PathPlanner& planner, std::mt19937& rng,
        const AssignmentFeasibility& feasible) override {
        std::vector<std::size_t> idle;
        for (std::size_t i = 0; i < robots.size(); ++i)
            if (robots[i].state == RobotState::Idle && !robots[i].task) idle.push_back(i);
        if (idle.empty()) return {};
        std::vector<const Order*> pending;
        for (const auto& order : orders)
            if (!order.completed_at && !order.assigned_robot && order.shelf_id >= 0 &&
                static_cast<std::size_t>(order.shelf_id) < map.shelves().size()) pending.push_back(&order);
        std::stable_sort(pending.begin(), pending.end(), [](const Order* a, const Order* b) {
            if (a->priority != b->priority) return a->priority > b->priority;
            if (a->created_at != b->created_at) return a->created_at < b->created_at;
            return a->id < b->id;
        });
        // The row dimension drives Hungarian complexity; all idle robots remain
        // eligible while a bounded order batch is reconsidered every scheduling tick.
        constexpr std::size_t batch_limit = 64;
        if (pending.size() > batch_limit) pending.resize(batch_limit);
        if (pending.empty()) return {};
        if (cached_map_ != &map) { fields_.clear(); field_order_.clear(); cached_map_ = &map; }
        if (strategy_ == SchedulingStrategy::Random) std::shuffle(idle.begin(), idle.end(), rng);
        const double infinity = std::numeric_limits<double>::infinity();
        std::vector<std::vector<double>> distances(pending.size(), std::vector<double>(idle.size(), infinity));
        for (std::size_t row = 0; row < pending.size(); ++row) {
            const auto pickup = map.shelves()[static_cast<std::size_t>(pending[row]->shelf_id)].pickup;
            const auto& field = distance_field(map, pickup);
            for (std::size_t column = 0; column < idle.size(); ++column) {
                const auto& robot = robots[idle[column]];
                const int distance = field[static_cast<std::size_t>(map.index(robot.position))];
                if (distance >= 0 && (!feasible || feasible(robot, *pending[row], distance)))
                    distances[row][column] = static_cast<double>(distance);
            }
        }
        std::vector<int> selected(pending.size(), -1);
        if (strategy_ == SchedulingStrategy::Hungarian) selected = hungarian_assignment(distances);
        else {
            std::vector<bool> used(idle.size(), false);
            for (std::size_t row = 0; row < pending.size(); ++row) {
                double best = infinity;
                const auto pickup = map.shelves()[static_cast<std::size_t>(pending[row]->shelf_id)].pickup;
                for (std::size_t column = 0; column < idle.size(); ++column) {
                    if (used[column] || !std::isfinite(distances[row][column])) continue;
                    double score = distances[row][column];
                    if (strategy_ == SchedulingStrategy::Nearest)
                        score = static_cast<double>(manhattan(robots[idle[column]].position, pickup));
                    if (strategy_ == SchedulingStrategy::Random) score = static_cast<double>(column);
                    if (score < best) { best = score; selected[row] = static_cast<int>(column); }
                }
                if (selected[row] >= 0) used[static_cast<std::size_t>(selected[row])] = true;
            }
        }
        std::vector<Assignment> result;
        for (std::size_t row = 0; row < selected.size(); ++row) {
            if (selected[row] < 0) continue;
            const auto column = static_cast<std::size_t>(selected[row]);
            const auto& robot = robots[idle[column]];
            double cost = distances[row][column];
            if (strategy_ == SchedulingStrategy::Cost) {
                const auto pickup = map.shelves()[static_cast<std::size_t>(pending[row]->shelf_id)].pickup;
                const auto route = planner.plan(map, robot.position, pickup);
                if (!route.found) continue;
                cost = route.cost;
                if (feasible && !feasible(robot, *pending[row], cost)) continue;
            }
            result.push_back({robot.id, pending[row]->id, cost});
        }
        return result;
    }
private:
    const std::vector<int>& distance_field(const Warehouse& map, Position goal) {
        const int key = map.index(goal);
        const auto existing = fields_.find(key);
        if (existing != fields_.end()) return existing->second;
        constexpr std::size_t cache_limit = 128;
        if (fields_.size() >= cache_limit) { fields_.erase(field_order_.front()); field_order_.pop_front(); }
        field_order_.push_back(key);
        return fields_.emplace(key, map.distance_field(goal)).first->second;
    }
    SchedulingStrategy strategy_;
    const Warehouse* cached_map_{};
    std::unordered_map<int, std::vector<int>> fields_;
    std::deque<int> field_order_;
};
} // namespace
std::unique_ptr<Scheduler> make_scheduler(SchedulingStrategy strategy) {
    return std::make_unique<GridScheduler>(strategy);
}
} // namespace warehouse
