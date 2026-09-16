#include "warehouse/types.hpp"

#include <cstdlib>
#include <stdexcept>

namespace warehouse {
std::string to_string(RobotState state) {
    switch (state) {
    case RobotState::Idle: return "IDLE";
    case RobotState::MovingToItem: return "MOVING_TO_ITEM";
    case RobotState::PickingItem: return "PICKING_ITEM";
    case RobotState::MovingToPacking: return "MOVING_TO_PACKING";
    case RobotState::DroppingItem: return "DROPPING_ITEM";
    case RobotState::MovingToCharge: return "MOVING_TO_CHARGE";
    case RobotState::MovingToPark: return "MOVING_TO_PARK";
    case RobotState::Charging: return "CHARGING";
    case RobotState::Waiting: return "WAITING";
    case RobotState::Failed: return "FAILED";
    }
    throw std::logic_error("Invalid robot state");
}
int manhattan(Position a, Position b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y); }
} // namespace warehouse
