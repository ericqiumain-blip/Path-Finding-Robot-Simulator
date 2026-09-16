#include <catch2/catch.hpp>
#include "warehouse/simulation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>

using namespace warehouse;
namespace {
Warehouse compact() { return Warehouse({"#############", "#C.........P#", "#...........#", "#..S...S....#", "#...........#", "#...........#", "#############"}); }
SimulationConfig workload(std::size_t robots=4,std::size_t orders=30) {
    SimulationConfig c; c.robot_count=robots; c.max_orders=orders; c.initial_orders=orders;
    c.order_rate=0; c.seed=42; return c;
}
void safe_tick(SimulationEngine& e) {
    std::vector<Position> before; for(const auto& r:e.robots()) before.push_back(r.position);
    e.tick(); std::set<int> positions;
    for(std::size_t i=0;i<e.robots().size();++i) {
        const auto& r=e.robots()[i];
        REQUIRE(e.warehouse().traversable(r.position));
        REQUIRE(positions.insert(e.warehouse().index(r.position)).second);
        REQUIRE(manhattan(before[i],r.position)<=1);
        REQUIRE(r.battery>=0);
        REQUIRE(r.battery<=r.battery_capacity);
        for(std::size_t j=0;j<i;++j) if(before[i]!=r.position)
            REQUIRE_FALSE((before[i]==e.robots()[j].position && before[j]==r.position));
    }
}
}

TEST_CASE("One robot executes the full pickup and delivery finite state machine", "[simulation]") {
    auto c=workload(1,8); SimulationEngine e(compact(),c);
    std::set<RobotState> seen;
    for(int i=0;i<1000&&!e.complete();++i) { e.tick(); seen.insert(e.robots()[0].state); }
    REQUIRE(e.complete());
    REQUIRE(seen.contains(RobotState::MovingToItem));
    REQUIRE(seen.contains(RobotState::PickingItem));
    REQUIRE(seen.contains(RobotState::MovingToPacking));
    REQUIRE(seen.contains(RobotState::DroppingItem));
    const auto m=e.metrics();
    REQUIRE(m.orders_generated==8); REQUIRE(m.orders_completed==8); REQUIRE(m.outstanding_orders==0);
    REQUIRE(m.orders_per_hour==Approx(8.0*3600.0/m.simulation_seconds));
    REQUIRE(m.total_distance>0); REQUIRE(m.pathfinding_calls>0); REQUIRE(m.nodes_explored>0);
    REQUIRE(m.avg_fulfillment_time>0); REQUIRE(m.p95_fulfillment_time>=m.median_fulfillment_time);
    REQUIRE(m.robot_utilization>0); REQUIRE(m.robot_utilization<=1);
    REQUIRE(std::accumulate(e.heatmap().begin(),e.heatmap().end(),std::uint64_t{})==static_cast<std::uint64_t>(m.total_distance));
    for(const auto& o:e.orders()) { REQUIRE(o.completed_at.has_value()); REQUIRE(*o.completed_at>=o.created_at); }
}

TEST_CASE("Multiple robots complete orders without vertex or edge collisions", "[integration]") {
    auto c=workload(8,80); c.battery_enabled=false;
    SimulationEngine e(Warehouse::for_fleet(8,42),c);
    for(int i=0;i<2500&&!e.complete();++i) safe_tick(e);
    REQUIRE(e.complete()); REQUIRE(e.metrics().orders_completed==80);
}

TEST_CASE("Every scheduler completes an identical compact workload", "[integration][scheduler]") {
    for(const std::string strategy:{"random","nearest","cost","hungarian"}) {
        INFO(strategy); auto c=workload(3,15); c.scheduler=strategy; c.battery_enabled=false;
        SimulationEngine e(compact(),c);
        for(int i=0;i<1600&&!e.complete();++i) safe_tick(e);
        REQUIRE(e.complete());
    }
}

TEST_CASE("Fixed seed reproduces trajectories and all non-wall-clock metrics", "[determinism]") {
    auto c=workload(8,500); c.initial_orders=10; c.order_rate=1.2;
    c.failures_enabled=true; c.failure_probability=0.001;
    SimulationEngine a(Warehouse::for_fleet(8,42),c),b(Warehouse::for_fleet(8,42),c);
    for(int t=0;t<250;++t) {
        a.tick(); b.tick();
        for(std::size_t i=0;i<a.robots().size();++i) {
            REQUIRE(a.robots()[i].position==b.robots()[i].position);
            REQUIRE(a.robots()[i].state==b.robots()[i].state);
            REQUIRE(a.robots()[i].battery==b.robots()[i].battery);
        }
    }
    REQUIRE(a.metrics().orders_completed==b.metrics().orders_completed);
    REQUIRE(a.metrics().collisions_prevented==b.metrics().collisions_prevented);
    REQUIRE(a.metrics().path_replans==b.metrics().path_replans);
    REQUIRE(a.metrics().failure_events==b.metrics().failure_events);
}

TEST_CASE("Scheduler randomness cannot change the generated benchmark workload", "[determinism]") {
    auto c=workload(6,1000); c.initial_orders=5; c.order_rate=0.7; c.scheduler="random";
    SimulationEngine a(compact(),c); c.scheduler="nearest"; SimulationEngine b(compact(),c);
    a.step(200); b.step(200); REQUIRE(a.orders().size()==b.orders().size());
    for(std::size_t i=0;i<a.orders().size();++i) {
        REQUIRE(a.orders()[i].id==b.orders()[i].id);
        REQUIRE(a.orders()[i].created_at==b.orders()[i].created_at);
        REQUIRE(a.orders()[i].shelf_id==b.orders()[i].shelf_id);
        REQUIRE(a.orders()[i].priority==b.orders()[i].priority);
    }
}

