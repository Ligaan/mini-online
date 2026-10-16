find_package(Protobuf REQUIRED)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS game.proto)

add_library(gamenet STATIC ${PROTO_SRCS})
target_include_directories(gamenet PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(gamenet PUBLIC protobuf::libprotobuf)

