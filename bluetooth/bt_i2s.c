#include "bt_i2s.h"
#include <audio_i2s.h>
#include <math.h>
#include <btstack.h>

#define BT_I2S_VOLUME_MAX 127 // Max value according to AVRCP spec

#define BT_I2S_TASK_INTERVAL_MS 5
#define BT_I2S_FRAMES_PER_BUFFER 1024

#define BT_I2S_CLAMP(val, min, max) MIN(max, MAX(val, min))

typedef void (*bt_i2s_samples_callback_t)(int16_t *buffer, uint16_t samples_count);

typedef struct 
{
    bt_i2s_samples_callback_t samples_callback;
    audio_i2s_t i2s;
    audio_i2s_config_t i2s_config;
    float volume;
    volatile bool buffer_request;
    btstack_timer_source_t task_timer;
} bt_i2s_ctx_t;

static bt_i2s_ctx_t ctx;

static void bt_i2s_dma_callback(void)
{
    ctx.buffer_request = true;
    audio_i2s_clear_dma_irq(&ctx.i2s);
}

static void bt_i2s_scale_volume(int16_t *buffer, size_t frames_count)
{
    for (size_t i = 0; i < frames_count; ++i) {
        buffer[2 * i] *= ctx.volume;
        buffer[2 * i + 1] *= ctx.volume;
    }
}

static void bt_i2s_fill_next_buffer(void)
{
    int16_t *buffer = audio_i2s_get_next_buffer(&ctx.i2s);
    ctx.samples_callback(buffer, BT_I2S_FRAMES_PER_BUFFER);

    // TODO duplicate samples for mono

    bt_i2s_scale_volume(buffer, BT_I2S_FRAMES_PER_BUFFER);
}

static void bt_i2s_task(btstack_timer_source_t *ts)
{
    if (ctx.buffer_request) {
        bt_i2s_fill_next_buffer();
        ctx.buffer_request = false;
    }

    /* Restart timer */
    btstack_run_loop_set_timer(ts, BT_I2S_TASK_INTERVAL_MS);
    btstack_run_loop_add_timer(ts);
}

static int bt_i2s_audio_init(uint8_t channels, uint32_t sample_rate, bt_i2s_samples_callback_t samples_callback)
{
    ctx.samples_callback = samples_callback;
    ctx.buffer_request = false;
    
    ctx.i2s_config.pio = pio0;
    ctx.i2s_config.data_pin = 28;
    ctx.i2s_config.clock_pin_base = 26;
    ctx.i2s_config.sample_size = sizeof(int16_t);
    ctx.i2s_config.buffer_frames_count = BT_I2S_FRAMES_PER_BUFFER;
    ctx.i2s_config.dma_handler = bt_i2s_dma_callback;
    ctx.i2s_config.sample_rate = sample_rate;

    ctx.i2s.config = &ctx.i2s_config;
    audio_i2s_init(&ctx.i2s);

    return 0;
}

static void bt_i2s_start_stream(void)
{
    bt_i2s_fill_next_buffer();

    /* Create timer that will repeatedly call I2S task function */
    btstack_run_loop_set_timer_handler(&ctx.task_timer, bt_i2s_task);
    btstack_run_loop_set_timer(&ctx.task_timer, BT_I2S_TASK_INTERVAL_MS);
    btstack_run_loop_add_timer(&ctx.task_timer);

    /* Start playback */
    audio_i2s_enable(&ctx.i2s, true);
}

static void bt_i2s_stop_stream(void)
{
    audio_i2s_enable(&ctx.i2s, false);
    btstack_run_loop_remove_timer(&ctx.task_timer);
}

static void bt_i2s_close(void)
{
    bt_i2s_stop_stream();
    audio_i2s_deinit(&ctx.i2s);
}

static void bt_i2s_audio_set_volume(uint8_t volume)
{
    if (volume == 0) {
        ctx.volume = 0.0f;
        return;
    }

    const float volume_normalized = volume / (float)BT_I2S_VOLUME_MAX;
    const float a = 1e-3f;
    const float b = 6.908f;
    ctx.volume = a * expf(b * volume_normalized);
	ctx.volume = BT_I2S_CLAMP(ctx.volume, 0.0f, 1.0f);
}

static const btstack_audio_sink_t bt_i2s_sink = {
    .init = bt_i2s_audio_init,
    .start_stream = bt_i2s_start_stream,
    .stop_stream = bt_i2s_stop_stream,
    .close = bt_i2s_close,
    .set_volume = bt_i2s_audio_set_volume
};

const btstack_audio_sink_t *bt_i2s_get_instance(void)
{
    return &bt_i2s_sink;
}
