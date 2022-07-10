project(RaftFuseFS)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -g -std=c++11")

add_subdirectory(thirth_party/braft)
add_subdirectory(thirth_party/brpc)
include_directories(
        ${CMAKE_BINARY_DIR}/thirth_party/braft/output/include
        ${CMAKE_BINARY_DIR}/thirth_party/brpc/output/include)
link_directories(
        ${CMAKE_BINARY_DIR}/thirth_party/braft/output/lib
        ${CMAKE_BINARY_DIR}/thirth_party/brpc/output/lib)

add_subdirectory(thirth_party/fusexx)

include_directories(thirth_party/fusexx/include)

include(FindProtobuf)
protobuf_generate_cpp(PROTO_SRC PROTO_HEADER raft_log.proto)
# include PROTO_HEADER
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_executable(Server Server.cpp FuseFS.cpp RaftStateMachine.cpp Utils.cpp ${PROTO_SRC} ${PROTO_HEADER})
target_link_libraries(Server  fuse++ fuse3 braft brpc gflags event protobuf leveldb pthread ssl crypto dl)