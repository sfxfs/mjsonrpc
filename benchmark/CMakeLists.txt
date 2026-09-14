# Performance benchmark suite. Build with:
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMJSONRPC_BUILD_BENCHMARKS=ON
#   cmake --build build
#   ./output/mjsonrpc-benchmark --json results.json --markdown summary.md --check
#
# Intentionally no project() call here: benchmark binaries must inherit the
# parent project version and settings.

add_executable(mjsonrpc-benchmark benchmark.c)

target_link_libraries(mjsonrpc-benchmark PRIVATE mjsonrpc)

find_package(Threads REQUIRED)
target_link_libraries(mjsonrpc-benchmark PRIVATE Threads::Threads)

if(NOT APPLE)
    # sqrt() used for sample standard deviation
    target_link_libraries(mjsonrpc-benchmark PRIVATE m)
endif()

target_compile_definitions(mjsonrpc-benchmark PRIVATE
    MJSONRPC_VERSION="${PROJECT_VERSION}"
    BENCH_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
)

# Benchmark numbers are only meaningful with optimizations enabled.
set(_bench_optimized_types Release RelWithDebInfo MinSizeRel)
if(NOT CMAKE_BUILD_TYPE IN_LIST _bench_optimized_types)
    message(WARNING
        "MJSONRPC_BUILD_BENCHMARKS is ON but CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}'. "
        "Benchmark results will not be meaningful; configure with "
        "-DCMAKE_BUILD_TYPE=Release.")
endif()
