add_executable(matchmaker main.cpp)
target_include_directories(matchmaker PRIVATE ${CMAKE_SOURCE_DIR}/third_party)

find_package(Threads REQUIRED)
target_link_libraries(matchmaker PRIVATE Threads::Threads)