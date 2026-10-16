cmake_minimum_required(VERSION 3.20)
project(mini_online CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0A00)
endif()
if(NOT MSVC)
  add_compile_options(-Wall -Wextra)
endif()

include_directories(common)

add_subdirectory(common)
add_subdirectory(client)
add_subdirectory(mmclient)
add_subdirectory(protodemo)

if(NOT WIN32)
  add_subdirectory(server)
  add_subdirectory(matchmaker)
endif()