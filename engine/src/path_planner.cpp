#include "warehouse/path_planner.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>

namespace warehouse {
PathResult AStarPlanner::plan(const Warehouse& map, Position start, Position goal,
                             const PathOptions& options) {
    const auto begin = std::chrono::steady_clock::now();
    PathResult result;
    const auto finish = [&] {
        result.computation_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - begin).count();
        return result;
    };
    if (options.congestion_weight < 0.0 || !std::isfinite(options.congestion_weight))
        throw std::invalid_argument("Congestion weight must be finite and nonnegative");
    if (options.congestion && options.congestion->size() != static_cast<std::size_t>(map.cell_count()))
        throw std::invalid_argument("Congestion field dimensions do not match warehouse");
    if (!map.traversable(start) || !map.traversable(goal)) return finish();
    if (start == goal) { result.found = true; return finish(); }
    const int source = map.index(start);
    const int target = map.index(goal);
    if (options.blocked && options.blocked->contains(target)) return finish();
    struct Candidate {
        double f;
        double g;
        int cell;
        bool operator>(const Candidate& other) const {
            if (f != other.f) return f > other.f;
            if (g != other.g) return g < other.g;
            return cell > other.cell;
        }
    };
    const auto size = static_cast<std::size_t>(map.cell_count());
    std::vector<double> cost(size, std::numeric_limits<double>::infinity());
    std::vector<int> parent(size, -1);
    std::priority_queue<Candidate, std::vector<Candidate>, std::greater<>> frontier;
    cost[source] = 0.0;
    frontier.push({static_cast<double>(manhattan(start, goal)), 0.0, source});
    constexpr std::array<Position, 4> directions{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};
    while (!frontier.empty()) {
        const auto current = frontier.top();
        frontier.pop();
        if (current.g != cost[current.cell]) continue;
        ++result.nodes_explored;
        if (current.cell == target) {
            result.found = true;
            result.cost = current.g;
            for (int cell = target; cell != source; cell = parent[cell])
                result.path.push_back(map.position(cell));
            std::reverse(result.path.begin(), result.path.end());
            return finish();
        }
        const auto p = map.position(current.cell);
        for (const auto d : directions) {
            const Position next{p.x + d.x, p.y + d.y};
            if (!map.traversable(next)) continue;
            const int cell = map.index(next);
            if (options.blocked && options.blocked->contains(cell)) continue;
            double penalty = 0.0;
            if (options.congestion) {
                const double density = (*options.congestion)[cell];
                if (!std::isfinite(density) || density < 0.0)
                    throw std::invalid_argument("Congestion density must be finite and nonnegative");
                penalty = density * options.congestion_weight;
            }
            const double candidate = current.g + 1.0 + penalty;
            if (candidate >= cost[cell]) continue;
            cost[cell] = candidate;
            parent[cell] = current.cell;
            frontier.push({candidate + manhattan(next, goal), candidate, cell});
        }
    }
    return finish();
}
} // namespace warehouse
