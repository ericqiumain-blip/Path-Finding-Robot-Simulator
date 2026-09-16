#include "serialization.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

using namespace warehouse;
namespace {
std::uint64_t integer(const std::string& value) {
    if(value.empty() || value.front()=='-') throw std::invalid_argument("Expected a nonnegative integer: "+value);
    std::size_t parsed{}; const auto result=std::stoull(value,&parsed);
    if(parsed!=value.size()) throw std::invalid_argument("Invalid integer: "+value);
    return result;
}
double number(const std::string& value) {
    std::size_t parsed{}; const auto result=std::stod(value,&parsed);
    if(parsed!=value.size()) throw std::invalid_argument("Invalid number: "+value);
    return result;
}
void save(const std::string& path,const std::string& data) {
    std::ofstream out(path);
    if(!out) throw std::runtime_error("Cannot write "+path);
    out<<data;
    if(!out) throw std::runtime_error("Write failed: "+path);
}
void help() {
    std::cout<<R"(Pathfinder | Autonomous Multi-Robot Warehouse Simulator

warehouse_sim [options]
  --robots N                  Fleet size (default 32)
  --orders N                  Total order cap (default 10000)
  --initial-orders N          Orders at tick zero (default 20)
  --order-rate N              Poisson arrivals / simulated second (default 1.5)
  --scheduler NAME            random | nearest | cost | hungarian
  --seed N                    Reproducible 32-bit random seed (default 42)
  --layout NAME|FILE          small | medium | large | congested | procedural
  --ticks N                   Simulation tick limit (default 3600)
  --until-complete            Stop early if all configured orders complete
  --tick-seconds N            Simulated seconds per tick (default 1)
  --congestion-weight N       Routing penalty (default 1.5)
  --battery-capacity N        Energy capacity (default 100)
  --move-energy N             Energy per grid move (default 0.08)
  --charge-rate N             Energy gained / simulated second (default 2.5)
  --no-battery                Disable energy constraints
  --failures                 Enable random robot failures
  --failure-probability N     Failure probability per robot per tick
  --failure-duration N        Temporary failure ticks (default 30)
  --permanent-failure-probability N  Fraction of failures that are permanent
  --json FILE                Export metrics/config and wall timing as JSON
  --csv FILE                 Export one flat CSV result row
  --interactive              Read NDJSON commands; emit state on stdout
  --help                     Show this help

Without --interactive, stdout is one JSON benchmark document.
Tick duration does not depend on wall time. Layout defaults to procedural.
)";
}
}

int main(int argc,char** argv) {
    try {
        SimulationConfig cfg; cfg.max_orders=10000;
        std::string layout="procedural",json_path,csv_path;
        std::uint64_t ticks=3600; bool interactive=false,until_complete=false;
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            auto next=[&]() { if(i+1>=argc) throw std::invalid_argument("Missing value for "+arg); return std::string(argv[++i]); };
            if(arg=="--help"||arg=="-h") { help(); return 0; }
            if(arg=="--robots") cfg.robot_count=integer(next());
            else if(arg=="--orders") cfg.max_orders=integer(next());
            else if(arg=="--initial-orders") cfg.initial_orders=integer(next());
            else if(arg=="--order-rate") cfg.order_rate=number(next());
            else if(arg=="--scheduler") cfg.scheduler=next();
            else if(arg=="--seed") { cfg.seed=integer(next()); if(cfg.seed>4294967295ULL) throw std::invalid_argument("seed must be a 32-bit unsigned integer"); }
            else if(arg=="--layout") layout=next();
            else if(arg=="--ticks") ticks=integer(next());
            else if(arg=="--tick-seconds") cfg.tick_seconds=number(next());
            else if(arg=="--congestion-weight") cfg.congestion_weight=number(next());
            else if(arg=="--battery-capacity") cfg.battery_capacity=number(next());
            else if(arg=="--move-energy") cfg.move_energy=number(next());
            else if(arg=="--charge-rate") cfg.charge_rate=number(next());
            else if(arg=="--failure-probability") { cfg.failure_probability=number(next()); cfg.failures_enabled=true; }
            else if(arg=="--failure-duration") cfg.failure_duration=integer(next());
            else if(arg=="--permanent-failure-probability") { cfg.permanent_failure_probability=number(next()); cfg.failures_enabled=true; }
            else if(arg=="--json") json_path=next();
            else if(arg=="--csv") csv_path=next();
            else if(arg=="--no-battery") cfg.battery_enabled=false;
            else if(arg=="--failures") cfg.failures_enabled=true;
            else if(arg=="--interactive") interactive=true;
            else if(arg=="--until-complete") until_complete=true;
            else throw std::invalid_argument("Unknown option: "+arg);
        }
        cfg.validate();
        auto engine=std::make_unique<SimulationEngine>(api::load_layout(layout,cfg),cfg);
        if(interactive) {
            std::cout<<api::state(*engine,layout).dump()<<std::endl;
            std::string line;
            while(std::getline(std::cin,line)) {
                try {
                    if(line.size()>16384) throw std::invalid_argument("Command too large");
                    const auto command=api::Json::parse(line);
                    const auto action=command.at("action").get<std::string>();
                    if(action=="step") {
                        const auto count=command.value("ticks",1);
                        if(count<1||count>1000) throw std::invalid_argument("step ticks must be 1..1000");
                        engine->step(static_cast<std::size_t>(count));
                    } else if(action=="restart") {
                        auto next_config=cfg; auto next_layout=layout;
                        api::configure(next_config,next_layout,command.value("config",api::Json::object()));
                        auto replacement=std::make_unique<SimulationEngine>(api::load_layout(next_layout,next_config),next_config);
                        engine=std::move(replacement); cfg=std::move(next_config); layout=std::move(next_layout);
                    } else if(action=="fail") engine->forceFailure(command.at("robotId").get<int>(),command.value("duration",30),command.value("permanent",false));
                    else if(action!="snapshot") throw std::invalid_argument("Unknown action: "+action);
                    std::cout<<api::state(*engine,layout).dump()<<std::endl;
                } catch(const std::exception& error) { std::cout<<api::Json{{"error",error.what()}}.dump()<<std::endl; }
            }
            return 0;
        }
        const auto start=std::chrono::steady_clock::now();
        for(std::uint64_t t=0;t<ticks;++t) { if(until_complete&&engine->complete()) break; engine->tick(); }
        const auto wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        const api::Json result={{"config",api::config(cfg,layout)},{"metrics",api::metrics(engine->metrics())},
            {"ticks",engine->currentTick()},{"wallSeconds",wall},
            {"simulatedTicksPerSecond",wall>0?static_cast<double>(engine->currentTick())/wall:0.0},
            {"completedWorkload",engine->complete()}};
        if(!json_path.empty()) save(json_path,result.dump(2)+"\n");
        if(!csv_path.empty()) save(csv_path,api::csv(result));
        std::cout<<result.dump(2)<<'\n';
    } catch(const std::exception& error) { std::cerr<<"warehouse_sim: "<<error.what()<<'\n'; return 1; }
}
