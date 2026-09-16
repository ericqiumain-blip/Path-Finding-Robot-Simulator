#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace warehouse {

using Tick = std::uint64_t;

struct Position {
    int x{};
    int y{};
    bool operator==(const Position&) const = default;
};

enum class CellType { Floor, Wall, Shelf, Packing, Charging };
enum class RobotState {
    Idle, MovingToItem, PickingItem, MovingToPacking, DroppingItem,
    MovingToCharge, MovingToPark, Charging, Waiting, Failed
};

std::string to_string(RobotState state);
int manhattan(Position a, Position b);

struct Shelf {
    int id{};
    Position position{};
    Position pickup{};
};

struct PackingStation {
    int id{};
    Position position{};
    int capacity{1};
};

struct ChargingStation {
    int id{};
    Position position{};
    int capacity{1};
};

struct Order {
    int id{};
    Tick created_at{};
    int shelf_id{};
    int priority{1};
    std::optional<int> assigned_robot;
    std::optional<Tick> completed_at;
};

struct Task {
    int order_id{};
    int shelf_id{};
    Position pickup{};
    Position packing{};
    bool carrying{};
};

struct Robot {
    int id{};
    Position position{};
    std::optional<Position> destination;
    std::vector<Position> path;
    std::size_t path_index{};
    RobotState state{RobotState::Idle};
    RobotState resume_state{RobotState::Idle};
    std::optional<Task> task;
    double battery{100.0};
    double battery_capacity{100.0};
    std::uint64_t distance_travelled{};
    Tick idle_time{};
    Tick charging_time{};
    Tick working_time{};
    Tick battery_idle_time{};
    Tick state_ticks{};
    Tick blocked_ticks{};
    Tick failure_until{};
    bool permanent_failure{};
    std::uint64_t replans{};
};

} // namespace warehouse

