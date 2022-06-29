set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -g -std=c++11")
add_subdirectory(thirth_party/fusexx)

include_directories(thirth_party/fusexx/include)
add_executable(Server Server.cpp FuseFS.cpp)
target_link_libraries(Server  fuse++ fuse3 pthread)