TEST_CASE("Robots recharge and continue working with limited charger capacity", "[battery]") {
    auto c=workload(3,60); c.battery_capacity=6; c.battery_reserve=0.2;
    c.move_energy=0.10; c.action_energy=0.01; c.charge_rate=1; c.charge_threshold=50;
    SimulationEngine e(compact(),c); bool charged=false;
    for(int t=0;t<3000&&!e.complete();++t) {
        safe_tick(e); int at_charger=0;
        for(const auto& r:e.robots()) if(r.state==RobotState::Charging) { ++at_charger; charged=true; }
        REQUIRE(at_charger<=1);
    }
    REQUIRE(charged); REQUIRE(e.metrics().robot_charging_time>0); REQUIRE(e.complete());
}

TEST_CASE("A robot rejects a task whose round trip exceeds its energy budget", "[battery]") {
    auto c=workload(1,1); c.battery_capacity=1; c.battery_reserve=0.1; c.move_energy=1;
    SimulationEngine e(compact(),c); e.step(30);
    REQUIRE_FALSE(e.robots()[0].task.has_value()); REQUIRE(e.metrics().orders_completed==0);
    REQUIRE(e.robots()[0].battery>=0);
}

TEST_CASE("Temporary failures release unfinished work and recover", "[failure]") {
    auto c=workload(3,20); c.battery_enabled=false; SimulationEngine e(compact(),c);
    for(int i=0;i<20&&!e.robots()[0].task;++i) e.tick();
    REQUIRE(e.robots()[0].task.has_value()); const auto order=e.robots()[0].task->order_id;
    const auto position=e.robots()[0].position;
    e.forceFailure(0,5); REQUIRE(e.robots()[0].state==RobotState::Failed);
    REQUIRE_FALSE(e.orders()[static_cast<std::size_t>(order)].assigned_robot.has_value());
    for(int i=0;i<4;++i) { safe_tick(e); REQUIRE(e.robots()[0].position==position); }
    e.step(3); REQUIRE(e.robots()[0].state!=RobotState::Failed);
    for(int i=0;i<2000&&!e.complete();++i) safe_tick(e);
    REQUIRE(e.complete()); REQUIRE(e.metrics().failure_events==1);
}

TEST_CASE("Permanent failed robots remain physical obstacles", "[failure]") {
    auto c=workload(4,20); c.battery_enabled=false; SimulationEngine e(compact(),c);
    e.step(3); e.forceFailure(0,10,true); const auto position=e.robots()[0].position;
    for(int i=0;i<400;++i) safe_tick(e);
    REQUIRE(e.robots()[0].state==RobotState::Failed); REQUIRE(e.robots()[0].position==position);
    REQUIRE(e.metrics().failed_robots==1); REQUIRE(e.metrics().failure_events==1);
    REQUIRE(e.metrics().orders_completed>0);
}

TEST_CASE("Metrics compute exact means, quantiles, and accumulated times", "[metrics]") {
    MetricsCollector m; m.completed(10); m.completed(20); m.completed(30); m.completed(40);
    m.path(2,12); m.path(4,18); m.movement(); m.collision(2); m.replan(); m.failure();
    m.observe(4,2,1,1,0); const auto s=m.snapshot(1,2,6,1);
    REQUIRE(s.avg_fulfillment_time==25); REQUIRE(s.median_fulfillment_time==25);
    REQUIRE(s.p95_fulfillment_time>=30); REQUIRE(s.p95_fulfillment_time<=40);
    REQUIRE(s.robot_utilization==0.5); REQUIRE(s.robot_idle_time==2); REQUIRE(s.robot_charging_time==2);
    REQUIRE(s.avg_pathfinding_ms==3); REQUIRE(s.nodes_explored==30);
    REQUIRE(s.outstanding_orders==2); REQUIRE(s.collisions_prevented==2); REQUIRE(s.failed_robots==1);
}

TEST_CASE("Invalid configuration and impossible spawn density fail clearly", "[config]") {
    auto c=workload(); c.robot_count=0; REQUIRE_THROWS_AS(c.validate(),std::invalid_argument);
    c=workload(); c.order_rate=std::numeric_limits<double>::quiet_NaN(); REQUIRE_THROWS_AS(c.validate(),std::invalid_argument);
    c=workload(); c.failure_probability=1.1; REQUIRE_THROWS_AS(c.validate(),std::invalid_argument);
    c=workload(); c.robot_count=100; REQUIRE_THROWS_AS(SimulationEngine(compact(),c),std::invalid_argument);
    c=workload(1,0); SimulationEngine e(compact(),c); REQUIRE(e.complete()); e.step(10); REQUIRE(e.orders().empty());
}

TEST_CASE("Five hundred robots safely execute simultaneous deterministic ticks", "[scale]") {
    auto c=workload(500,1500); c.initial_orders=200; c.order_rate=10;
    SimulationEngine e(Warehouse::for_fleet(500,42),c);
    for(int i=0;i<100;++i) {
        e.tick(); std::set<int> occupied;
        for(const auto& r:e.robots()) REQUIRE(occupied.insert(e.warehouse().index(r.position)).second);
    }
    REQUIRE(e.robots().size()==500); REQUIRE(e.metrics().orders_completed>0);
}
