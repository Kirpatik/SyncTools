include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
)
FetchContent_MakeAvailable(googletest)


add_executable(tests mpsc_tests.cpp mutex_tests.cpp spsc_tests.cpp)

target_compile_options(tests PRIVATE -O0 -fsanitize=thread -DSYNC_TOOLS_ENABLE_DEADLOCK_CHECKS)
target_link_options(tests PRIVATE -fsanitize=thread)

target_include_directories(tests PRIVATE ${CMAKE_SOURCE_DIR}/include/sync_tools)
target_link_libraries(tests PRIVATE gtest gtest_main pthread)