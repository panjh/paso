cmake_minimum_required(VERSION 4.0)
project(paso)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -g3")

include_directories(src)

add_executable(paso src/paso/TaskPool.cpp test/test.cpp)

add_executable(tutorial test/tutorial.cpp)

