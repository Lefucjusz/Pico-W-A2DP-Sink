add_library(audio_i2s INTERFACE)

pico_generate_pio_header(audio_i2s 
    ${CMAKE_CURRENT_LIST_DIR}/audio_i2s.pio
)

target_sources(audio_i2s 
    INTERFACE
        audio_i2s.c
)

target_include_directories(audio_i2s 
    INTERFACE 
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(audio_i2s
    INTERFACE
        pico_stdlib
        hardware_pio 
        hardware_dma
)
