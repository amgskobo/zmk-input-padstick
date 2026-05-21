if(CONFIG_ZMK_INPUT_PROCESSOR_PADSTICK)
    zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)
    add_subdirectory(src)
endif()
