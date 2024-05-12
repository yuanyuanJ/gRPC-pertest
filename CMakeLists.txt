cmake_minimum_required(VERSION 3.5)
project(perftest)
find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED CONFIG)
set(_PROTOBUF_LIBPROTOBUF protobuf::libprotobuf)
#set(_REFLECTION gRPC::grpc++_reflection)
find_package(gRPC CONFIG REQUIRED)
set(_GRPC_GRPCPP gRPC::grpc++)

# Include generated *.pb.h files
include_directories("${CMAKE_CURRENT_BINARY_DIR}/../gen_code")
set(hw_proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/../gen_code/perf.pb.cc")
set(hw_proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/../gen_code/perf.pb.h")
set(hw_grpc_srcs "${CMAKE_CURRENT_BINARY_DIR}/../gen_code/perf.grpc.pb.cc")
set(hw_grpc_hdrs "${CMAKE_CURRENT_BINARY_DIR}/../gen_code/perf.grpc.pb.h")
# hw_grpc_proto
add_library(hw_grpc_proto
  ${hw_grpc_srcs}
  ${hw_grpc_hdrs}
  ${hw_proto_srcs}
  ${hw_proto_hdrs})
target_link_libraries(hw_grpc_proto
#  ${_REFLECTION}
  ${_GRPC_GRPCPP}
  ${_PROTOBUF_LIBPROTOBUF})

# Targets greeter_[async_](client|server)
  add_executable(test_server "test_server.cpp")
  target_link_libraries(test_server
    hw_grpc_proto
#    ${_REFLECTION}
    ${_GRPC_GRPCPP}
    ${_PROTOBUF_LIBPROTOBUF})

    add_executable(test_client "test_client.cpp")
    target_link_libraries(test_client
      hw_grpc_proto
  #    ${_REFLECTION}
      ${_GRPC_GRPCPP}
      ${_PROTOBUF_LIBPROTOBUF})