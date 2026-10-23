# Benchmarks for cnor: one plain C executable, no framework. Built only with
# CNOR_BUILD_BENCH=ON; meant for an optimised build (Release), see README.md.

add_executable(cnor_bench
        main.c
        clock.c
        workloads.c
        reference.c
)
target_link_libraries(cnor_bench PRIVATE cnor)
