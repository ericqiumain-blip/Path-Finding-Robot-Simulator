cmake_minimum_required(VERSION 3.20)
project(Pathfinder VERSION 1.0.0 LANGUAGES CXX)

option(WAREHOUSE_BUILD_TESTS "Build the Catch2 unit and integration tests" ON)
option(WAREHOUSE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(warehouse_engine
    engine/src/types.cpp
    engine/src/warehouse.cpp
    engine/src/path_planner.cpp
    engine/src/collision_manager.cpp
    engine/src/scheduler.cpp
    engine/src/metrics.cpp
    engine/src/simulation.cpp)
target_include_directories(warehouse_engine PUBLIC engine/include)
if(MSVC)
    target_compile_options(warehouse_engine PUBLIC /W4 /permissive- /EHsc)
else()
    target_compile_options(warehouse_engine PUBLIC -Wall -Wextra -Wpedantic)
endif()
if(WAREHOUSE_SANITIZERS AND NOT MSVC)
    target_compile_options(warehouse_engine PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(warehouse_engine PUBLIC -fsanitize=address,undefined)
endif()

add_executable(warehouse_sim server/main.cpp server/serialization.cpp)
target_link_libraries(warehouse_sim PRIVATE warehouse_engine)
target_include_directories(warehouse_sim SYSTEM PRIVATE third_party)
target_include_directories(warehouse_sim PRIVATE server)
target_compile_definitions(warehouse_sim PRIVATE WAREHOUSE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

if(WAREHOUSE_BUILD_TESTS)
    enable_testing()
    add_executable(warehouse_tests tests/test_main.cpp tests/test_algorithms.cpp tests/test_simulation.cpp)
    target_link_libraries(warehouse_tests PRIVATE warehouse_engine)
    target_include_directories(warehouse_tests SYSTEM PRIVATE third_party)
    target_compile_definitions(warehouse_tests PRIVATE CATCH_CONFIG_NO_POSIX_SIGNALS WAREHOUSE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME warehouse_tests COMMAND warehouse_tests)
    set_tests_properties(warehouse_tests PROPERTIES TIMEOUT 180)
endif()
