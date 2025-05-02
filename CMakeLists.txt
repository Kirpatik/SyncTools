cmake_minimum_required(VERSION 3.20)
project(SyncTools VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(SyncTools INTERFACE)
target_include_directories(SyncTools INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
add_library(SyncTools::SyncTools ALIAS SyncTools)

option(ENABLE_BENCHMARKS "Enable google Benchmark" OFF)
option(ENABLE_TESTING    "Enable google tests"     OFF)
option(BUILD_EXAMPLES    "Build Readme examples"   OFF)

if(ENABLE_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
if(ENABLE_TESTING)
  add_subdirectory(tests)
endif()
if(BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  include(CMakePackageConfigHelpers)

  install(TARGETS SyncTools EXPORT SyncToolsTargets)
  install(EXPORT SyncToolsTargets
    NAMESPACE SyncTools::
    DESTINATION lib/cmake/SyncTools
  )
  install(DIRECTORY include/ DESTINATION include)

  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/SyncToolsConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
  )
  configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SyncToolsConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/SyncToolsConfig.cmake"
    INSTALL_DESTINATION lib/cmake/SyncTools
  )
  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/SyncToolsConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/SyncToolsConfigVersion.cmake"
    DESTINATION lib/cmake/SyncTools
  )
endif()
