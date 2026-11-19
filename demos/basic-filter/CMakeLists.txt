find_program(BASIC_FILTER_CLANG NAMES clang HINTS ${UBPF_ALTERNATE_LLVM_PATH})
if(NOT BASIC_FILTER_CLANG)
  message(FATAL_ERROR "basic-filter requires Clang with BPF support. Set BASIC_FILTER_CLANG to its path.")
endif()

set(filter_object "${CMAKE_CURRENT_BINARY_DIR}/filter.o")
add_custom_command(
  OUTPUT "${filter_object}"
  COMMAND "${BASIC_FILTER_CLANG}" --target=bpfel -mcpu=v1 -ffreestanding -O2
          -c "${CMAKE_CURRENT_SOURCE_DIR}/filter.c" -o "${filter_object}"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/filter.c" "${CMAKE_CURRENT_SOURCE_DIR}/filter.h"
  COMMENT "Compiling the basic-filter eBPF program"
  VERBATIM
)

add_custom_target(basic-filter-bpf DEPENDS "${filter_object}")

add_executable(basic-filter main.c)
target_compile_features(basic-filter PRIVATE c_std_99)
target_link_libraries(basic-filter PRIVATE ubpf)
add_dependencies(basic-filter basic-filter-bpf)
