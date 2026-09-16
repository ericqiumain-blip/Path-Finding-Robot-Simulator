#pragma once

#include "warehouse/types.hpp"
#include "warehouse/warehouse.hpp"

#include <deque>
#include <map>
#include <unordered_map>
#include <vector>

namespace warehouse {

struct Reservation {
    Position position{};
    Tick time{};
    int robot_id{};
};

class CollisionManager {
public:
    explicit CollisionManager(int width = 1) : width_(width) {}
    [[nodiscard]] bool available(Position from, Position to, Tick time, int robot_id) const;
    bool reserve(Position from, Position to, Tick time, int robot_id);
    std::size_t reserve_path(Position start, const std::deque<Position>& path,
                             Tick first_tick, int robot_id, std::size_t horizon = 8);
    std::size_t reserve_path(Position start, const std::vector<Position>& path,
                             Tick first_tick, int robot_id, std::size_t horizon = 8,
                             std::size_t start_index = 0);
    void release(int robot_id);
    void prune(Tick before);
    void prune_future(Tick from);
    void forecast(const Warehouse& map, const std::vector<Robot>& robots,
                  Tick first_tick, std::size_t horizon = 4);
    void clear();
    [[nodiscard]] std::vector<Reservation> reservations(Tick from, Tick through) const;

    // Computes a simultaneous safe move set. Following a departing robot is legal;
    // swaps are forbidden, and rejecting a leader also rejects all its followers.
    [[nodiscard]] std::vector<bool> resolve(const Warehouse& map,
        const std::vector<Robot>& robots, const std::vector<Position>& desired,
        Tick tick) const;

private:
    using Edge = std::uint64_t;
    struct Frame {
        std::unordered_map<int, int> cells;
        std::unordered_map<Edge, int> edges;
    };
    [[nodiscard]] int index(Position p) const { return p.y * width_ + p.x; }
    [[nodiscard]] Edge edge(Position from, Position to) const;
    int width_;
    std::map<Tick, Frame> timeline_;
};

} // namespace warehouse


