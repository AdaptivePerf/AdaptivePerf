# SPDX-FileCopyrightText: 2026 CERN
# SPDX-License-Identifier: LGPL-3.0-or-later

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

include(FetchContent)
include(CMakePackageConfigHelpers)

project(adaptyst
  DESCRIPTION "Performance analysis tool"
  LANGUAGES CXX)

# option(ENABLE_TESTS "Enable Adaptyst automated tests" OFF)
set(ADAPTYST_MISC_PATH "${CMAKE_INSTALL_PREFIX}/opt/adaptyst" CACHE STRING "Path where Adaptyst miscellaneous files should be installed into")
set(ADAPTYST_MODULE_PATH "${CMAKE_INSTALL_PREFIX}/opt/adaptyst/modules" CACHE STRING "Path where Adaptyst modules should be installed into")
set(ADAPTYST_CONFIG_PATH "${CMAKE_INSTALL_PREFIX}/etc/adaptyst.conf" CACHE STRING "Path where Adaptyst config file should be stored in")

message(WARNING "The default values of ADAPTYST_MISC_PATH, ADAPTYST_MODULE_PATH, and "
                "ADAPTYST_CONFIG_PATH have changed!\nThese are prefixed by the value of "
                "CMAKE_INSTALL_PREFIX (in your case: ${CMAKE_INSTALL_PREFIX}) now. You may "
                "ignore this warning if you have specified "
                "custom values of the above variables.")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(nlohmann_json REQUIRED)
find_package(Poco REQUIRED Net Foundation)
find_package(CLI11 CONFIG REQUIRED)
find_package(LibArchive REQUIRED)

