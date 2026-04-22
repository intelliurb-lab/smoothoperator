cmake_minimum_required(VERSION 3.10)
project(smoothoperator
  VERSION 0.2.0
  DESCRIPTION "SmoothOperator - Intelliurb FM RabbitMQ Controller"
  LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Flags padrão
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic -Wformat -Wformat-security")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror=format-security -Werror=implicit-function-declaration")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wconversion -Wsign-conversion -Wshadow")

# Hardening flags (all builds)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_FORTIFY_SOURCE=2 -fstack-protector-strong")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-clash-protection -fno-common")

# Debug vs Release
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0 --coverage")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fsanitize=undefined")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address -fsanitize=undefined")
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -fPIE")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pie -Wl,-z,relro -Wl,-z,now")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,noexecstack -Wl,-z,separate-code")
else()
  set(CMAKE_BUILD_TYPE "Debug")
endif()

# Dependências
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBRABBITMQ REQUIRED librabbitmq)
pkg_check_modules(JANSSON REQUIRED jansson)
pkg_check_modules(CUNIT REQUIRED cunit)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${LIBRABBITMQ_INCLUDE_DIRS})
include_directories(${JANSSON_INCLUDE_DIRS})
include_directories(${CUNIT_INCLUDE_DIRS})

# Código-fonte principal
set(SMOOTHOP_SOURCES
  src/main.c
  src/config.c
  src/message.c
  src/liquidsoap_client.c
  src/rabbitmq_consumer.c
  src/rabbitmq_producer.c
  src/ls_controller.c
  src/smoothoperator_logging.c
)

# Executável principal
add_executable(smoothoperator ${SMOOTHOP_SOURCES})
target_link_libraries(smoothoperator
  ${LIBRABBITMQ_LIBRARIES}
  ${JANSSON_LIBRARIES}
  pthread
  m
)

# Testes (Critério)
enable_testing()

set(TEST_SOURCES
  test/test_runner.c
  test/test_message.c
  test/test_config.c
  test/test_liquidsoap_client.c
  test/test_ls_controller.c
  test/test_logging.c
  # test/test_rabbitmq_consumer.c  # integração, roda separado
)

# Compilar sources sem main.c para testes
list(REMOVE_ITEM SMOOTHOP_SOURCES src/main.c)

# Executável de testes
add_executable(smoothoperator_tests ${TEST_SOURCES} ${SMOOTHOP_SOURCES})
target_link_libraries(smoothoperator_tests
  ${LIBRABBITMQ_LIBRARIES}
  ${JANSSON_LIBRARIES}
  ${CUNIT_LIBRARIES}
  pthread
  m
)

# Discovery automático de testes
add_test(
  NAME SmoothOperatorTests
  COMMAND smoothoperator_tests
)

# Opções de build
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
