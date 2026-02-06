cmake_minimum_required(VERSION 3.10)

project(ReactUIApp C)

# Add include directory
include_directories(include)

# Create executable from main.c
add_executable(main src/main.c)

# Link Windows GDI library
target_link_libraries(main gdi32)