#include "warehouse/collision_manager.hpp"

#include <algorithm>
#include <queue>
#include <stdexcept>
#include <tuple>

namespace warehouse {
CollisionManager::Edge CollisionManager::edge(Position from, Position to) const {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(index(from))) << 32U) |
           static_cast<std::uint32_t>(index(to));
}
bool CollisionManager::available(Position from, Position to, Tick time, int robot_id) const {
    if (manhattan(from, to) > 1) return false;
    const auto frame = timeline_.find(time);
    if (frame == timeline_.end()) return true;
    const auto destination = frame->second.cells.find(index(to));
    if (destination != frame->second.cells.end() && destination->second != robot_id) return false;
    const auto reverse = frame->second.edges.find(edge(to, from));
    return reverse == frame->second.edges.end() || reverse->second == robot_id;
}
bool CollisionManager::reserve(Position from, Position to, Tick time, int robot_id) {
    if (!available(from, to, time, robot_id)) return false;
    auto& frame = timeline_[time];
    frame.cells[index(to)] = robot_id;
    frame.edges[edge(from, to)] = robot_id;
    return true;
}
std::size_t CollisionManager::reserve_path(Position start, const std::deque<Position>& path,
    Tick first_tick, int robot_id, std::size_t horizon) {
    std::size_t count = 0;
    for (const auto next : path) {
        if (count >= horizon || !reserve(start, next, first_tick + count, robot_id)) break;
        start = next;
        ++count;
    }
    return count;
}
std::size_t CollisionManager::reserve_path(Position start, const std::vector<Position>& path,
    Tick first_tick, int robot_id, std::size_t horizon, std::size_t start_index) {
    std::size_t count = 0;
    for (std::size_t i = start_index; i < path.size() && count < horizon; ++i) {
        if (!reserve(start, path[i], first_tick + count, robot_id)) break;
        start = path[i];
        ++count;
    }
    return count;
}
void CollisionManager::release(int robot_id) {
    for (auto it = timeline_.begin(); it != timeline_.end();) {
        std::erase_if(it->second.cells, [robot_id](const auto& item) { return item.second == robot_id; });
        std::erase_if(it->second.edges, [robot_id](const auto& item) { return item.second == robot_id; });
        if (it->second.cells.empty() && it->second.edges.empty()) it = timeline_.erase(it);
        else ++it;
    }
}
void CollisionManager::prune(Tick before) { timeline_.erase(timeline_.begin(), timeline_.lower_bound(before)); }
void CollisionManager::prune_future(Tick from) {
    timeline_.erase(timeline_.lower_bound(from), timeline_.end());
}
void CollisionManager::forecast(const Warehouse& map, const std::vector<Robot>& robots,
                               Tick first_tick, std::size_t horizon) {
    prune_future(first_tick);
    auto predicted = robots;
    std::vector<Position> desired(predicted.size());
    for (std::size_t step = 0; step < horizon; ++step) {
        const Tick arrival = first_tick + step;
        for (std::size_t i = 0; i < predicted.size(); ++i) {
            const auto& robot = predicted[i];
            const auto state = robot.state == RobotState::Waiting ? robot.resume_state : robot.state;
            const bool moving = state == RobotState::MovingToItem || state == RobotState::MovingToPacking ||
                                state == RobotState::MovingToCharge || state == RobotState::MovingToPark;
            desired[i] = moving && robot.path_index < robot.path.size() ? robot.path[robot.path_index] : robot.position;
        }
        const auto accepted = resolve(map, predicted, desired, arrival);
        for (std::size_t i = 0; i < predicted.size(); ++i) {
            auto& robot = predicted[i];
            const auto previous = robot.position;
            if (accepted[i]) { robot.position = desired[i]; ++robot.path_index; }
            reserve(previous, robot.position, arrival, robot.id);
        }
    }
}
void CollisionManager::clear() { timeline_.clear(); }
std::vector<Reservation> CollisionManager::reservations(Tick from, Tick through) const {
    std::vector<Reservation> result;
    for (auto it = timeline_.lower_bound(from); it != timeline_.end() && it->first <= through; ++it) {
        for (const auto& [cell, robot] : it->second.cells)
            result.push_back({{cell % width_, cell / width_}, it->first, robot});
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return std::tie(a.time, a.position.y, a.position.x, a.robot_id) <
               std::tie(b.time, b.position.y, b.position.x, b.robot_id);
    });
    return result;
}
std::vector<bool> CollisionManager::resolve(const Warehouse& map,
    const std::vector<Robot>& robots, const std::vector<Position>& desired, Tick tick) const {
    if (robots.size() != desired.size()) throw std::invalid_argument("Move proposals must match fleet size");
    const std::size_t count = robots.size();
    std::vector<bool> accepted(count, false);
    if (count == 0) return accepted;
    std::unordered_map<int, std::size_t> occupied;
    occupied.reserve(count * 2);
    for (std::size_t i = 0; i < count; ++i) {
        if (!occupied.emplace(map.index(robots[i].position), i).second)
            throw std::logic_error("Robots already share a cell before collision resolution");
    }
    std::unordered_map<int, std::size_t> claimed;
    claimed.reserve(count * 2);
    // Rotate arbitration so a persistent intersection conflict cannot always favor ID 0.
    const auto first = static_cast<std::size_t>(tick % count);
    for (std::size_t step = 0; step < count; ++step) {
        const auto i = (first + step) % count;
        if (desired[i] == robots[i].position || !map.traversable(desired[i]) ||
            manhattan(robots[i].position, desired[i]) != 1 ||
            !available(robots[i].position, desired[i], tick, robots[i].id)) continue;
        accepted[i] = claimed.emplace(map.index(desired[i]), i).second;
    }
    std::vector<std::vector<std::size_t>> followers(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (!accepted[i]) continue;
        const auto occupant = occupied.find(map.index(desired[i]));
        if (occupant == occupied.end()) continue;
        const auto j = occupant->second;
        followers[j].push_back(i);
        if (desired[j] == robots[i].position) {
            accepted[i] = false;
            accepted[j] = false;
        }
    }
    std::queue<std::size_t> stopped;
    for (std::size_t i = 0; i < count; ++i) if (!accepted[i]) stopped.push(i);
    while (!stopped.empty()) {
        const auto leader = stopped.front();
        stopped.pop();
        for (const auto follower : followers[leader]) {
            if (accepted[follower]) { accepted[follower] = false; stopped.push(follower); }
        }
    }
    return accepted;
}
} // namespace warehouse

