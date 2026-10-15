add_executable(mmclient main.cpp)
target_include_directories(mmclient PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
find_package(Threads REQUIRED)
target_link_libraries(mmclient PRIVATE Threads::Threads)