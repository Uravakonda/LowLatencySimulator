cmake_minimum_required(VERSION 3.15)
project(LowLatencyOrderBook CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#This has been set to the architecture for my MacBook Pro using an M4 chip
set(CMAKE_OSX_ARCHITECTURES arm64)

#For concurrentqueue.h header file in 'include' folder
include_directories(include)
add_executable(order_book_sim main.cpp)

#THIS IS IMPORTANT: must build in Release mode for low-latency
set_target_properties(order_book_sim PROPERTIES
        COMPILE_FLAGS_RELEASE "-O3 -DNDEBUG"
)