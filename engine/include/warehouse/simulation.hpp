#pragma once

#include "warehouse/collision_manager.hpp"
#include "warehouse/metrics.hpp"
#include "warehouse/path_planner.hpp"
#include "warehouse/scheduler.hpp"

#include <memory>
#include <random>
#include <string>
#include <unordered_map>

namespace warehouse {

struct SimulationConfig {
    std::size_t robot_count{32};
    std::size_t max_orders{1000};
    std::size_t initial_orders{20};
    double order_rate{1.5}; // Orders per simulated second, sampled independently of robots.
    double tick_seconds{1.0};
    std::uint64_t seed{42};
    std::string scheduler{"nearest"};
    bool battery_enabled{true};
    double battery_capacity{100.0};
    double move_energy{0.08};
    double action_energy{0.02};
    double charge_rate{2.5}; // Energy per simulated second.
    double charge_threshold{22.0}; // Percent of capacity.
    double battery_reserve{3.0}; // Absolute energy units.
    bool failures_enabled{false};
    double failure_probability{0.00005}; // Per robot per tick.
    double permanent_failure_probability{0.0};
    Tick failure_duration{30};
    Tick pickup_ticks{2};
    Tick dropoff_ticks{2};
    double congestion_weight{1.5};
    std::size_t scheduling_interval{3};
    std::size_t batch_size{48};

    void validate() const;
};

/// Single-threaded, reproducible simulation. A tick plans all moves, arbitrates
/// reservations, and then commits accepted moves simultaneously.
class SimulationEngine {
public:
    SimulationEngine(Warehouse warehouse, SimulationConfig config = {});
    void tick();
    void step(std::size_t ticks);
    void forceFailure(int robot_id, Tick duration, bool permanent = false);

    [[nodiscard]] Tick currentTick() const noexcept { return tick_; }
    [[nodiscard]] const Warehouse& warehouse() const noexcept { return warehouse_; }
    [[nodiscard]] const std::vector<Robot>& robots() const noexcept { return robots_; }
    [[nodiscard]] const std::vector<Order>& orders() const noexcept { return orders_; }
    [[nodiscard]] const SimulationConfig& config() const noexcept { return config_; }
    [[nodiscard]] MetricsSnapshot metrics() const;
    [[nodiscard]] const CollisionManager& collisionManager() const noexcept { return collision_; }
    [[nodiscard]] const std::vector<std::uint64_t>& heatmap() const noexcept { return traffic_; }
    [[nodiscard]] bool complete() const;

private:
    struct ShelfRoute { Position packing; int delivery; int recharge; };
    struct Runtime {
        std::optional<int> charger;
        bool battery_waiting{};
        bool yielding{};
        Tick next_plan_tick{};
    };

    Warehouse warehouse_;
    SimulationConfig config_;
    Tick tick_{};
    std::vector<Robot> robots_;
    std::vector<Runtime> runtime_;
    std::vector<Order> orders_;
    std::vector<ShelfRoute> shelf_routes_;
    std::vector<int> charger_owners_;
    std::vector<std::uint64_t> traffic_;
    std::vector<double> congestion_;
    std::unordered_map<int, std::vector<int>> distance_cache_;
    std::mt19937 workload_rng_;
    std::mt19937 failure_rng_;
    std::mt19937 scheduler_rng_;
    AStarPlanner planner_;
    std::unique_ptr<Scheduler> scheduler_;
    CollisionManager collision_;
    MetricsCollector metrics_;

    void generateOrder();
    void assignTasks();
    void updateStates();
    void moveRobots();
    void observe();
    void releaseTask(Robot& robot);
    void releaseCharger(Robot& robot);
    bool requestCharge(Robot& robot);
    bool park(Robot& robot, std::optional<Position> avoid = std::nullopt);
    bool plan(Robot& robot, Position goal, RobotState state, bool dynamic = false);
    void arrived(Robot& robot);
    bool feasible(const Robot& robot, const Order& order);
    int distance(Position from, Position to);
    int chargerDistance(Position position);
    Position packingFor(Position pickup);
    void setState(Robot& robot, RobotState state);
};

} // namespace warehouse



