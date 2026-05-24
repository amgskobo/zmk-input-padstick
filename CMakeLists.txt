if(CONFIG_ZMK_INPUT_PROCESSOR_PADSTICK)
    add_subdirectory(src)
    # drivers/input_processor.h is provided by the ZMK application include tree.
    zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)
endif()
