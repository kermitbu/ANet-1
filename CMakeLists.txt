cmake_minimum_required(VERSION 2.4)
project(xnet)

set(CMAKE_CXX_FLAGS "-fPIC -m64 -std=c++11 -w")

set(SOURCE_FILES ae.cpp anet.cpp protocol.cpp buffer.cpp)

add_executable(server server.cpp ${SOURCE_FILES})

add_executable(client client.cpp ${SOURCE_FILES})
