cmake_minimum_required(VERSION 3.20)
project(mini_online CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)

include_directories(common)
add_subdirectory(server)
add_subdirectory(client)
add_subdirectory(matchmaker)
add_subdirectory(mmclient)
add_subdirectory(common)
add_subdirectory(protodemo)

