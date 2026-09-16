#include "warehouse/warehouse.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <queue>
#include <random>
#include <stdexcept>

namespace warehouse {
namespace {
constexpr std::array<Position, 4> directions{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};
}
Warehouse::Warehouse(std::vector<std::string> rows) : rows_(std::move(rows)) {
    if (rows_.empty() || rows_.front().empty()) throw std::invalid_argument("Warehouse map is empty");
    width_ = static_cast<int>(rows_.front().size());
    height_ = static_cast<int>(rows_.size());
    for (int y = 0; y < height_; ++y) {
        if (static_cast<int>(rows_[y].size()) != width_)
            throw std::invalid_argument("Warehouse map rows must have equal lengths");
        for (int x = 0; x < width_; ++x) {
            const Position p{x, y};
            switch (rows_[y][x]) {
            case '.': floor_.push_back(p); break;
            case '#': break;
            case 'S': break;
            case 'P': packing_.push_back({static_cast<int>(packing_.size()), p, 1}); break;
            case 'C': charging_.push_back({static_cast<int>(charging_.size()), p, 1}); break;
            default: throw std::invalid_argument("Unknown warehouse map cell: " + std::string(1, rows_[y][x]));
            }
        }
    }
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            if (rows_[y][x] != 'S') continue;
            const auto adjacent = neighbors({x, y});
            if (adjacent.empty()) throw std::invalid_argument("Shelf has no traversable pickup location");
            shelves_.push_back({static_cast<int>(shelves_.size()), {x, y}, adjacent.front()});
        }
    }
}
Warehouse Warehouse::load(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open warehouse layout: " + path.string());
    std::vector<std::string> rows;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == ';') continue;
        rows.push_back(line);
    }
    return Warehouse(std::move(rows));
}
Warehouse Warehouse::procedural(int width, int height, std::uint32_t seed) {
    if (width < 10 || height < 10 || width > 2048 || height > 2048)
        throw std::invalid_argument("Procedural map dimensions must be between 10 and 2048");
    std::vector<std::string> rows(static_cast<std::size_t>(height), std::string(static_cast<std::size_t>(width), '.'));
    for (int y = 0; y < height; ++y) {
        rows[y][0] = rows[y][width - 1] = '#';
    }
    std::fill(rows.front().begin(), rows.front().end(), '#');
    std::fill(rows.back().begin(), rows.back().end(), '#');
    std::mt19937 rng(seed);
    const int cross_aisle = 5 + static_cast<int>(rng() % 2);
    for (int y = 3; y < height - 3; ++y) {
        if (y % cross_aisle == 0) continue;
        for (int x = 4; x < width - 3; ++x) {
            if ((x - 4) % 6 < 2) rows[y][x] = 'S';
        }
    }
    for (int y = 2; y < height - 2; y += 3) {
        rows[y][1] = 'C';
        rows[y][width - 2] = 'P';
    }
    return Warehouse(std::move(rows));
}
Warehouse Warehouse::for_fleet(int robot_count, std::uint32_t seed) {
    if (robot_count < 1 || robot_count > 10000) throw std::invalid_argument("Robot count must be in [1,10000]");
    // Spacious low-density layouts retain room to pass and park even at 1,000 robots.
    const int side = std::max(18, static_cast<int>(std::ceil(std::sqrt(robot_count * 10.0))) + 4);
    return procedural(side + side / 3, side, seed);
}
bool Warehouse::contains(Position p) const { return p.x >= 0 && p.y >= 0 && p.x < width_ && p.y < height_; }
bool Warehouse::traversable(Position p) const {
    return contains(p) && rows_[p.y][p.x] != '#' && rows_[p.y][p.x] != 'S';
}
CellType Warehouse::at(Position p) const {
    if (!contains(p)) throw std::out_of_range("Position outside warehouse");
    switch (rows_[p.y][p.x]) {
    case '#': return CellType::Wall;
    case 'S': return CellType::Shelf;
    case 'P': return CellType::Packing;
    case 'C': return CellType::Charging;
    default: return CellType::Floor;
    }
}
std::vector<Position> Warehouse::neighbors(Position p) const {
    std::vector<Position> result;
    result.reserve(4);
    for (const auto d : directions) {
        Position n{p.x + d.x, p.y + d.y};
        if (traversable(n)) result.push_back(n);
    }
    return result;
}
std::vector<int> Warehouse::distance_field(Position goal) const {
    std::vector<int> distance(static_cast<std::size_t>(cell_count()), -1);
    if (!traversable(goal)) return distance;
    std::vector<int> queue;
    queue.reserve(distance.size());
    queue.push_back(index(goal));
    distance[index(goal)] = 0;
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const int current = queue[head];
        const Position p = position(current);
        for (const auto d : directions) {
            const Position n{p.x + d.x, p.y + d.y};
            if (!traversable(n)) continue;
            const int next = index(n);
            if (distance[next] >= 0) continue;
            distance[next] = distance[current] + 1;
            queue.push_back(next);
        }
    }
    return distance;
}
Position Warehouse::nearest_packing(Position from) const {
    const auto distance = distance_field(from);
    int best = std::numeric_limits<int>::max();
    std::optional<Position> result;
    for (const auto& station : packing_) {
        const int d = distance[index(station.position)];
        if (d >= 0 && d < best) { best = d; result = station.position; }
    }
    if (!result) throw std::runtime_error("No reachable packing station");
    return *result;
}
Position Warehouse::nearest_charging(Position from) const {
    const auto distance = distance_field(from);
    int best = std::numeric_limits<int>::max();
    std::optional<Position> result;
    for (const auto& station : charging_) {
        const int d = distance[index(station.position)];
        if (d >= 0 && d < best) { best = d; result = station.position; }
    }
    if (!result) throw std::runtime_error("No reachable charging station");
    return *result;
}
} // namespace warehouse
