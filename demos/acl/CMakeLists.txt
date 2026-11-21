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
    message(FATAL_ERROR "Cannot load raw ACL bytecode with relocations in ${OBJECT}: ${relocations}")
  endif()
  return()
endif()

if(NOT PLATFORM_LINUX OR NOT UBPF_COMPILER_HAS_MAVX2)
  message(STATUS "Skipping acl: the benchmark requires the Linux x86-64 AVX2 backend")
  return()
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(ACL_PCAP QUIET IMPORTED_TARGET libpcap)
endif()
find_package(Python3 QUIET COMPONENTS Interpreter)
find_program(UBPF_BPF_CLANG NAMES clang HINTS ${UBPF_ALTERNATE_LLVM_PATH})
find_program(ACL_LLVM_MC NAMES llvm-mc HINTS ${UBPF_ALTERNATE_LLVM_PATH})
find_program(ACL_LLVM_READELF NAMES llvm-readelf HINTS ${UBPF_ALTERNATE_LLVM_PATH})
find_program(ACL_LLVM_OBJCOPY NAMES llvm-objcopy HINTS ${UBPF_ALTERNATE_LLVM_PATH})

if(NOT ACL_PCAP_FOUND OR NOT Python3_Interpreter_FOUND OR NOT UBPF_BPF_CLANG
   OR NOT ACL_LLVM_MC OR NOT ACL_LLVM_READELF OR NOT ACL_LLVM_OBJCOPY)
  message(STATUS "Skipping acl: install the libpcap development files, pkg-config, Python 3, Clang and LLVM tools")
  return()
endif()

execute_process(
  COMMAND "${Python3_EXECUTABLE}" -c "import parcon"
  RESULT_VARIABLE acl_parcon_status
  OUTPUT_QUIET ERROR_QUIET
)
if(NOT acl_parcon_status STREQUAL "0")
  message(STATUS "Skipping acl: install the assembler requirements and select its interpreter with -DPython3_EXECUTABLE")
  return()
endif()

set(acl_src "${CMAKE_CURRENT_SOURCE_DIR}")
set(acl_out "${CMAKE_CURRENT_BINARY_DIR}")
set(acl_assembler "${PROJECT_SOURCE_DIR}/bin/ubpf-assembler")

add_custom_command(
  OUTPUT "${acl_out}/acl_serial.asm"
  COMMAND "${UBPF_BPF_CLANG}" --target=bpfel -mcpu=v3 -ffreestanding -fno-addrsig -O2
          -S "${acl_src}/acl_serial.bpf.c" -o "${acl_out}/acl_serial.asm"
  DEPENDS "${acl_src}/acl_serial.bpf.c" "${acl_src}/acl_context.h" "${CMAKE_CURRENT_LIST_FILE}"
  COMMENT "Compiling ACL to scalar eBPF assembly"
  VERBATIM
)
add_custom_command(
  OUTPUT "${acl_out}/acl_serial.o"
  COMMAND "${ACL_LLVM_MC}" -triple=bpfel -filetype=obj
          "${acl_out}/acl_serial.asm" -o "${acl_out}/acl_serial.o"
  DEPENDS "${acl_out}/acl_serial.asm"
  COMMENT "Assembling scalar ACL with LLVM"
  VERBATIM
)
add_custom_command(
  OUTPUT "${acl_out}/acl_serial.bin"
  COMMAND "${CMAKE_COMMAND}" "-DREADELF=${ACL_LLVM_READELF}" "-DOBJECT=${acl_out}/acl_serial.o"
          -P "${CMAKE_CURRENT_LIST_FILE}"
  COMMAND "${ACL_LLVM_OBJCOPY}" --only-section=.text -O binary
          "${acl_out}/acl_serial.o" "${acl_out}/acl_serial.bin"
  DEPENDS "${acl_out}/acl_serial.o" "${CMAKE_CURRENT_LIST_FILE}"
  COMMENT "Checking relocations and extracting scalar ACL bytecode"
  VERBATIM
)
add_custom_command(
  OUTPUT "${acl_out}/acl_simd.bin"
  COMMAND "${Python3_EXECUTABLE}" "${acl_assembler}" "${acl_src}/acl_simd.asm" "${acl_out}/acl_simd.bin"
  DEPENDS "${acl_src}/acl_simd.asm" "${acl_src}/acl_simd_context.h" "${acl_src}/acl_context.h" "${acl_assembler}"
          "${PROJECT_SOURCE_DIR}/ubpf/assembler.py" "${PROJECT_SOURCE_DIR}/ubpf/asm_parser.py"
  COMMENT "Assembling SIMD ACL with the uBPF assembler"
  VERBATIM
)
add_custom_target(acl_bytecode DEPENDS "${acl_out}/acl_serial.bin" "${acl_out}/acl_simd.bin")

add_executable(acl_benchmark main.c ../utils/trace_reader.c)
target_compile_features(acl_benchmark PRIVATE c_std_11)
target_include_directories(acl_benchmark PRIVATE ../utils)
target_compile_definitions(acl_benchmark PRIVATE ACL_BYTECODE_DIR="${acl_out}")
target_link_libraries(acl_benchmark PRIVATE ubpf PkgConfig::ACL_PCAP)
add_dependencies(acl_benchmark acl_bytecode)
