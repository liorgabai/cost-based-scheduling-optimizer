cmake_minimum_required(VERSION 3.20)
project(SchedulingOptimizer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(scheduling_optimizer
    src/main.c
    src/parser.c
    src/hungarian.c
    src/min_cost_flow.c
    src/engine.c
)

target_include_directories(scheduling_optimizer PRIVATE include)

add_executable(severity_calibrator
    calibration/adaptive_severity_policy_calibrator.c
)

if (NOT MSVC)
    target_link_libraries(severity_calibrator PRIVATE m)
endif()

if (MSVC)
    target_compile_options(scheduling_optimizer PRIVATE /W4)
    target_compile_options(severity_calibrator PRIVATE /W4)
else()
    target_compile_options(scheduling_optimizer PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(severity_calibrator PRIVATE -Wall -Wextra -Wpedantic)
endif()
