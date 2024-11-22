cmake_minimum_required(VERSION 3.20)

if(COMMAND cmake_policy)
  cmake_policy(SET CMP0003 NEW)
  cmake_policy(SET CMP0012 NEW)
  cmake_policy(SET CMP0115 NEW)

  if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24")
    cmake_policy(SET CMP0135 NEW)
  endif()

  if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.30")
    cmake_policy(SET CMP0167 NEW)
  endif()
endif()

option(SERVER_ONLY "Compile only adaptiveperf-server (OS-portable)" OFF)
option(ENABLE_TESTS "Enable AdaptivePerf automated tests" OFF)
set(APERF_SCRIPT_PATH "/opt/adaptiveperf" CACHE STRING "Path where AdaptivePerf helper scripts should be installed into")
set(APERF_CONFIG_PATH "/etc/adaptiveperf.conf" CACHE STRING "Path where AdaptivePerf config file should be stored in")

if(SERVER_ONLY)
  project(adaptiveperf-server
    DESCRIPTION "Post-processing server for AdaptivePerf"
    LANGUAGES CXX)
else()
  project(adaptiveperf
    DESCRIPTION "Comprehensive profiling tool based on Linux perf"
    LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(nlohmann_json REQUIRED)
find_package(Poco REQUIRED Net Foundation)
find_package(CLI11 CONFIG REQUIRED)

execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
  OUTPUT_VARIABLE GIT_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE)

configure_file(src/version.cpp.in version.cpp @ONLY)

