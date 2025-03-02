add_library(bluetooth INTERFACE)

target_sources(bluetooth
    INTERFACE
        a2dp.c
        avrcp.c
        bt.c
        sdp.c
        bt_i2s.c
)

target_include_directories(bluetooth
    INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(bluetooth
    INTERFACE
        pico_btstack_classic
        pico_btstack_cyw43
        pico_btstack_sbc_decoder
        audio_i2s
)
