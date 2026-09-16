#include <catch2/catch.hpp>
#include "warehouse/collision_manager.hpp"
#include "warehouse/path_planner.hpp"
#include "warehouse/scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <limits>
#include <set>
#include <unordered_set>

#ifndef WAREHOUSE_SOURCE_DIR
#define WAREHOUSE_SOURCE_DIR "."
#endif

using namespace warehouse;
namespace {
std::vector<Robot> fleet(std::initializer_list<Position> positions) {
    std::vector<Robot> robots;
    for (const auto position : positions) {
        Robot robot;
        robot.id = static_cast<int>(robots.size());
        robot.position = position;
        robots.push_back(robot);
    }
    return robots;
}
const double infinity = std::numeric_limits<double>::infinity();
}

TEST_CASE("A* returns optimal valid routes around obstacles", "[astar]") {
    Warehouse map({".......", ".###.#.", "...#...", ".#...#.", "......."});
    AStarPlanner planner;
    for (const Position goal : {Position{6,4}, Position{0,0}, Position{4,2}}) {
        const auto oracle = map.distance_field(goal);
        for (const auto start : map.floor_positions()) {
            const auto route = planner.plan(map, start, goal);
            REQUIRE(route.found);
            REQUIRE(route.path.size() == static_cast<std::size_t>(oracle[map.index(start)]));
            REQUIRE(route.cost == Approx(static_cast<double>(route.path.size())));
            auto previous = start;
            for (const auto step : route.path) {
                REQUIRE(map.traversable(step));
                REQUIRE(manhattan(previous, step) == 1);
                previous = step;
            }
            REQUIRE(previous == goal);
            REQUIRE(route.computation_ms >= 0.0);
        }
    }
}
TEST_CASE("A* handles empty routes and unreachable or invalid endpoints", "[astar]") {
    Warehouse map({"..#..", "..#..", "..#.."});
    AStarPlanner planner;
    const auto stationary = planner.plan(map,{0,0},{0,0});
    REQUIRE(stationary.found);
    REQUIRE(stationary.path.empty());
    REQUIRE(stationary.cost == 0.0);
    REQUIRE_FALSE(planner.plan(map,{0,0},{4,2}).found);
    REQUIRE_FALSE(planner.plan(map,{0,0},{2,0}).found);
    REQUIRE_FALSE(planner.plan(map,{-1,0},{1,0}).found);
}
TEST_CASE("A* avoids dynamic blocked cells and takes a weighted congestion detour", "[astar]") {
    Warehouse map({".....", ".....", "....."});
    AStarPlanner planner;
    std::unordered_set<int> blocked{map.index({2,1})};
    auto route = planner.plan(map,{0,1},{4,1},{nullptr,0.0,&blocked});
    REQUIRE(route.found);
    REQUIRE(route.path.size() == 6);
    for (const auto cell : route.path) REQUIRE_FALSE(blocked.contains(map.index(cell)));
    blocked.insert(map.index({4,1}));
    REQUIRE_FALSE(planner.plan(map,{0,1},{4,1},{nullptr,0.0,&blocked}).found);
    std::vector<double> traffic(static_cast<std::size_t>(map.cell_count()));
    for (int x=1;x<=3;++x) traffic[map.index({x,1})] = 10.0;
    route = planner.plan(map,{0,1},{4,1},{&traffic,1.0,nullptr});
    REQUIRE(route.found);
    REQUIRE(route.path.size() == 6);
    REQUIRE(route.cost == Approx(6.0));
    REQUIRE_THROWS_AS(planner.plan(map,{0,1},{4,1},{&traffic,-1.0,nullptr}), std::invalid_argument);
    traffic.pop_back();
    REQUIRE_THROWS_AS(planner.plan(map,{0,1},{4,1},{&traffic,1.0,nullptr}), std::invalid_argument);
}
TEST_CASE("Warehouse validates ASCII maps and exposes shelf access", "[warehouse]") {
    REQUIRE_THROWS_AS(Warehouse(std::vector<std::string>{}), std::invalid_argument);
    REQUIRE_THROWS_AS(Warehouse({"...", ".."}), std::invalid_argument);
    REQUIRE_THROWS_AS(Warehouse({".?"}), std::invalid_argument);
    REQUIRE_THROWS_AS(Warehouse({"SS", "SS"}), std::invalid_argument);
    Warehouse map({"#####", "#S.P#", "#C..#", "#####"});
    REQUIRE(map.shelves().size() == 1);
    REQUIRE(map.traversable(map.shelves().front().pickup));
    REQUIRE(manhattan(map.shelves().front().position, map.shelves().front().pickup) == 1);
    REQUIRE(map.packing_stations().size() == 1);
    REQUIRE(map.charging_stations().size() == 1);
    REQUIRE(map.at({1,1}) == CellType::Shelf);
    REQUIRE_FALSE(map.traversable({1,1}));
    REQUIRE_THROWS_AS(map.at({-1,1}), std::out_of_range);
    REQUIRE((map.nearest_packing({2,2}) == Position{3,1}));
    REQUIRE((map.nearest_charging({2,2}) == Position{1,2}));
}
TEST_CASE("Example and large procedural maps have reachable facilities", "[warehouse]") {
    for (const auto name : {"small", "medium", "large", "congested"}) {
        INFO(name);
        const auto path = std::filesystem::path(WAREHOUSE_SOURCE_DIR) / "configs" / (std::string(name)+".map");
        const auto map = Warehouse::load(path);
        const auto reachable = map.distance_field(map.floor_positions().front());
        REQUIRE_FALSE(map.shelves().empty());
        REQUIRE_FALSE(map.packing_stations().empty());
        REQUIRE_FALSE(map.charging_stations().empty());
        for (const auto& shelf : map.shelves()) REQUIRE(reachable[map.index(shelf.pickup)] >= 0);
        for (const auto& station : map.packing_stations()) REQUIRE(reachable[map.index(station.position)] >= 0);
        for (const auto& station : map.charging_stations()) REQUIRE(reachable[map.index(station.position)] >= 0);
    }
    const auto large = Warehouse::for_fleet(1000,42);
    REQUIRE(large.floor_positions().size() > 1000);
    REQUIRE(large.rows() == Warehouse::for_fleet(1000,42).rows());
    REQUIRE_THROWS_AS(Warehouse::procedural(4,4), std::invalid_argument);
}
TEST_CASE("Collision arbitration prevents vertex conflicts with rotating priority", "[collision]") {
    Warehouse map({"...", "...", "..."});
    CollisionManager collisions(map.width());
    const auto robots = fleet({{0,1},{2,1}});
    REQUIRE((collisions.resolve(map,robots,{{1,1},{1,1}},0) == std::vector<bool>{true,false}));
    REQUIRE((collisions.resolve(map,robots,{{1,1},{1,1}},1) == std::vector<bool>{false,true}));
    REQUIRE_THROWS_AS(collisions.resolve(map,robots,{{1,1}},0), std::invalid_argument);
}
TEST_CASE("Collision arbitration allows following chains and blocks swaps or stopped leaders", "[collision]") {
    Warehouse map({".....", "....."});
    CollisionManager collisions(map.width());
    const auto robots = fleet({{0,0},{1,0},{2,0}});
    REQUIRE((collisions.resolve(map,robots,{{1,0},{2,0},{3,0}},0) == std::vector<bool>{true,true,true}));
    REQUIRE((collisions.resolve(map,robots,{{1,0},{2,0},{2,0}},0) == std::vector<bool>{false,false,false}));
    REQUIRE((collisions.resolve(map,robots,{{1,0},{0,0},{2,0}},0) == std::vector<bool>{false,false,false}));
    REQUIRE((collisions.resolve(map,robots,{{0,1},{1,1},{4,0}},0) == std::vector<bool>{true,true,false}));
}
TEST_CASE("Collision arbitration permits safe four-way rotations", "[collision]") {
    Warehouse map({"..", ".."});
    CollisionManager collisions(map.width());
    const auto robots = fleet({{0,0},{1,0},{1,1},{0,1}});
    REQUIRE((collisions.resolve(map,robots,{{1,0},{1,1},{0,1},{0,0}},0) == std::vector<bool>{true,true,true,true}));
}
TEST_CASE("Reservations reject duplicate cells and reverse edges while allowing following", "[reservation]") {
    CollisionManager collisions(10);
    REQUIRE(collisions.reserve({0,0},{1,0},5,10));
    REQUIRE_FALSE(collisions.reserve({2,0},{1,0},5,11));
    REQUIRE_FALSE(collisions.reserve({1,0},{0,0},5,11));
    REQUIRE(collisions.reserve({0,1},{0,0},5,11));
    REQUIRE(collisions.available({2,0},{1,0},6,11));
    REQUIRE_FALSE(collisions.reserve({0,0},{3,0},5,12));
    REQUIRE(collisions.reserve({0,0},{1,0},5,10));
}
TEST_CASE("Reservation horizon stops at conflicts and supports release and pruning", "[reservation]") {
    CollisionManager collisions(10);
    const std::vector<Position> path{{1,0},{2,0},{3,0},{4,0}};
    REQUIRE(collisions.reserve_path({0,0},path,10,1,2) == 2);
    REQUIRE(collisions.reservations(0,100).size() == 2);
    collisions.clear();
    REQUIRE(collisions.reserve({4,0},{3,0},12,2));
    REQUIRE(collisions.reserve_path({0,0},path,10,1,4) == 2);
    collisions.release(2);
    REQUIRE(collisions.available({2,0},{3,0},12,1));
    collisions.prune(11);
    REQUIRE(collisions.reservations(0,100).size() == 1);
    collisions.prune_future(11);
    REQUIRE(collisions.reservations(0,100).empty());
    REQUIRE(collisions.reserve_path({1,0},path,20,3,2,1) == 2);
    REQUIRE((collisions.reservations(20,20).front().position == Position{2,0}));
}
TEST_CASE("Future forecast reserves collision-free moving and stationary cells", "[reservation]") {
    Warehouse map({"......", "......"});
    auto robots = fleet({{0,0},{1,0},{5,1}});
    robots[0].state = robots[1].state = RobotState::MovingToItem;
    robots[0].path = {{1,0},{2,0},{3,0}};
    robots[1].path = {{2,0},{3,0},{4,0}};
    CollisionManager collisions(map.width());
    collisions.forecast(map,robots,4,4);
    const auto reservations = collisions.reservations(4,7);
    REQUIRE(reservations.size() == 12);
    for (Tick tick=4;tick<=7;++tick) {
        std::set<int> occupied;
        for (const auto& r : collisions.reservations(tick,tick)) REQUIRE(occupied.insert(map.index(r.position)).second);
    }
    REQUIRE((robots[0].position == Position{0,0}));
    REQUIRE((collisions.reservations(4,4).front().position == Position{1,0}));
    collisions.prune_future(4);
    REQUIRE(collisions.reservations(4,100).empty());
}
TEST_CASE("All schedulers share eligibility, feasibility and priority behavior", "[scheduler]") {
    Warehouse map({"##########", "#........#", "#....S...#", "#P......C#", "##########"});
    AStarPlanner planner;
    std::vector<Order> orders(2);
    orders[0].id=0; orders[0].shelf_id=0; orders[0].priority=1;
    orders[1].id=1; orders[1].shelf_id=0; orders[1].priority=3;
    for (const auto strategy : {SchedulingStrategy::Random,SchedulingStrategy::Nearest,SchedulingStrategy::Cost,SchedulingStrategy::Hungarian}) {
        auto robots = fleet({{1,1},{7,2}});
        robots[0].state = RobotState::Charging;
        std::mt19937 rng(42);
        auto scheduler = make_scheduler(strategy);
        const auto assignments = scheduler->assign(map,robots,orders,planner,rng);
        REQUIRE(assignments.size() == 1);
        REQUIRE(assignments[0].robot_id == 1);
        REQUIRE(assignments[0].order_id == 1);
        REQUIRE(assignments[0].cost == 1.0);
        REQUIRE(scheduler->assign(map,robots,orders,planner,rng,[](const Robot&,const Order&,double){return false;}).empty());
        orders[1].assigned_robot = 1;
        const auto remaining = scheduler->assign(map,robots,orders,planner,rng);
        REQUIRE(remaining.size() == 1);
        REQUIRE(remaining[0].order_id == 0);
        orders[1].assigned_robot.reset();
    }
}
TEST_CASE("Cost scheduler measures a route that Manhattan nearest cannot distinguish", "[scheduler]") {
    Warehouse map({"###########", "#..#......#", "#..#..S...#", "#..#......#", "#.........#", "###########"});
    auto robots = fleet({{2,2},{1,4}});
    std::vector<Order> orders(1);
    AStarPlanner planner;
    std::mt19937 rng(42);
    auto nearest = make_scheduler(SchedulingStrategy::Nearest);
    auto cost = make_scheduler(SchedulingStrategy::Cost);
    REQUIRE(nearest->assign(map,robots,orders,planner,rng).front().robot_id == 0);
    const auto actual = cost->assign(map,robots,orders,planner,rng);
    REQUIRE(actual.front().robot_id == 1);
    REQUIRE(actual.front().cost == 8.0);
}
TEST_CASE("Random scheduler is reproducible with a fixed seed", "[scheduler]") {
    Warehouse map({"........", "...S....", "........"});
    auto robots = fleet({{0,0},{1,0},{2,0},{3,0},{4,0},{5,0}});
    std::vector<Order> orders(4);
    for (std::size_t i=0;i<orders.size();++i) orders[i].id=static_cast<int>(i);
    AStarPlanner planner;
    std::mt19937 a(817), b(817);
    auto first = make_scheduler(SchedulingStrategy::Random)->assign(map,robots,orders,planner,a);
    auto second = make_scheduler(SchedulingStrategy::Random)->assign(map,robots,orders,planner,b);
    REQUIRE(first.size() == second.size());
    for (std::size_t i=0;i<first.size();++i) {
        REQUIRE(first[i].robot_id == second[i].robot_id);
        REQUIRE(first[i].order_id == second[i].order_id);
    }
}
TEST_CASE("Hungarian finds rectangular optimum and leaves forbidden rows unmatched", "[hungarian]") {
    REQUIRE((hungarian_assignment({{4,1,3},{2,0,5},{3,2,2}}) == std::vector<int>{1,0,2}));
    REQUIRE((hungarian_assignment({{infinity,infinity},{4,2},{1,8}}) == std::vector<int>{-1,1,0}));
    REQUIRE((hungarian_assignment({{2},{1}}) == std::vector<int>{-1,0}));
    REQUIRE((hungarian_assignment({{}, {}}) == std::vector<int>{-1,-1}));
    REQUIRE(hungarian_assignment({}).empty());
    REQUIRE_THROWS_AS(hungarian_assignment({{1,2},{3}}), std::invalid_argument);
    REQUIRE_THROWS_AS(hungarian_assignment({{std::numeric_limits<double>::quiet_NaN()}}), std::invalid_argument);
}
TEST_CASE("Hungarian agrees with exhaustive small assignment search", "[hungarian]") {
    std::mt19937 rng(52);
    for (int sample=0;sample<40;++sample) {
        std::vector<std::vector<double>> costs(3,std::vector<double>(4));
        for (auto& row : costs) for (auto& value : row)
            value = rng()%4 == 0 ? infinity : static_cast<double>(static_cast<int>(rng()%21)-5);
        int best_count=-1;
        double best_cost=infinity;
        std::function<void(std::size_t,unsigned,int,double)> enumerate = [&](std::size_t row,unsigned used,int count,double cost) {
            if (row == costs.size()) {
                if (count>best_count || (count==best_count && cost<best_cost)) {best_count=count;best_cost=cost;}
                return;
            }
            enumerate(row+1,used,count,cost);
            for (unsigned column=0;column<4;++column)
                if (!(used & (1U<<column)) && std::isfinite(costs[row][column]))
                    enumerate(row+1,used|(1U<<column),count+1,cost+costs[row][column]);
        };
        enumerate(0,0,0,0.0);
        const auto result = hungarian_assignment(costs);
        std::set<int> columns;
        double total=0.0;
        for (std::size_t row=0;row<result.size();++row) if (result[row]>=0) {
            REQUIRE(columns.insert(result[row]).second);
            REQUIRE(std::isfinite(costs[row][static_cast<std::size_t>(result[row])]));
            total += costs[row][static_cast<std::size_t>(result[row])];
        }
        REQUIRE(columns.size() == static_cast<std::size_t>(best_count));
        REQUIRE(total == Approx(best_cost));
    }
}


