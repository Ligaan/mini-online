add_executable(server main.cpp)
target_include_directories(server PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/third_party/jwt)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
target_link_libraries(server PRIVATE OpenSSL::Crypto gamenet controlnet Threads::Threads)
