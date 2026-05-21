if(CONFIG_ZMK_INPUT_PROCESSOR_PADSTICK)
    add_subdirectory(src)
    zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)
endif()
