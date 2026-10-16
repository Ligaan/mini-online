add_executable(matchmaker main.cpp)
target_include_directories(matchmaker PRIVATE
 ${CMAKE_SOURCE_DIR}/third_party
 ${CMAKE_SOURCE_DIR}/third_party/jwt
 )

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIREDIS REQUIRED IMPORTED_TARGET hiredis)
target_link_libraries(matchmaker PRIVATE Threads::Threads OpenSSL::Crypto controlnet PkgConfig::HIREDIS)