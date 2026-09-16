#pragma once

#include "warehouse/warehouse.hpp"

#include <unordered_set>
#include <vector>

namespace warehouse {

struct PathOptions {
    const std::vector<double>* congestion{};
    double congestion_weight{};
    const std::unordered_set<int>* blocked{};
};

struct PathResult {
    bool found{};
    // The current position is excluded; a successful zero-length trip has an empty path.
    std::vector<Position> path;
    double cost{};
    std::uint64_t nodes_explored{};
    double computation_ms{};
};

class PathPlanner {
public:
    virtual ~PathPlanner() = default;
    virtual PathResult plan(const Warehouse& map, Position start, Position goal,
                            const PathOptions& options = {}) = 0;
};

class AStarPlanner final : public PathPlanner {
public:
    PathResult plan(const Warehouse& map, Position start, Position goal,
                    const PathOptions& options = {}) override;
};

} // namespace warehouse
