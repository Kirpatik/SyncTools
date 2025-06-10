include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
)
FetchContent_MakeAvailable(googletest)

add_executable(tests
    mpsc_tests.cpp
    mutex_tests.cpp
    spsc_tests.cpp
    object_pool_tests.cpp
)

if(ENABLE_COVERAGE)
  target_compile_options(tests PRIVATE -O0 --coverage -fprofile-update=atomic -DSYNC_TOOLS_ENABLE_DEADLOCK_CHECKS -Wall -Wextra -Werror)
  target_link_options(tests PRIVATE --coverage -fprofile-update=atomic)
else()
  target_compile_options(tests PRIVATE -O0 -fsanitize=thread -DSYNC_TOOLS_ENABLE_DEADLOCK_CHECKS -Wall -Wextra -Werror)
  target_link_options(tests PRIVATE -fsanitize=thread)
endif()

target_include_directories(tests PRIVATE ${CMAKE_SOURCE_DIR}/include/sync_tools)
target_link_libraries(tests PRIVATE gtest gtest_main pthread)
