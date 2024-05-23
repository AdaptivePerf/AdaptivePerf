cmake_minimum_required(VERSION 3.14)

if(COMMAND cmake_policy)
  cmake_policy(SET CMP0003 NEW)
  cmake_policy(SET CMP0012 NEW)
  cmake_policy(SET CMP0115 NEW)
  cmake_policy(SET CMP0135 NEW)
endif()

project(adaptiveperf-server
  DESCRIPTION "Post-processing server for AdaptivePerf"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/2954cb8d879886403d55343f941ae7d0216e0f6b.zip)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
  OUTPUT_VARIABLE GIT_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE)

configure_file(src/server/version.cpp.in version.cpp @ONLY)

add_library(server.o OBJECT src/server/server.cpp)
add_library(client.o OBJECT src/server/client.cpp)
add_library(subclient.o OBJECT src/server/subclient.cpp)
add_library(socket.o OBJECT src/server/socket.cpp)

add_library(version.o OBJECT version.cpp)
target_include_directories(version.o PRIVATE ${CMAKE_SOURCE_DIR}/src/server)

add_executable(adaptiveperf-server
  src/server/main.cpp)

find_package(nlohmann_json REQUIRED)
find_package(Poco REQUIRED Net Foundation)
find_package(CLI11 CONFIG REQUIRED)

target_link_libraries(adaptiveperf-server PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(adaptiveperf-server PRIVATE Poco::Foundation Poco::Net)
target_link_libraries(adaptiveperf-server PRIVATE CLI11::CLI11)
target_link_libraries(adaptiveperf-server PRIVATE server.o client.o subclient.o socket.o version.o)

enable_testing()
add_executable(auto-test-server
  test/server/test_server.cpp)
add_executable(auto-test-client
  test/server/test_client.cpp)
add_executable(auto-test-subclient
  test/server/test_subclient.cpp)
add_executable(auto-test-socket
  test/server/test_socket.cpp)
add_executable(auto-test-main
  test/server/test_main.cpp)

target_include_directories(auto-test-server PRIVATE ${CMAKE_SOURCE_DIR}/src/server)
target_include_directories(auto-test-client PRIVATE ${CMAKE_SOURCE_DIR}/src/server)
target_include_directories(auto-test-subclient PRIVATE ${CMAKE_SOURCE_DIR}/src/server)
target_include_directories(auto-test-socket PRIVATE ${CMAKE_SOURCE_DIR}/src/server)
target_include_directories(auto-test-main PRIVATE ${CMAKE_SOURCE_DIR}/src/server)

target_link_libraries(auto-test-server PRIVATE GTest::gtest_main GTest::gmock_main Poco::Foundation Poco::Net server.o client.o)
target_link_libraries(auto-test-client PRIVATE GTest::gtest_main)
target_link_libraries(auto-test-subclient PRIVATE GTest::gtest_main)
target_link_libraries(auto-test-socket PRIVATE GTest::gtest_main)
target_link_libraries(auto-test-main PRIVATE GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(auto-test-server)
gtest_discover_tests(auto-test-client)
gtest_discover_tests(auto-test-subclient)
gtest_discover_tests(auto-test-socket)
gtest_discover_tests(auto-test-main)
