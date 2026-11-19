option(UBPF_ENABLE_BASIC_FILTER "Build the basic-filter demo" OFF)

if(UBPF_ENABLE_BASIC_FILTER)
  add_subdirectory(basic-filter)
endif()
