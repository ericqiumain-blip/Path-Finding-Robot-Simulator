#include "warehouse/simulation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace warehouse {
namespace {
constexpr int unreachable = std::numeric_limits<int>::max() / 4;

bool moving(RobotState state) {
    return state == RobotState::MovingToItem || state == RobotState::MovingToPacking ||
           state == RobotState::MovingToCharge || state == RobotState::MovingToPark;
}

class MeasuredPlanner final : public PathPlanner {
public:
    MeasuredPlanner(PathPlanner& planner, MetricsCollector& metrics)
        : planner_(planner), metrics_(metrics) {}
    PathResult plan(const Warehouse& map, Position from, Position to,
                    const PathOptions& options = {}) override {
        auto result = planner_.plan(map, from, to, options);
        metrics_.path(result.computation_ms, result.nodes_explored);
        return result;
    }
private:
    PathPlanner& planner_;
    MetricsCollector& metrics_;
};
}

void SimulationConfig::validate() const {
    auto finite_nonnegative = [](double value) { return std::isfinite(value) && value >= 0.0; };
    if (robot_count == 0 || robot_count > 10000) throw std::invalid_argument("robot_count must be between 1 and 10000");
    if (max_orders > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("max_orders exceeds the order ID range");
    if (!finite_nonnegative(order_rate) || order_rate > 100000.0)
        throw std::invalid_argument("order_rate must be finite and between 0 and 100000");
    if (!std::isfinite(tick_seconds) || tick_seconds <= 0.0 || tick_seconds > 3600.0)
        throw std::invalid_argument("tick_seconds must be positive and at most 3600");
    if (!std::isfinite(battery_capacity) || battery_capacity <= 0.0 ||
        !finite_nonnegative(move_energy) || !finite_nonnegative(action_energy) ||
        !std::isfinite(charge_rate) || charge_rate <= 0.0 ||
        !finite_nonnegative(charge_threshold) || charge_threshold > 100.0 ||
        !finite_nonnegative(battery_reserve) || battery_reserve >= battery_capacity)
        throw std::invalid_argument("invalid battery configuration");
    if (!finite_nonnegative(failure_probability) || failure_probability > 1.0 ||
        !finite_nonnegative(permanent_failure_probability) || permanent_failure_probability > 1.0)
        throw std::invalid_argument("failure probabilities must be in [0, 1]");
    if (failure_duration == 0 || pickup_ticks == 0 || dropoff_ticks == 0)
        throw std::invalid_argument("failure and service durations must be positive");
    if (!finite_nonnegative(congestion_weight) || scheduling_interval == 0 ||
        batch_size == 0 || batch_size > 256)
        throw std::invalid_argument("invalid scheduling or congestion configuration");
    (void)parse_scheduler(scheduler);
}

SimulationEngine::SimulationEngine(Warehouse map, SimulationConfig config)
    : warehouse_(std::move(map)), config_(std::move(config)),
      traffic_(static_cast<std::size_t>(warehouse_.cell_count())),
      congestion_(traffic_.size()),
      workload_rng_(static_cast<std::uint32_t>(config_.seed)),
      failure_rng_(static_cast<std::uint32_t>(config_.seed ^ 0x9e3779b9ULL)),
      scheduler_rng_(static_cast<std::uint32_t>(config_.seed ^ 0x85ebca6bULL)),
      collision_(warehouse_.width()) {
    config_.validate();
    scheduler_ = make_scheduler(parse_scheduler(config_.scheduler));
    if (warehouse_.shelves().empty() || warehouse_.packing_stations().empty())
        throw std::invalid_argument("warehouse needs shelves and packing stations");
    if (config_.battery_enabled && warehouse_.charging_stations().empty())
        throw std::invalid_argument("battery simulation requires a charging station");
    auto starts = warehouse_.floor_positions();
    if (starts.size() <= config_.robot_count)
        throw std::invalid_argument("warehouse needs more floor cells than robots, including room to yield");
    // Spawning never consumes the workload stream: changing fleet size preserves orders.
    std::mt19937 spawn_rng(static_cast<std::uint32_t>(config_.seed ^ 0xc2b2ae35ULL));
    std::shuffle(starts.begin(), starts.end(), spawn_rng);
    robots_.reserve(config_.robot_count);
    runtime_.resize(config_.robot_count);
    for (std::size_t i = 0; i < config_.robot_count; ++i) {
        Robot robot;
        robot.id = static_cast<int>(i);
        robot.position = starts[i];
        robot.battery = config_.battery_capacity;
        robot.battery_capacity = config_.battery_capacity;
        robots_.push_back(std::move(robot));
    }
    charger_owners_.assign(warehouse_.charging_stations().size(), -1);
    for (const auto& shelf : warehouse_.shelves()) {
        const auto packing = packingFor(shelf.pickup);
        shelf_routes_.push_back({packing, distance(shelf.pickup, packing), chargerDistance(packing)});
    }
    orders_.reserve(std::min<std::size_t>(config_.max_orders, 100000));
    for (std::size_t i = 0; i < std::min(config_.initial_orders, config_.max_orders); ++i) generateOrder();
}

void SimulationEngine::generateOrder() {
    if (orders_.size() >= config_.max_orders) return;
    std::uniform_int_distribution<int> shelves(0, static_cast<int>(warehouse_.shelves().size()) - 1);
    std::discrete_distribution<int> priorities({0.70, 0.23, 0.07});
    Order order;
    order.id = static_cast<int>(orders_.size());
    order.created_at = tick_;
    order.shelf_id = warehouse_.shelves()[static_cast<std::size_t>(shelves(workload_rng_))].id;
    order.priority = priorities(workload_rng_) + 1;
    orders_.push_back(order);
}

int SimulationEngine::distance(Position from, Position to) {
    const auto key = warehouse_.index(to);
    auto found = distance_cache_.find(key);
    if (found == distance_cache_.end())
        found = distance_cache_.emplace(key, warehouse_.distance_field(to)).first;
    const int value = found->second[static_cast<std::size_t>(warehouse_.index(from))];
    return value < 0 ? unreachable : value;
}

int SimulationEngine::chargerDistance(Position position) {
    int best = unreachable;
    for (const auto& station : warehouse_.charging_stations()) best = std::min(best, distance(position, station.position));
    return best;
}

Position SimulationEngine::packingFor(Position pickup) {
    Position best = warehouse_.packing_stations().front().position;
    int best_distance = unreachable;
    for (const auto& station : warehouse_.packing_stations()) {
        const int candidate = distance(pickup, station.position);
        if (candidate < best_distance) { best_distance = candidate; best = station.position; }
    }
    return best;
}

bool SimulationEngine::feasible(const Robot& robot, const Order& order) {
    const auto pickup = warehouse_.shelves().at(static_cast<std::size_t>(order.shelf_id)).pickup;
    const auto& route = shelf_routes_.at(static_cast<std::size_t>(order.shelf_id));
    const auto approach = distance(robot.position, pickup);
    const auto delivery = route.delivery;
    if (approach == unreachable || delivery == unreachable) return false;
    if (!config_.battery_enabled) return true;
    const auto recharge = route.recharge;
    if (recharge == unreachable) return false;
    const double required = (static_cast<double>(approach) + delivery + recharge) * config_.move_energy +
        static_cast<double>(config_.pickup_ticks + config_.dropoff_ticks) * config_.action_energy + config_.battery_reserve;
    return robot.battery + 0.000001 >= required;
}

void SimulationEngine::setState(Robot& robot, RobotState state) {
    robot.state = state;
    if (state != RobotState::Waiting) robot.resume_state = state;
    robot.state_ticks = 0;
}

bool SimulationEngine::plan(Robot& robot, Position goal, RobotState state, bool dynamic) {
    std::unordered_set<int> blocked;
    for (const auto& other : robots_) {
        if (other.id != robot.id && other.position != goal &&
            (dynamic || other.state == RobotState::Failed)) blocked.insert(warehouse_.index(other.position));
    }
    PathOptions options{&congestion_, state == RobotState::MovingToCharge ? 0.0 : config_.congestion_weight, &blocked};
    auto result = planner_.plan(warehouse_, robot.position, goal, options);
    metrics_.path(result.computation_ms, result.nodes_explored);
    robot.destination = goal;
    robot.path = std::move(result.path);
    robot.path_index = 0;
    setState(robot, state);
    runtime_[static_cast<std::size_t>(robot.id)].next_plan_tick = tick_ + 4;
    if (!result.found) {
        robot.resume_state = state;
        robot.state = RobotState::Waiting;
    }
    return result.found;
}

void SimulationEngine::releaseTask(Robot& robot) {
    if (robot.task) {
        auto& order = orders_.at(static_cast<std::size_t>(robot.task->order_id));
        if (!order.completed_at) order.assigned_robot.reset();
        robot.task.reset();
    }
    robot.path.clear();
    robot.path_index = 0;
    robot.destination.reset();
}

void SimulationEngine::releaseCharger(Robot& robot) {
    auto& runtime = runtime_.at(static_cast<std::size_t>(robot.id));
    if (runtime.charger) {
        charger_owners_[static_cast<std::size_t>(*runtime.charger)] = -1;
        runtime.charger.reset();
    }
}

bool SimulationEngine::requestCharge(Robot& robot) {
    auto& runtime = runtime_[static_cast<std::size_t>(robot.id)];
    if (runtime.charger) return true;
    int best = -1;
    int best_distance = unreachable;
    const auto& stations = warehouse_.charging_stations();
    for (std::size_t i = 0; i < stations.size(); ++i) {
        if (charger_owners_[i] != -1) continue;
        const auto destination = stations[i].position;
        const bool failed_blocker = std::any_of(robots_.begin(), robots_.end(), [&](const Robot& other) {
            return other.id != robot.id && other.position == destination && other.state == RobotState::Failed;
        });
        if (failed_blocker) continue;
        const int candidate = distance(robot.position, destination);
        if (candidate < best_distance && static_cast<double>(candidate) * config_.move_energy <= robot.battery + 0.000001) {
            best = static_cast<int>(i);
            best_distance = candidate;
        }
    }
    runtime.battery_waiting = true;
    if (best == -1) return false;
    runtime.charger = best;
    charger_owners_[static_cast<std::size_t>(best)] = robot.id;
    plan(robot, stations[static_cast<std::size_t>(best)].position, RobotState::MovingToCharge);
    return true;
}

bool SimulationEngine::park(Robot& robot, std::optional<Position> avoid) {
    std::unordered_set<int> occupied;
    for (const auto& other : robots_) if (other.id != robot.id) occupied.insert(warehouse_.index(other.position));
    std::unordered_set<int> pickups;
    for (const auto& shelf : warehouse_.shelves()) pickups.insert(warehouse_.index(shelf.pickup));
    Position best = robot.position;
    int best_score = unreachable;
    for (auto candidate : warehouse_.floor_positions()) {
        if (candidate == robot.position || (avoid && candidate == *avoid) ||
            occupied.contains(warehouse_.index(candidate)) || pickups.contains(warehouse_.index(candidate))) continue;
        const int manhattan_distance = manhattan(robot.position, candidate);
        if (manhattan_distance > 12) continue;
        const int score = manhattan_distance * 10 + static_cast<int>(warehouse_.neighbors(candidate).size());
        if (score < best_score) { best = candidate; best_score = score; }
    }
    if (best == robot.position) return false;
    return plan(robot, best, RobotState::MovingToPark, true);
}

void SimulationEngine::assignTasks() {
    std::vector<Order> pending;
    pending.reserve(config_.batch_size);
    for (const auto& order : orders_) if (!order.completed_at && !order.assigned_robot) pending.push_back(order);
    if (pending.empty()) return;
    std::stable_sort(pending.begin(), pending.end(), [](const Order& a, const Order& b) {
        return a.priority > b.priority;
    });
    if (pending.size() > config_.batch_size) pending.resize(config_.batch_size);
    MeasuredPlanner measured(planner_, metrics_);
    const auto assignments = scheduler_->assign(warehouse_, robots_, pending, measured, scheduler_rng_,
        [this](const Robot& robot, const Order& order, double) { return feasible(robot, order); });
    for (const auto& assignment : assignments) {
        auto& robot = robots_.at(static_cast<std::size_t>(assignment.robot_id));
        auto& order = orders_.at(static_cast<std::size_t>(assignment.order_id));
        if (robot.state != RobotState::Idle || robot.task || order.assigned_robot || order.completed_at) continue;
        const auto& shelf = warehouse_.shelves().at(static_cast<std::size_t>(order.shelf_id));
        robot.task = Task{order.id, shelf.id, shelf.pickup, shelf_routes_[static_cast<std::size_t>(shelf.id)].packing, false};
        order.assigned_robot = robot.id;
        runtime_[static_cast<std::size_t>(robot.id)].battery_waiting = false;
        plan(robot, shelf.pickup, RobotState::MovingToItem);
    }
    // Idle robots that cannot afford a queued job proactively replenish, even above
    // the normal low-battery threshold. A full battery never creates charge loops.
    if (config_.battery_enabled) for (auto& robot : robots_) {
        if (robot.state != RobotState::Idle || robot.battery >= robot.battery_capacity - 0.001) continue;
        if (std::none_of(pending.begin(), pending.end(), [&](const Order& order) { return feasible(robot, order); }))
            requestCharge(robot);
    }
}

void SimulationEngine::arrived(Robot& robot) {
    const auto state = robot.state == RobotState::Waiting ? robot.resume_state : robot.state;
    robot.path.clear();
    robot.path_index = 0;
    robot.destination.reset();
    robot.blocked_ticks = 0;
    if (state == RobotState::MovingToItem) setState(robot, RobotState::PickingItem);
    else if (state == RobotState::MovingToPacking) setState(robot, RobotState::DroppingItem);
    else if (state == RobotState::MovingToCharge) {
        setState(robot, RobotState::Charging);
        runtime_[static_cast<std::size_t>(robot.id)].battery_waiting = false;
    } else setState(robot, RobotState::Idle);
}

void SimulationEngine::forceFailure(int robot_id, Tick duration, bool permanent) {
    if (robot_id < 0 || static_cast<std::size_t>(robot_id) >= robots_.size())
        throw std::out_of_range("unknown robot ID");
    auto& robot = robots_[static_cast<std::size_t>(robot_id)];
    const bool newly_failed = robot.state != RobotState::Failed;
    releaseTask(robot);
    releaseCharger(robot);
    collision_.release(robot_id);
    setState(robot, RobotState::Failed);
    robot.failure_until = tick_ + std::max<Tick>(duration, 1);
    robot.permanent_failure = permanent;
    runtime_[static_cast<std::size_t>(robot_id)].battery_waiting = false;
    if (newly_failed) metrics_.failure();
}

void SimulationEngine::updateStates() {
    std::bernoulli_distribution fails(config_.failure_probability);
    std::bernoulli_distribution permanent(config_.permanent_failure_probability);
    for (auto& robot : robots_) {
        auto& runtime = runtime_[static_cast<std::size_t>(robot.id)];
        if (robot.state == RobotState::Failed) {
            if (!robot.permanent_failure && tick_ >= robot.failure_until) setState(robot, RobotState::Idle);
            else continue;
        }
        if (config_.failures_enabled && fails(failure_rng_)) {
            forceFailure(robot.id, config_.failure_duration, permanent(failure_rng_));
            continue;
        }
        ++robot.state_ticks;
        if (robot.state == RobotState::PickingItem || robot.state == RobotState::DroppingItem) {
            if (config_.battery_enabled) robot.battery = std::max(0.0, robot.battery - config_.action_energy);
            if (robot.state == RobotState::PickingItem && robot.state_ticks >= config_.pickup_ticks) {
                robot.task->carrying = true;
                plan(robot, robot.task->packing, RobotState::MovingToPacking);
            } else if (robot.state == RobotState::DroppingItem && robot.state_ticks >= config_.dropoff_ticks) {
                auto& order = orders_.at(static_cast<std::size_t>(robot.task->order_id));
                order.completed_at = tick_;
                metrics_.completed(static_cast<double>(tick_ - order.created_at) * config_.tick_seconds);
                releaseTask(robot);
                setState(robot, RobotState::Idle);
                park(robot);
            }
            continue;
        }
        if (robot.state == RobotState::Charging) {
            robot.battery = std::min(robot.battery_capacity, robot.battery + config_.charge_rate * config_.tick_seconds);
            if (robot.battery >= robot.battery_capacity - 0.000001) {
                releaseCharger(robot);
                setState(robot, RobotState::Idle);
                park(robot);
            }
            continue;
        }
        if (config_.battery_enabled && robot.state != RobotState::MovingToCharge &&
            !(robot.state == RobotState::Waiting && robot.resume_state == RobotState::MovingToCharge)) {
            const double safe_return = static_cast<double>(chargerDistance(robot.position)) * config_.move_energy +
                                       config_.battery_reserve + config_.move_energy;
            if (robot.task && robot.battery <= safe_return) {
                releaseTask(robot);
                setState(robot, RobotState::Idle);
            }
            if (!robot.task && (robot.state == RobotState::MovingToPark ||
                (robot.state == RobotState::Waiting && robot.resume_state == RobotState::MovingToPark)) && robot.battery <= safe_return) {
                robot.path.clear();
                robot.destination.reset();
                setState(robot, RobotState::Idle);
            }
            if (robot.state == RobotState::Idle && (runtime.battery_waiting ||
                robot.battery < robot.battery_capacity * config_.charge_threshold / 100.0 || robot.battery <= safe_return))
                requestCharge(robot);
        }
        if (robot.state == RobotState::Idle && warehouse_.at(robot.position) != CellType::Floor) park(robot);
    }
}

void SimulationEngine::moveRobots() {
    std::vector<Position> desired;
    desired.reserve(robots_.size());
    for (auto& robot : robots_) {
        const auto state = robot.state == RobotState::Waiting ? robot.resume_state : robot.state;
        auto& runtime = runtime_[static_cast<std::size_t>(robot.id)];
        if (moving(state) && robot.destination) {
            if (robot.position == *robot.destination) arrived(robot);
            else if (robot.path_index >= robot.path.size() && tick_ >= runtime.next_plan_tick) {
                ++robot.replans;
                metrics_.replan();
                plan(robot, *robot.destination, state, robot.blocked_ticks > 0);
            }
        }
        const auto current_state = robot.state == RobotState::Waiting ? robot.resume_state : robot.state;
        bool can_move = moving(current_state) && robot.path_index < robot.path.size() &&
                        (!config_.battery_enabled || robot.battery + 0.000001 >= config_.move_energy);
        if (can_move && config_.battery_enabled && current_state == RobotState::MovingToCharge && robot.destination) {
            const auto next = robot.path[robot.path_index];
            const double needed = static_cast<double>(distance(next, *robot.destination) + 1) * config_.move_energy;
            if (needed > robot.battery + 0.000001) {
                can_move = false;
                robot.path.clear();
                robot.path_index = 0;
                runtime.next_plan_tick = tick_ + 2;
                robot.state = RobotState::Waiting;
                robot.resume_state = RobotState::MovingToCharge;
            }
        }
        desired.push_back(can_move ? robot.path[robot.path_index] : robot.position);
    }
    // Last tick's forecast is refreshed because service, failure and route decisions
    // may have changed. The next simultaneous movement is reserved before commit.
    collision_.prune_future(tick_);
    const auto accepted = collision_.resolve(warehouse_, robots_, desired, tick_);
    collision_.prune(tick_);
    for (std::size_t i = 0; i < robots_.size(); ++i) {
        const auto next = accepted[i] ? desired[i] : robots_[i].position;
        if (!collision_.reserve(robots_[i].position, next, tick_, robots_[i].id))
            throw std::logic_error("collision resolver produced an inconsistent reservation");
    }
    for (std::size_t i = 0; i < robots_.size(); ++i) {
        auto& robot = robots_[i];
        const auto previous = robot.position;
        if (accepted[i]) {
            robot.position = desired[i];
            ++robot.path_index;
            ++robot.distance_travelled;
            ++traffic_[static_cast<std::size_t>(warehouse_.index(robot.position))];
            congestion_[static_cast<std::size_t>(warehouse_.index(robot.position))] += 0.12;
            if (config_.battery_enabled) robot.battery = std::max(0.0, robot.battery - config_.move_energy);
            metrics_.movement();
            robot.blocked_ticks = 0;
            if (runtime_[i].yielding) {
                runtime_[i].yielding = false;
                runtime_[i].next_plan_tick = tick_ + 3;
            }
            if (robot.state == RobotState::Waiting) setState(robot, robot.resume_state);
            if (robot.destination && robot.position == *robot.destination) arrived(robot);
        } else if (desired[i] != previous) {
            ++robot.blocked_ticks;
            if (robot.state != RobotState::Waiting) { robot.resume_state = robot.state; robot.state = RobotState::Waiting; }
            metrics_.collision();
            if (robot.blocked_ticks == 3) metrics_.congestion();
        }
    }
    // Persistent conflicts trigger occupancy-aware replanning; a stagger avoids
    // all robots rebuilding routes on the same tick.
    for (auto& robot : robots_) {
        if (robot.blocked_ticks >= 3 && robot.destination && (tick_ + static_cast<Tick>(robot.id)) % 4 == 0) {
            ++robot.replans;
            metrics_.replan();
            const auto state = robot.state == RobotState::Waiting ? robot.resume_state : robot.state;
            const bool found = plan(robot, *robot.destination, state, true);
            if (!found || robot.blocked_ticks >= 7) {
                // A one-cell sidestep and short hold opens a passing pocket. Merely
                // replanning to an occupied destination cannot break a docking queue.
                auto neighbors = warehouse_.neighbors(robot.position);
                const auto offset = static_cast<std::size_t>((tick_ / 4 + static_cast<Tick>(robot.id)) % std::max<std::size_t>(1, neighbors.size()));
                std::rotate(neighbors.begin(), neighbors.begin() + static_cast<std::ptrdiff_t>(offset), neighbors.end());
                for (const auto candidate : neighbors) {
                    if (candidate == *robot.destination || warehouse_.at(candidate) != CellType::Floor) continue;
                    if (std::any_of(robots_.begin(), robots_.end(), [&](const Robot& other) { return other.position == candidate; })) continue;
                    robot.path = {candidate};
                    robot.path_index = 0;
                    setState(robot, state);
                    runtime_[static_cast<std::size_t>(robot.id)].yielding = true;
                    break;
                }
            }
        }
    }
    // A resting robot yields when it occupies the next cell of a live route.
    for (std::size_t i = 0; i < robots_.size(); ++i) {
        if (desired[i] == robots_[i].position || accepted[i]) continue;
        for (auto& blocker : robots_) {
            if (blocker.position == desired[i] && blocker.state == RobotState::Idle &&
                (!config_.battery_enabled || blocker.battery > config_.move_energy * 3.0)) {
                park(blocker, robots_[i].position);
                break;
            }
        }
    }
    collision_.forecast(warehouse_, robots_, tick_ + 1);
}

void SimulationEngine::observe() {
    std::size_t working = 0, idle = 0, charging = 0, battery_waiting = 0;
    for (auto& robot : robots_) {
        if (robot.task && robot.state != RobotState::Failed) { ++working; ++robot.working_time; }
        if (robot.state == RobotState::Idle || (robot.state == RobotState::Waiting && !robot.task)) {
            ++idle; ++robot.idle_time;
        }
        if (robot.state == RobotState::Charging) { ++charging; ++robot.charging_time; }
        if (runtime_[static_cast<std::size_t>(robot.id)].battery_waiting) { ++battery_waiting; ++robot.battery_idle_time; }
    }
    metrics_.observe(robots_.size(), working, idle, charging, battery_waiting);
}

void SimulationEngine::tick() {
    ++tick_;
    if (orders_.size() < config_.max_orders && config_.order_rate > 0.0) {
        std::poisson_distribution<int> arrivals(config_.order_rate * config_.tick_seconds);
        const auto count = std::min<std::size_t>(static_cast<std::size_t>(arrivals(workload_rng_)), config_.max_orders - orders_.size());
        for (std::size_t i = 0; i < count; ++i) generateOrder();
    }
    for (auto& value : congestion_) value *= 0.96;
    updateStates();
    if (tick_ == 1 || tick_ % config_.scheduling_interval == 0) assignTasks();
    moveRobots();
    observe();
}

void SimulationEngine::step(std::size_t ticks) { for (std::size_t i = 0; i < ticks; ++i) tick(); }

MetricsSnapshot SimulationEngine::metrics() const {
    const auto failed = std::count_if(robots_.begin(), robots_.end(), [](const Robot& robot) { return robot.state == RobotState::Failed; });
    return metrics_.snapshot(tick_, config_.tick_seconds, orders_.size(), static_cast<std::size_t>(failed));
}

bool SimulationEngine::complete() const {
    return orders_.size() == config_.max_orders &&
        std::all_of(orders_.begin(), orders_.end(), [](const Order& order) { return order.completed_at.has_value(); });
}

} // namespace warehouse



