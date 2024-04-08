cmake_minimum_required(VERSION 3.9.6)

if(COMMAND cmake_policy)
  cmake_policy(SET CMP0003 NEW)
  cmake_policy(SET CMP0012 NEW)
  cmake_policy(SET CMP0115 NEW)
endif()

project(adaptiveperf-server
  DESCRIPTION "Post-processing server for AdaptivePerf"
  LANGUAGES CXX)

execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
  OUTPUT_VARIABLE GIT_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE)

configure_file(version.cpp.in version.cpp @ONLY)

add_executable(adaptiveperf-server
  server.cpp
  socket.cpp
  version.cpp)

find_package(nlohmann_json REQUIRED)
find_package(Poco REQUIRED Net Foundation)
find_package(CLI11 CONFIG REQUIRED)

target_link_libraries(adaptiveperf-server PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(adaptiveperf-server PRIVATE Poco::Foundation Poco::Net)
target_link_libraries(adaptiveperf-server PRIVATE CLI11::CLI11)

target_include_directories(adaptiveperf-server PRIVATE ${CMAKE_SOURCE_DIR})
