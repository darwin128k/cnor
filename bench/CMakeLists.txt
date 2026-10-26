# Benchmarks for cnor: one plain C executable, no framework. Built only with
# CNOR_BUILD_BENCH=ON; meant for an optimised build (Release), see README.md.

add_executable(cnor_bench
        main.c
        clock.c
        workloads.c
        reference.c
)
target_link_libraries(cnor_bench PRIVATE cnor)

# The same programs, run by a tree-walker: the measurement behind "how much
# slower is an interpreter than the bytecode machine?". See tree/README.md.
add_executable(cnor_tree_bench
        tree/tree_bench.c
        tree/tree_run.c
        clock.c
        workloads.c
)
target_link_libraries(cnor_tree_bench PRIVATE cnor)
# The tree-walker uses the machine's own value helpers, so the two run on the
# same values.
target_include_directories(cnor_tree_bench PRIVATE ${CNOR_SRC_PATH})
