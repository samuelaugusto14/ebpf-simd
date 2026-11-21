# SPDX-License-Identifier: Apache-2.0

# The scalar bytecode build invokes this file with cmake -P after assembling the ELF object.
if(DEFINED CMAKE_SCRIPT_MODE_FILE)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env LC_ALL=C "${READELF}" --relocations "${OBJECT}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE relocations
    ERROR_VARIABLE error
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  string(STRIP "${relocations}" relocations)
  if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Could not read relocations in ${OBJECT}: ${error}")
  endif()
  if(NOT relocations STREQUAL "There are no relocations in this file.")
    message(FATAL_ERROR "Cannot load raw CMS bytecode with relocations in ${OBJECT}: ${relocations}")
  endif()
  return()
endif()

if(NOT PLATFORM_LINUX OR NOT UBPF_COMPILER_HAS_MAVX2)
  message(STATUS "Skipping cms: the benchmark requires the Linux x86-64 AVX2 backend")
  return()
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(CMS_PCAP QUIET IMPORTED_TARGET libpcap)
endif()
find_package(Python3 QUIET COMPONENTS Interpreter)
find_program(UBPF_BPF_CLANG NAMES clang HINTS ${UBPF_ALTERNATE_LLVM_PATH})
find_program(CMS_LLVM_MC NAMES llvm-mc HINTS ${UBPF_ALTERNATE_LLVM_PATH})
find_program(CMS_LLVM_READELF NAMES llvm-readelf HINTS ${UBPF_ALTERNATE_LLVM_PATH})
find_program(CMS_LLVM_OBJCOPY NAMES llvm-objcopy HINTS ${UBPF_ALTERNATE_LLVM_PATH})

if(NOT CMS_PCAP_FOUND OR NOT Python3_Interpreter_FOUND OR NOT UBPF_BPF_CLANG
   OR NOT CMS_LLVM_MC OR NOT CMS_LLVM_READELF OR NOT CMS_LLVM_OBJCOPY)
  message(STATUS "Skipping cms: install the libpcap development files, pkg-config, Python 3, Clang and LLVM tools")
  return()
endif()

execute_process(
  COMMAND "${Python3_EXECUTABLE}" -c "import parcon"
  RESULT_VARIABLE cms_parcon_status
  OUTPUT_QUIET ERROR_QUIET
)
if(NOT cms_parcon_status STREQUAL "0")
  message(STATUS "Skipping cms: install the assembler requirements and select its interpreter with -DPython3_EXECUTABLE")
  return()
endif()

set(cms_src "${CMAKE_CURRENT_SOURCE_DIR}")
set(cms_out "${CMAKE_CURRENT_BINARY_DIR}")
set(cms_assembler "${PROJECT_SOURCE_DIR}/bin/ubpf-assembler")

add_custom_command(
  OUTPUT "${cms_out}/cms_serial.asm"
  COMMAND "${UBPF_BPF_CLANG}" --target=bpfel -mcpu=v3 -ffreestanding -fno-addrsig -O2
          -S "${cms_src}/cms_serial.bpf.c" -o "${cms_out}/cms_serial.asm"
  DEPENDS "${cms_src}/cms_serial.bpf.c" "${cms_src}/cms_context.h" "${CMAKE_CURRENT_LIST_FILE}"
  COMMENT "Compiling CMS to scalar eBPF assembly"
  VERBATIM
)
add_custom_command(
  OUTPUT "${cms_out}/cms_serial.o"
  COMMAND "${CMS_LLVM_MC}" -triple=bpfel -filetype=obj
          "${cms_out}/cms_serial.asm" -o "${cms_out}/cms_serial.o"
  DEPENDS "${cms_out}/cms_serial.asm"
  COMMENT "Assembling scalar CMS with LLVM"
  VERBATIM
)
add_custom_command(
  OUTPUT "${cms_out}/cms_serial.bin"
  COMMAND "${CMAKE_COMMAND}" "-DREADELF=${CMS_LLVM_READELF}" "-DOBJECT=${cms_out}/cms_serial.o"
          -P "${CMAKE_CURRENT_LIST_FILE}"
  COMMAND "${CMS_LLVM_OBJCOPY}" --only-section=.text -O binary
          "${cms_out}/cms_serial.o" "${cms_out}/cms_serial.bin"
  DEPENDS "${cms_out}/cms_serial.o" "${CMAKE_CURRENT_LIST_FILE}"
  COMMENT "Checking relocations and extracting scalar CMS bytecode"
  VERBATIM
)
add_custom_command(
  OUTPUT "${cms_out}/cms_simd.bin"
  COMMAND "${Python3_EXECUTABLE}" "${cms_assembler}" "${cms_src}/cms_simd.asm" "${cms_out}/cms_simd.bin"
  DEPENDS "${cms_src}/cms_simd.asm" "${cms_src}/cms_context.h" "${cms_assembler}"
          "${PROJECT_SOURCE_DIR}/ubpf/assembler.py" "${PROJECT_SOURCE_DIR}/ubpf/asm_parser.py"
  COMMENT "Assembling SIMD CMS with the uBPF assembler"
  VERBATIM
)
add_custom_target(cms_bytecode DEPENDS "${cms_out}/cms_serial.bin" "${cms_out}/cms_simd.bin")

add_executable(cms_benchmark main.c ../utils/trace_reader.c)
target_compile_features(cms_benchmark PRIVATE c_std_11)
target_include_directories(cms_benchmark PRIVATE ../utils)
target_compile_definitions(cms_benchmark PRIVATE CMS_BYTECODE_DIR="${cms_out}")
target_link_libraries(cms_benchmark PRIVATE ubpf PkgConfig::CMS_PCAP)
add_dependencies(cms_benchmark cms_bytecode)
