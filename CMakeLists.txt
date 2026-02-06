cmake_minimum_required(VERSION 3.10)

# Project name and language
project(SimpleCProject C)

# Set the source file
set(SRC src/main.c)

# Add executable
add_executable(main ${SRC})
