if(ANDROID)
  add_compiler_rt_script(xsan_device_setup)
  add_dependencies(xsan xsan_device_setup)
endif()
