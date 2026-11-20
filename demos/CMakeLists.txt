function(add_bpf_program TARGET_NAME SOURCE_FILE)
  # Accept the compiler override used before Clang selection was shared by the demos.
  if(NOT UBPF_BPF_CLANG AND BASIC_FILTER_CLANG)
    set(UBPF_BPF_CLANG "${BASIC_FILTER_CLANG}")
  endif()
  find_program(UBPF_BPF_CLANG NAMES clang HINTS ${UBPF_ALTERNATE_LLVM_PATH})
  if(NOT UBPF_BPF_CLANG OR NOT EXISTS "${UBPF_BPF_CLANG}")
    message(FATAL_ERROR "${TARGET_NAME} requires Clang with BPF support. Set UBPF_BPF_CLANG to its path.")
  endif()

  get_filename_component(bpf_source "${SOURCE_FILE}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  get_filename_component(bpf_name "${SOURCE_FILE}" NAME_WLE)
  set(bpf_object "${CMAKE_CURRENT_BINARY_DIR}/${bpf_name}.o")

  add_custom_command(
    OUTPUT "${bpf_object}"
    COMMAND "${UBPF_BPF_CLANG}" --target=bpfel -mcpu=v1 -ffreestanding -O2
            -c "${bpf_source}" -o "${bpf_object}"
    DEPENDS "${bpf_source}" ${ARGN}
    COMMENT "Compiling ${SOURCE_FILE} to eBPF"
    VERBATIM
  )
  add_custom_target(${TARGET_NAME} ALL DEPENDS "${bpf_object}")
endfunction()

file(GLOB DEMO_ENTRIES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*")
foreach(DEMO_DIR IN LISTS DEMO_ENTRIES)
  if(NOT DEMO_DIR STREQUAL "build"
     AND IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${DEMO_DIR}"
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${DEMO_DIR}/CMakeLists.txt")
    string(MAKE_C_IDENTIFIER "UBPF_ENABLE_${DEMO_DIR}" OPT_NAME)
    string(TOUPPER "${OPT_NAME}" OPT_NAME)
    option(${OPT_NAME} "Build the ${DEMO_DIR} demo" ON)
    if(${OPT_NAME})
      set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${DEMO_DIR}")
      add_subdirectory("${DEMO_DIR}")
    endif()
  endif()
endforeach()
