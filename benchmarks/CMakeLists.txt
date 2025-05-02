set(BENCHMARK_ENABLE_GTEST_TESTS OFF)

include(FetchContent)

FetchContent_Declare(
  benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.9.1
)
FetchContent_MakeAvailable(benchmark)

FetchContent_Declare(
  tbb
  GIT_REPOSITORY https://github.com/oneapi-src/oneTBB.git
  GIT_TAG v2022.1.0
)
FetchContent_MakeAvailable(tbb)

FetchContent_Declare(
  abseil
  GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
  GIT_TAG        20250127.1
)
FetchContent_MakeAvailable(abseil)

add_executable(benchmarks mutex_benchmarks.cpp mpsc_benchmarks.cpp spsc_benchmarks.cpp pool_benchmarks.cpp)
target_link_libraries(benchmarks PRIVATE benchmark::benchmark tbb tbbmalloc absl::synchronization)
target_include_directories(benchmarks PRIVATE ${CMAKE_SOURCE_DIR}/include/sync_tools)
target_compile_options(benchmarks PRIVATE -O3 -march=native -flto)
target_link_options(benchmarks PRIVATE -flto)