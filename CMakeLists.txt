cmake_minimum_required(VERSION 3.16)
project(smoothoperator
  VERSION 1.0.0
  DESCRIPTION "SmoothOperator - Intelliurb FM RabbitMQ Controller (C++26)"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Versioning
add_compile_definitions(SMOOTHOP_VERSION="${PROJECT_VERSION}")

# Flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion")

# Hardening
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_FORTIFY_SOURCE=2 -fstack-protector-strong")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fstack-clash-protection -fno-common")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 --coverage")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
else()
  set(CMAKE_BUILD_TYPE "Release")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -fPIE")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pie -Wl,-z,relro -Wl,-z,now")
endif()

# Dependencies
include(FetchContent)
include(GNUInstallDirs)

# Installation paths (can be overridden)
set(SMOOTHOP_CONFIG_DIR "${CMAKE_INSTALL_SYSCONFDIR}/smoothoperator" CACHE PATH "Directory for configuration files")
set(SMOOTHOP_LOG_DIR "${CMAKE_INSTALL_LOCALSTATEDIR}/log/smoothoperator" CACHE PATH "Directory for log files")

# nlohmann/json
FetchContent_Declare(
  json
  URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
)
FetchContent_MakeAvailable(json)

# AMQP-CPP
FetchContent_Declare(
  amqpcpp
  GIT_REPOSITORY https://github.com/CopernicaMarketingSoftware/AMQP-CPP.git
  GIT_TAG v4.3.24
)
# AMQP-CPP needs some options
set(AMQP-CPP_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(AMQP-CPP_LINUX_TCP ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(amqpcpp)

# libev (manual find if pkg-config fails)
find_path(LIBEV_INCLUDE_DIR NAMES ev.h)
find_library(LIBEV_LIBRARY NAMES ev)

if(NOT LIBEV_INCLUDE_DIR OR NOT LIBEV_LIBRARY)
    message(FATAL_ERROR "libev not found. Please install libev-dev")
endif()

# Source files
set(SMOOTHOP_SOURCES
  src/main.cpp
  src/config/config_parser.cpp
  src/drivers/rabbitmq_driver.cpp
  src/drivers/liquidsoap_driver.cpp
  src/core/state_manager.cpp
)

add_executable(smoothoperator ${SMOOTHOP_SOURCES})
target_include_directories(smoothoperator PRIVATE 
    include 
    src 
    ${LIBEV_INCLUDE_DIR}
)
# Silence warnings from dependencies
target_include_directories(smoothoperator SYSTEM PRIVATE 
    ${json_SOURCE_DIR}/include
    ${amqpcpp_SOURCE_DIR}/include
)
target_link_libraries(smoothoperator
  nlohmann_json::nlohmann_json
  amqpcpp
  ${LIBEV_LIBRARY}
  pthread
)

# Installation
install(TARGETS smoothoperator DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES smoothoperator.conf.example DESTINATION ${SMOOTHOP_CONFIG_DIR})
install(FILES smoothoperator.json.example DESTINATION ${SMOOTHOP_CONFIG_DIR})
install(FILES smoothoperator.env.example DESTINATION ${SMOOTHOP_CONFIG_DIR})

# Sanitizers

# Sanitizers
option(ENABLE_ASAN "Enable AddressSanitizer" ON)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR "ASAN and TSAN are mutually exclusive. Choose one.")
endif()

if(ENABLE_ASAN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,undefined -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address,undefined")
endif()

if(ENABLE_TSAN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
endif()

# GTest
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/heads/main.zip
)
FetchContent_MakeAvailable(googletest)

# Test Sources
set(TEST_SOURCES
  test/unit/test_state_manager.cpp
)

add_executable(smoothoperator_unit_tests 
    ${TEST_SOURCES}
    src/core/state_manager.cpp
    src/config/config_parser.cpp
)
target_include_directories(smoothoperator_unit_tests PRIVATE 
    include 
    src
    ${LIBEV_INCLUDE_DIR}
)
# Silence warnings from dependencies
target_include_directories(smoothoperator_unit_tests SYSTEM PRIVATE 
    ${json_SOURCE_DIR}/include
    ${amqpcpp_SOURCE_DIR}/include
)
target_link_libraries(smoothoperator_unit_tests 
    GTest::gtest_main 
    GTest::gmock
    nlohmann_json::nlohmann_json
    pthread
)

include(GoogleTest)
gtest_discover_tests(smoothoperator_unit_tests)