FetchContent_Declare(ryml
  GIT_REPOSITORY https://github.com/biojppm/rapidyaml
  GIT_TAG v0.9.0
  GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(ryml)

FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
  GIT_TAG v7.0.3
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(ftxui)

if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.30")
  find_package(Boost REQUIRED program_options CONFIG
               OPTIONAL_COMPONENTS process)
  message(STATUS "Found Boost: headers inside ${Boost_INCLUDE_DIRS}, version ${Boost_VERSION_STRING}")
else()
  find_package(Boost REQUIRED program_options
               OPTIONAL_COMPONENTS process)
endif()

execute_process(
  COMMAND git describe --tags
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
  OUTPUT_VARIABLE GIT_DESC_TAG
  OUTPUT_STRIP_TRAILING_WHITESPACE)

configure_file(src/version.cpp.in version.cpp @ONLY)

add_library(adaptyst SHARED
  src/system.cpp
  src/print.cpp
  src/archive.cpp
  src/ir.cpp
  version.cpp)

target_link_libraries(adaptyst PUBLIC nlohmann_json::nlohmann_json)
target_link_libraries(adaptyst PUBLIC Poco::Foundation Poco::Net)
target_link_libraries(adaptyst PUBLIC LibArchive::LibArchive)
target_link_libraries(adaptyst PRIVATE ryml::ryml)

target_include_directories(adaptyst PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

add_library(adaptyst_inject SHARED src/inject.cpp)

target_link_libraries(adaptyst_inject PUBLIC Poco::Foundation Poco::Net)
target_include_directories(adaptyst_inject PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

configure_package_config_file(
  ${CMAKE_SOURCE_DIR}/adaptyst-config.cmake.in
  ${CMAKE_BINARY_DIR}/adaptyst-config.cmake
  INSTALL_DESTINATION lib/cmake/adaptyst
  PATH_VARS ADAPTYST_MODULE_PATH)

install(TARGETS adaptyst adaptyst_inject
  EXPORT adaptyst-targets
  LIBRARY
  INCLUDES)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/adaptyst
  DESTINATION include)

install(EXPORT adaptyst-targets
  FILE adaptyst-targets.cmake
  NAMESPACE adaptyst::
  DESTINATION lib/cmake/adaptyst)

install(FILES
  ${CMAKE_BINARY_DIR}/adaptyst-config.cmake
  DESTINATION lib/cmake/adaptyst)

# Main Adaptyst setup
add_executable(adaptyst-bin
  src/main.cpp
  src/entrypoint.cpp
  src/tui.cpp
  src/workflow.cpp)

target_compile_definitions(adaptyst-bin PRIVATE ADAPTYST_MISC_PATH="${ADAPTYST_MISC_PATH}")
target_compile_definitions(adaptyst-bin PRIVATE ADAPTYST_MODULE_PATH="${ADAPTYST_MODULE_PATH}")
target_compile_definitions(adaptyst-bin PRIVATE ADAPTYST_CONFIG_FILE="${ADAPTYST_CONFIG_PATH}")

target_link_libraries(adaptyst-bin PUBLIC adaptyst)
target_link_libraries(adaptyst-bin PUBLIC CLI11::CLI11)
target_link_libraries(adaptyst-bin PUBLIC Boost::headers Boost::program_options $<TARGET_NAME_IF_EXISTS:Boost::process>)
target_link_libraries(adaptyst-bin PRIVATE ftxui::ftxui)

target_include_directories(adaptyst-bin PUBLIC ${CMAKE_SOURCE_DIR}/include)

target_include_directories(adaptyst PUBLIC ${Boost_INCLUDE_DIRS})
target_include_directories(adaptyst-bin PUBLIC ${Boost_INCLUDE_DIRS})

set_target_properties(adaptyst-bin PROPERTIES OUTPUT_NAME "adaptyst")
install(TARGETS adaptyst-bin RUNTIME)
install(PROGRAMS src/utils/adaptyst-code.py TYPE BIN RENAME adaptyst-code)
install(DIRECTORY DESTINATION ${ADAPTYST_MODULE_PATH})

# if (ENABLE_TESTS)
#   include(FetchContent)
#   FetchContent_Declare(
#     googletest
#     URL https://github.com/google/googletest/archive/2954cb8d879886403d55343f941ae7d0216e0f6b.zip)

#   set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
#   FetchContent_MakeAvailable(googletest)

#   enable_testing()
#   add_executable(auto-test-server
#     test/server/test_server.cpp)
#   add_executable(auto-test-client
#     test/server/test_client.cpp)
#   add_executable(auto-test-subclient
#     test/server/test_subclient.cpp)
#   add_executable(auto-test-socket
#     test/server/test_socket.cpp)

#   target_include_directories(auto-test-server PRIVATE ${CMAKE_SOURCE_DIR}/src/server)
#   target_include_directories(auto-test-client PRIVATE ${CMAKE_SOURCE_DIR}/src/server)
#   target_include_directories(auto-test-subclient PRIVATE ${CMAKE_SOURCE_DIR}/src/server)
#   target_include_directories(auto-test-socket PRIVATE ${CMAKE_SOURCE_DIR}/src/server)

#   target_link_libraries(auto-test-server PUBLIC GTest::gtest_main GTest::gmock_main Poco::Foundation Poco::Net)
#   target_link_libraries(auto-test-server PRIVATE adaptystserv)

#   target_link_libraries(auto-test-client PUBLIC GTest::gtest_main GTest::gmock_main Poco::Foundation Poco::Net)
#   target_link_libraries(auto-test-client PRIVATE adaptystserv)

#   target_link_libraries(auto-test-subclient PUBLIC GTest::gtest_main GTest::gmock_main Poco::Foundation Poco::Net)
#   target_link_libraries(auto-test-subclient PRIVATE adaptystserv)

#   target_link_libraries(auto-test-socket PUBLIC GTest::gtest_main GTest::gmock_main Poco::Foundation Poco::Net)
#   target_link_libraries(auto-test-socket PRIVATE adaptystserv)

#   include(GoogleTest)
#   gtest_discover_tests(auto-test-server)
#   gtest_discover_tests(auto-test-client)
#   gtest_discover_tests(auto-test-subclient)
#   gtest_discover_tests(auto-test-socket)
# endif()
