#include "audio_i2s.h"
#include <audio_i2s.pio.h>
#include <hardware/pio.h>
#include <hardware/dma.h>
#include <hardware/clocks.h>
#include <errno.h>
#include <stdlib.h>

#define AUDIO_I2S_CHANNELS 2
#define AUDIO_I2S_BUFFER_COUNT 2
#define AUDIO_I2S_BITS_PER_BYTE 8
#define AUDIO_I2S_CLKDIV_FRAC_PART 256ULL

static uint32_t audio_i2s_compute_clkdiv(audio_i2s_t *i2s)
{
    const uint64_t sysclk_freq = clock_get_hz(clk_sys);
    const uint64_t bclk_freq = i2s->config->sample_rate * i2s->config->sample_size * AUDIO_I2S_BITS_PER_BYTE * AUDIO_I2S_CHANNELS;
    return (AUDIO_I2S_CLKDIV_FRAC_PART * sysclk_freq) / (2ULL * bclk_freq);
}

static void audio_i2s_sm_init(audio_i2s_t *i2s)
{
    i2s->sm = pio_claim_unused_sm(i2s->config->pio, true);
    i2s->sm_offset = pio_add_program(i2s->config->pio, &i2s_out_master_program);
    i2s_out_master_program_init(i2s->config->pio, i2s->sm, i2s->sm_offset, i2s->config->data_pin, i2s->config->clock_pin_base);
    const uint32_t divider = audio_i2s_compute_clkdiv(i2s);
    pio_sm_set_clkdiv_int_frac(i2s->config->pio, i2s->sm, divider >> 8U, divider & 0xFFU);
}

static void audio_i2s_sm_deinit(audio_i2s_t *i2s)
{
    pio_sm_clear_fifos(i2s->config->pio, i2s->sm);
    pio_sm_drain_tx_fifo(i2s->config->pio, i2s->sm);
    pio_remove_program(i2s->config->pio, &i2s_out_master_program, i2s->sm_offset);
    pio_clear_instruction_memory(i2s->config->pio);
    pio_sm_unclaim(i2s->config->pio, i2s->sm);
}

static void audio_i2s_dma_init(audio_i2s_t *i2s)
{
    /* Set up DMA for PIO I2S */
    i2s->dma_ctrl_ch = dma_claim_unused_channel(true);
    i2s->dma_data_ch = dma_claim_unused_channel(true);

    /* Set up control blocks */
    i2s->ctrl_blocks[0] = &i2s->pcm_buffer[0];
    i2s->ctrl_blocks[1] = &i2s->pcm_buffer[i2s->config->buffer_frames_count * AUDIO_I2S_CHANNELS];
    
    /* Configure control channel */
    dma_channel_config c = dma_channel_get_default_config(i2s->dma_ctrl_ch);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_ring(&c, false, 3);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    dma_channel_configure(i2s->dma_ctrl_ch, &c, &dma_hw->ch[i2s->dma_data_ch].al3_read_addr_trig, i2s->ctrl_blocks, 1, false);

    /* Configure data channel */
    c = dma_channel_get_default_config(i2s->dma_data_ch);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_chain_to(&c, i2s->dma_ctrl_ch);
    channel_config_set_dreq(&c, pio_get_dreq(i2s->config->pio, i2s->sm, true));
    dma_channel_configure(i2s->dma_data_ch, &c, &i2s->config->pio->txf[i2s->sm], NULL, i2s->config->buffer_frames_count, false);

    /* Configure DMA interrupt */
    dma_channel_set_irq0_enabled(i2s->dma_data_ch, true);
    irq_set_exclusive_handler(DMA_IRQ_0, i2s->config->dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    /* Start DMA */
    dma_channel_start(i2s->dma_ctrl_ch);
}

static void audio_i2s_dma_deinit(audio_i2s_t *i2s)
{
    /* Cleanup DMA */
    dma_channel_cleanup(i2s->dma_data_ch);
    dma_channel_cleanup(i2s->dma_ctrl_ch);

    /* Disable DMA interrupt */
    irq_set_enabled(DMA_IRQ_0, false);

    /* Free used DMA channels */
    dma_channel_unclaim(i2s->dma_data_ch);
    dma_channel_unclaim(i2s->dma_ctrl_ch);
}

int audio_i2s_init(audio_i2s_t *i2s)
{   
    // TODO lots of sanity checks
    pio_gpio_init(i2s->config->pio, i2s->config->data_pin);
    pio_gpio_init(i2s->config->pio, i2s->config->clock_pin_base);
    pio_gpio_init(i2s->config->pio, i2s->config->clock_pin_base + 1);
    const size_t buffer_size = i2s->config->buffer_frames_count * AUDIO_I2S_CHANNELS * i2s->config->sample_size;
    i2s->pcm_buffer = calloc(1,  buffer_size * AUDIO_I2S_BUFFER_COUNT);
    if (i2s->pcm_buffer == NULL) {
        return -ENOMEM;
    }

    audio_i2s_sm_init(i2s);
    audio_i2s_dma_init(i2s);
    
    return 0;
}

void audio_i2s_deinit(audio_i2s_t *i2s)
{
    audio_i2s_enable(i2s, false);
    audio_i2s_dma_deinit(i2s);
    audio_i2s_sm_deinit(i2s);

    free(i2s->pcm_buffer);
}

void audio_i2s_enable(audio_i2s_t *i2s, bool enabled)
{
    pio_sm_set_enabled(i2s->config->pio, i2s->sm, enabled);
}

void audio_i2s_clear_dma_irq(audio_i2s_t *i2s)
{
    dma_hw->ints0 = (1U << i2s->dma_data_ch);
}

int16_t *audio_i2s_get_next_buffer(audio_i2s_t *i2s)
{
    /* Return address set to be sent as next via control DMA */
    return *(int16_t **)dma_hw->ch[i2s->dma_ctrl_ch].read_addr;
}