add_library(version.o OBJECT version.cpp)
target_include_directories(version.o PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_library(server.o OBJECT src/server/server.cpp)
add_library(client.o OBJECT src/server/client.cpp)
add_library(subclient.o OBJECT src/server/subclient.cpp)

add_library(socket.o OBJECT src/server/socket.cpp)
if(SERVER_ONLY)
  target_compile_definitions(socket.o PRIVATE SERVER_ONLY)
endif()

add_library(server_entrypoint.o OBJECT src/server/entrypoint.cpp)
target_include_directories(server_entrypoint.o PRIVATE ${CMAKE_SOURCE_DIR}/src/cmd)

add_library(aperfserv SHARED)
target_include_directories(aperfserv PRIVATE ${nlohmann_json_INCLUDE_DIRS})
target_include_directories(aperfserv PRIVATE ${Poco_INCLUDE_DIRS})
target_link_libraries(aperfserv PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(aperfserv PRIVATE Poco::Foundation Poco::Net)
target_link_libraries(aperfserv PRIVATE server.o client.o subclient.o socket.o)

add_executable(adaptiveperf-server
  src/main.cpp)

target_compile_definitions(adaptiveperf-server PRIVATE SERVER_ONLY)
target_include_directories(adaptiveperf-server PRIVATE ${CLI11_INCLUDE_DIRS})
target_link_libraries(adaptiveperf-server PRIVATE Poco::Foundation Poco::Net)
target_link_libraries(adaptiveperf-server PRIVATE CLI11::CLI11)
target_link_libraries(adaptiveperf-server PRIVATE aperfserv server_entrypoint.o version.o)

if(NOT SERVER_ONLY)
  add_executable(adaptiveperf
    src/main.cpp)

  add_library(main_entrypoint.o OBJECT src/entrypoint.cpp)
  target_include_directories(main_entrypoint.o PRIVATE ${CMAKE_SOURCE_DIR}/src/cmd)

  add_library(print.o OBJECT src/print.cpp)
  add_library(profilers.o OBJECT src/profilers.cpp)
  add_library(profiling.o OBJECT src/profiling.cpp)
  add_library(requirements.o OBJECT src/requirements.cpp)
  add_library(archive.o OBJECT src/archive.cpp)
  add_library(process.o OBJECT src/process.cpp)

  target_compile_definitions(profilers.o PRIVATE APERF_SCRIPT_PATH="${APERF_SCRIPT_PATH}")
  target_compile_definitions(main_entrypoint.o PRIVATE APERF_CONFIG_FILE="${APERF_CONFIG_PATH}")

  find_package(Boost REQUIRED program_options)

  if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.30")
    message(STATUS "Found Boost: headers inside ${Boost_INCLUDE_DIRS}, version ${Boost_VERSION_STRING}")
  endif()

  find_package(LibArchive REQUIRED)

  target_link_libraries(adaptiveperf PRIVATE nlohmann_json::nlohmann_json)
  target_link_libraries(adaptiveperf PRIVATE Poco::Foundation Poco::Net)
  target_link_libraries(adaptiveperf PRIVATE CLI11::CLI11)
  target_link_libraries(adaptiveperf PRIVATE Boost::program_options)
  target_link_libraries(adaptiveperf PRIVATE LibArchive::LibArchive)

  target_include_directories(adaptiveperf PRIVATE ${nlohmann_json_INCLUDE_DIRS})
  target_include_directories(adaptiveperf PRIVATE ${Poco_INCLUDE_DIRS})
  target_include_directories(adaptiveperf PRIVATE ${CLI11_INCLUDE_DIRS})
  target_include_directories(adaptiveperf PRIVATE ${Boost_INCLUDE_DIRS})
  target_include_directories(adaptiveperf PRIVATE ${LibArchive_INCLUDE_DIRS})

  find_library(LIBNUMA
    NAMES numa
    DOC "libnuma")

  if(LIBNUMA)
    message(STATUS "Found libnuma: ${LIBNUMA}")

    find_path(LIBNUMA_INCLUDE
      NAMES numa.h
      PATH_SUFFIXES include
      DOC "libnuma header directory"
    )

    if (LIBNUMA_INCLUDE)
      message(STATUS "Found numa.h inside ${LIBNUMA_INCLUDE}")
      set(LIBNUMA_AVAILABLE TRUE)
    else()
      message(STATUS "numa.h not found, compiling without libnuma support")
      set(LIBNUMA_AVAILABLE FALSE)
    endif()
  else()
    message(STATUS "libnuma not found, compiling without libnuma support")
    set(LIBNUMA_AVAILABLE FALSE)
  endif()

  if(LIBNUMA_AVAILABLE)
    target_compile_definitions(requirements.o PRIVATE LIBNUMA_AVAILABLE)
    target_include_directories(requirements.o PRIVATE ${LIBNUMA_INCLUDE})
    target_link_libraries(adaptiveperf PRIVATE numa)
  endif()

  target_link_libraries(adaptiveperf PRIVATE aperfserv)
  target_link_libraries(adaptiveperf PRIVATE
    profiling.o requirements.o profilers.o print.o archive.o main_entrypoint.o process.o version.o)
endif()

if (ENABLE_TESTS)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/2954cb8d879886403d55343f941ae7d0216e0f6b.zip)

  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  enable_testing()
  add_executable(auto-test-server
    test/server/test_server.cpp)
  add_executable(auto-test-client
    test/server/test_client.cpp)
  add_executable(auto-test-subclient
    test/server/test_subclient.cpp)
  add_executable(auto-test-socket
    test/server/test_socket.cpp)

  target_include_directories(auto-test-server PRIVATE ${CMAKE_SOURCE_DIR}/src/server)
  target_include_directories(auto-test-client PRIVATE ${CMAKE_SOURCE_DIR}/src/server)
  target_include_directories(auto-test-subclient PRIVATE ${CMAKE_SOURCE_DIR}/src/server)
  target_include_directories(auto-test-socket PRIVATE ${CMAKE_SOURCE_DIR}/src/server)

  target_include_directories(auto-test-server PRIVATE ${Poco_INCLUDE_DIRS})
  target_include_directories(auto-test-client PRIVATE ${Poco_INCLUDE_DIRS})
  target_include_directories(auto-test-subclient PRIVATE ${Poco_INCLUDE_DIRS})
  target_include_directories(auto-test-socket PRIVATE ${Poco_INCLUDE_DIRS})

  target_link_libraries(auto-test-server PRIVATE GTest::gtest_main GTest::gmock_main Poco::Foundation Poco::Net server.o client.o)
  target_link_libraries(auto-test-client PRIVATE GTest::gtest_main GTest::gmock_main Poco::Foundation Poco::Net client.o)
  target_link_libraries(auto-test-subclient PRIVATE GTest::gtest_main GTest::gmock_main Poco::Foundation Poco::Net subclient.o)
  target_link_libraries(auto-test-socket PRIVATE GTest::gtest_main GTest::gmock_main Poco::Foundation Poco::Net socket.o)

  include(GoogleTest)
  gtest_discover_tests(auto-test-server)
  gtest_discover_tests(auto-test-client)
  gtest_discover_tests(auto-test-subclient)
  gtest_discover_tests(auto-test-socket)
endif()
