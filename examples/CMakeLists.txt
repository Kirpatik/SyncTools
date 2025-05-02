add_executable(quick_start quick_start.cpp)
target_include_directories(quick_start PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(mpsc_queue mpsc_queue.cpp)
target_include_directories(mpsc_queue PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(mutex mutex.cpp)
target_include_directories(mutex PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(object_pool object_pool.cpp)
target_include_directories(object_pool PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(thread_cached_object_pool thread_cached_object_pool.cpp)
target_include_directories(thread_cached_object_pool PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(spsc_queue spsc_queue.cpp)
target_include_directories(spsc_queue PRIVATE ${CMAKE_SOURCE_DIR}/include)