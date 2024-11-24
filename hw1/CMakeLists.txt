cmake_minimum_required(VERSION 3.23)
project(hw1)

set(CMAKE_C_STANDARD 11)

add_executable(master master.c)
add_executable(worker worker.c)

target_compile_options(master PRIVATE -Wall -Wextra)
target_compile_options(worker PRIVATE -Wall -Wextra)

target_link_libraries(master pthread)