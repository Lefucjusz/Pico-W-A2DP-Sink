#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <pico/types.h>
#include <hardware/pio.h>

#define ALIGN(var, size) __attribute__((aligned(size))) var

/* I2S peripheral configuration structure */
typedef struct audio_i2s_config 
{
    PIO pio;
    uint8_t data_pin;
    uint8_t clock_pin_base;
    uint32_t sample_rate;
    uint8_t sample_size;
    uint32_t buffer_frames_count;
    void (*dma_handler)(void);
} audio_i2s_config_t;

typedef struct audio_i2s_pio_ctx
{
    uint8_t sm;
    uint8_t sm_offset;
    uint dma_ctrl_ch;
    uint dma_data_ch;
    ALIGN(int16_t *ctrl_blocks[2], 8);
    int16_t *pcm_buffer;
    const audio_i2s_config_t *config;
} audio_i2s_t;

int audio_i2s_init(audio_i2s_t *i2s);
void audio_i2s_deinit(audio_i2s_t *i2s);
void audio_i2s_enable(audio_i2s_t *i2s, bool enabled);

void audio_i2s_clear_dma_irq(audio_i2s_t *i2s);
int16_t *audio_i2s_get_next_buffer(audio_i2s_t *i2s);
