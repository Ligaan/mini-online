if(WIN32)
  find_package(Protobuf CONFIG REQUIRED)
else()
  find_package(Protobuf REQUIRED)
endif()

add_library(gamenet STATIC game.proto)
protobuf_generate(TARGET gamenet LANGUAGE cpp)
target_include_directories(gamenet PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(gamenet PUBLIC protobuf::libprotobuf)

if(NOT WIN32)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GRPCPP REQUIRED IMPORTED_TARGET grpc++)
  find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)

  set(CTRL_PROTO ${CMAKE_CURRENT_SOURCE_DIR}/control.proto)
  set(CTRL_OUT ${CMAKE_CURRENT_BINARY_DIR})
  add_custom_command(
    OUTPUT ${CTRL_OUT}/control.pb.cc ${CTRL_OUT}/control.pb.h
           ${CTRL_OUT}/control.grpc.pb.cc ${CTRL_OUT}/control.grpc.pb.h
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
            --cpp_out=${CTRL_OUT} --grpc_out=${CTRL_OUT}
            --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
            -I ${CMAKE_CURRENT_SOURCE_DIR} ${CTRL_PROTO}
    DEPENDS ${CTRL_PROTO})

  add_library(controlnet STATIC ${CTRL_OUT}/control.pb.cc ${CTRL_OUT}/control.grpc.pb.cc)
  target_include_directories(controlnet PUBLIC ${CTRL_OUT})
  target_link_libraries(controlnet PUBLIC PkgConfig::GRPCPP protobuf::libprotobuf)
endif()