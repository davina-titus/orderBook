cmake_minimum_required(VERSION 3.16)
project(OrderBook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(orderbook src/OrderBook.cpp src/Types.cpp)
target_include_directories(orderbook PUBLIC include)

add_executable(replay apps/replay.cpp)
target_link_libraries(replay PRIVATE orderbook)

add_executable(bench bench/bench.cpp)
target_link_libraries(bench PRIVATE orderbook)

enable_testing()
add_executable(tests tests/test_orderbook.cpp)
target_link_libraries(tests PRIVATE orderbook)
add_test(NAME orderbook_tests COMMAND tests)
