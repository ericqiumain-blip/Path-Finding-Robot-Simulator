#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace warehouse {

struct MetricsSnapshot {
    std::uint64_t current_tick{};
    double simulation_seconds{};
    std::size_t orders_generated{};
    std::size_t orders_completed{};
    std::size_t outstanding_orders{};
    double orders_per_hour{};
    double avg_fulfillment_time{};
    double median_fulfillment_time{};
    double p95_fulfillment_time{};
    double robot_utilization{};
    double robot_idle_time{};
    double robot_charging_time{};
    double battery_wait_time{};
    double total_distance{};
    double avg_distance_per_order{};
    std::uint64_t collisions_prevented{};
    std::uint64_t path_replans{};
    std::uint64_t pathfinding_calls{};
    std::uint64_t nodes_explored{};
    double avg_pathfinding_ms{};
    std::uint64_t congestion_events{};
    std::size_t failed_robots{};
    std::uint64_t failure_events{};
};

/// Accumulates event counters and simulated time; wall time only measures A*.
class MetricsCollector {
public:
    void completed(double fulfillment_seconds);
    void path(double milliseconds, std::uint64_t explored);
    void movement() noexcept { ++distance_; }
    void collision(std::uint64_t count = 1) noexcept { collisions_ += count; }
    void replan() noexcept { ++replans_; }
    void congestion() noexcept { ++congestion_; }
    void failure() noexcept { ++failures_; }
    void observe(std::size_t total, std::size_t working, std::size_t idle,
                 std::size_t charging, std::size_t battery_waiting);

    [[nodiscard]] MetricsSnapshot snapshot(std::uint64_t tick, double tick_seconds,
                                           std::size_t generated,
                                           std::size_t currently_failed) const;

private:
    std::vector<double> fulfillment_times_;
    double fulfillment_sum_{};
    double pathfinding_ms_{};
    std::uint64_t pathfinding_calls_{};
    std::uint64_t nodes_{};
    std::uint64_t distance_{};
    std::uint64_t collisions_{};
    std::uint64_t replans_{};
    std::uint64_t congestion_{};
    std::uint64_t failures_{};
    std::uint64_t robot_ticks_{};
    std::uint64_t working_ticks_{};
    std::uint64_t idle_ticks_{};
    std::uint64_t charging_ticks_{};
    std::uint64_t battery_wait_ticks_{};
};

} // namespace warehouse
