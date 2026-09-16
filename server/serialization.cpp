#include "serialization.hpp"
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <stdexcept>

namespace warehouse::api {
Json metrics(const MetricsSnapshot& m) {
    return {{"ordersGenerated",m.orders_generated},{"ordersCompleted",m.orders_completed},
        {"ordersPerHour",m.orders_per_hour},{"avgFulfillment",m.avg_fulfillment_time},
        {"medianFulfillment",m.median_fulfillment_time},{"p95Fulfillment",m.p95_fulfillment_time},
        {"utilization",m.robot_utilization},{"totalDistance",m.total_distance},
        {"avgDistancePerOrder",m.avg_distance_per_order},{"idleTime",m.robot_idle_time},
        {"chargingTime",m.robot_charging_time},{"batteryWaitTime",m.battery_wait_time},
        {"collisionsPrevented",m.collisions_prevented},{"replans",m.path_replans},
        {"avgPathfindingMs",m.avg_pathfinding_ms},{"pathfindingCalls",m.pathfinding_calls},
        {"nodesExplored",m.nodes_explored},{"congestionEvents",m.congestion_events},
        {"failedRobots",m.failed_robots},{"failureEvents",m.failure_events},
        {"outstandingOrders",m.outstanding_orders},{"simulationSeconds",m.simulation_seconds}};
}
Json config(const SimulationConfig& c, const std::string& layout) {
    return {{"robots",c.robot_count},{"orderRate",c.order_rate},{"seed",c.seed},
        {"scheduler",c.scheduler},{"layout",layout},{"maxOrders",c.max_orders},
        {"initialOrders",c.initial_orders},{"tickSeconds",c.tick_seconds},
        {"batteryEnabled",c.battery_enabled},{"batteryCapacity",c.battery_capacity},
        {"moveEnergy",c.move_energy},{"actionEnergy",c.action_energy},{"chargeRate",c.charge_rate},
        {"chargeThreshold",c.charge_threshold},{"batteryReserve",c.battery_reserve},
        {"failuresEnabled",c.failures_enabled},{"failureProbability",c.failure_probability},
        {"failureDuration",c.failure_duration},{"permanentFailureProbability",c.permanent_failure_probability},
        {"congestionWeight",c.congestion_weight},{"schedulingInterval",c.scheduling_interval},
        {"batchSize",c.batch_size}};
}
Json state(const SimulationEngine& e, const std::string& layout) {
    Json robots=Json::array(), orders=Json::array(), reservations=Json::array();
    for(const auto& r:e.robots()) {
        Json path=Json::array();
        for(std::size_t i=r.path_index;i<r.path.size();++i) path.push_back({{"x",r.path[i].x},{"y",r.path[i].y}});
        robots.push_back({{"id",r.id},{"x",r.position.x},{"y",r.position.y},
            {"state",to_string(r.state)},{"battery",100.0*r.battery/r.battery_capacity},
            {"taskId",r.task?Json(r.task->order_id):Json(nullptr)},
            {"destination",r.destination?Json{{"x",r.destination->x},{"y",r.destination->y}}:Json(nullptr)},
            {"path",std::move(path)},{"distance",r.distance_travelled},{"replans",r.replans},
            {"idleTicks",r.idle_time},{"chargingTicks",r.charging_time}});
    }
    // Prefer active orders, then recent completions; the counters always include all orders.
    auto add_order=[&](const Order& o) {
        orders.push_back({{"id",o.id},{"shelfId",o.shelf_id},{"priority",o.priority},
            {"createdTick",o.created_at},{"assignedRobot",o.assigned_robot?Json(*o.assigned_robot):Json(nullptr)},
            {"status",o.completed_at?"completed":(o.assigned_robot?"assigned":"pending")},
            {"completedTick",o.completed_at?Json(*o.completed_at):Json(nullptr)}});
    };
    for(const auto& o:e.orders()) { if(!o.completed_at && orders.size()<100) add_order(o); }
    for(auto it=e.orders().rbegin();it!=e.orders().rend()&&orders.size()<100;++it) if(it->completed_at) add_order(*it);
    for(const auto& r:e.collisionManager().reservations(e.currentTick()+1,e.currentTick()+8))
        reservations.push_back({{"x",r.position.x},{"y",r.position.y},{"tick",r.time},{"robotId",r.robot_id}});
    return {{"tick",e.currentTick()},{"config",config(e.config(),layout)},
        {"warehouse",{{"width",e.warehouse().width()},{"height",e.warehouse().height()},{"cells",e.warehouse().rows()},{"name",layout}}},
        {"robots",std::move(robots)},{"orders",std::move(orders)},{"metrics",metrics(e.metrics())},
        {"heatmap",e.heatmap()},{"reservations",std::move(reservations)}};
}
void configure(SimulationConfig& c,std::string& layout,const Json& values) {
    if(!values.is_object()) throw std::invalid_argument("config must be an object");
    for(auto it=values.begin();it!=values.end();++it) {
        const auto& k=it.key(); const auto& v=it.value();
        if(k=="robots") { const auto n=v.get<int>(); if(n<1||n>1000) throw std::invalid_argument("robots must be 1..1000"); c.robot_count=static_cast<std::size_t>(n); }
        else if(k=="orderRate") c.order_rate=v.get<double>();
        else if(k=="seed") c.seed=v.get<std::uint64_t>();
        else if(k=="scheduler") c.scheduler=v.get<std::string>();
        else if(k=="layout") layout=v.get<std::string>();
        else if(k=="failuresEnabled") c.failures_enabled=v.get<bool>();
        else if(k=="failureProbability") c.failure_probability=v.get<double>();
        else throw std::invalid_argument("Unknown config option: "+k);
    }
    c.validate();
}
Warehouse load_layout(const std::string& name,const SimulationConfig& c) {
    if(name=="procedural") return Warehouse::for_fleet(static_cast<int>(c.robot_count),static_cast<std::uint32_t>(c.seed));
    auto path=std::filesystem::path(name);
    if(name=="small"||name=="medium"||name=="large"||name=="congested") path=std::filesystem::path(WAREHOUSE_SOURCE_DIR)/"configs"/(name+".map");
    return Warehouse::load(path);
}
std::string csv(const Json& document) {
    Json flat=Json::object();
    for(auto it=document.begin();it!=document.end();++it) {
        if(it.value().is_object()) for(auto v=it.value().begin();v!=it.value().end();++v) flat[v.key()]=v.value();
        else flat[it.key()]=it.value();
    }
    auto escaped=[](std::string s) { std::string result="\""; for(char c:s) { if(c=='\"') result+='\"'; result+=c; } return result+'\"'; };
    std::ostringstream out; bool first=true;
    for(auto it=flat.begin();it!=flat.end();++it) { if(!first) out<<','; first=false; out<<escaped(it.key()); }
    out<<'\n'; first=true;
    for(const auto& v:flat) { if(!first) out<<','; first=false; out<<(v.is_string()?escaped(v.get<std::string>()):v.dump()); }
    return out.str()+"\n";
}
} // namespace warehouse::api
