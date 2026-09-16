#include "warehouse/metrics.hpp"

#include <algorithm>
#include <cmath>

namespace warehouse {

void MetricsCollector::completed(double fulfillment_seconds) {
    fulfillment_times_.push_back(fulfillment_seconds);
    fulfillment_sum_ += fulfillment_seconds;
}

void MetricsCollector::path(double milliseconds, std::uint64_t explored) {
    pathfinding_ms_ += milliseconds;
    nodes_ += explored;
    ++pathfinding_calls_;
}

void MetricsCollector::observe(std::size_t total, std::size_t working, std::size_t idle,
                               std::size_t charging, std::size_t battery_waiting) {
    robot_ticks_ += total;
    working_ticks_ += working;
    idle_ticks_ += idle;
    charging_ticks_ += charging;
    battery_wait_ticks_ += battery_waiting;
}

MetricsSnapshot MetricsCollector::snapshot(std::uint64_t tick, double tick_seconds,
                                           std::size_t generated,
                                           std::size_t currently_failed) const {
    MetricsSnapshot result;
    result.current_tick = tick;
    result.simulation_seconds = static_cast<double>(tick) * tick_seconds;
    result.orders_generated = generated;
    result.orders_completed = fulfillment_times_.size();
    result.outstanding_orders = generated - result.orders_completed;
    if (result.simulation_seconds > 0.0) {
        result.orders_per_hour = static_cast<double>(result.orders_completed) * 3600.0 /
                                 result.simulation_seconds;
    }
    if (!fulfillment_times_.empty()) {
        result.avg_fulfillment_time = fulfillment_sum_ / static_cast<double>(result.orders_completed);
        auto sorted = fulfillment_times_;
        std::sort(sorted.begin(), sorted.end());
        const auto middle = sorted.size() / 2;
        result.median_fulfillment_time = sorted.size() % 2 == 0
            ? (sorted[middle - 1] + sorted[middle]) / 2.0 : sorted[middle];
        // Nearest-rank percentile is defined even for very small completed samples.
        const auto rank = static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(sorted.size())));
        result.p95_fulfillment_time = sorted[rank - 1];
    }
    result.robot_utilization = robot_ticks_ == 0 ? 0.0 :
        static_cast<double>(working_ticks_) / static_cast<double>(robot_ticks_);
    result.robot_idle_time = static_cast<double>(idle_ticks_) * tick_seconds;
    result.robot_charging_time = static_cast<double>(charging_ticks_) * tick_seconds;
    result.battery_wait_time = static_cast<double>(battery_wait_ticks_) * tick_seconds;
    result.total_distance = static_cast<double>(distance_);
    result.avg_distance_per_order = result.orders_completed == 0 ? 0.0 :
        result.total_distance / static_cast<double>(result.orders_completed);
    result.collisions_prevented = collisions_;
    result.path_replans = replans_;
    result.pathfinding_calls = pathfinding_calls_;
    result.nodes_explored = nodes_;
    result.avg_pathfinding_ms = pathfinding_calls_ == 0 ? 0.0 :
        pathfinding_ms_ / static_cast<double>(pathfinding_calls_);
    result.congestion_events = congestion_;
    result.failed_robots = currently_failed;
    result.failure_events = failures_;
    return result;
}

} // namespace warehouse
