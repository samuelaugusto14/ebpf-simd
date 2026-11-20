add_bpf_program(basic-filter-bpf filter.c filter.h)

add_executable(basic-filter main.c)
target_compile_features(basic-filter PRIVATE c_std_99)
target_link_libraries(basic-filter PRIVATE ubpf)
add_dependencies(basic-filter basic-filter-bpf)
