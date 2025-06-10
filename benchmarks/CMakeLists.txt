set(BENCHMARK_ENABLE_GTEST_TESTS OFF)

    include(FetchContent)

        FetchContent_Declare(benchmark GIT_REPOSITORY https :  // github.com/google/benchmark.git
                             GIT_TAG v1.9.1) FetchContent_MakeAvailable(benchmark)

            find_package(TBB REQUIRED CONFIG)

                FetchContent_Declare(abseil GIT_REPOSITORY https :  // github.com/abseil/abseil-cpp.git
                                     GIT_TAG 20250127.1) FetchContent_MakeAvailable(abseil)

                    add_executable(
                        benchmarks mutex_benchmarks.cpp mpsc_benchmarks.cpp spsc_benchmarks.cpp pool_benchmarks.cpp)
                        target_link_libraries(
                            benchmarks PRIVATE benchmark::benchmark TBB::tbb TBB::tbbmalloc absl::synchronization)
                            target_include_directories(benchmarks PRIVATE ${CMAKE_SOURCE_DIR} / include / sync_tools)
                                target_compile_options(benchmarks PRIVATE - O3 - march = native - flto - Wall - Wextra -
                                                                                         Werror)
                                    target_link_options(benchmarks PRIVATE - flto)
