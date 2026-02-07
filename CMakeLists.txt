cmake_minimum_required(VERSION 3.10)

project(ReactUIApp CXX)

include_directories(include)

add_executable(main src/main.cpp)

target_link_libraries(main gdi32)
