add_executable(basic-simd-example main.c)
target_compile_features(basic-simd-example PRIVATE c_std_11)
target_link_libraries(basic-simd-example PRIVATE ubpf)
