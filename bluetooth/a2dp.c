#include "a2dp.h"
#include "avrcp.h"
#include "bt_i2s.h"
#include <btstack.h>
#include <btstack_resample.h>
#include <classic/a2dp_sink.h>
#include <pico/cyw43_arch.h>

#define BT_A2DP_SBC_HEADER_SIZE 12 // Without CRC
#define BT_A2DP_MEDIA_HEADER_SIZE 12 // Without CRC

#define BT_A2DP_RESAMPLING_FACTOR_NOMINAL 0x10000 // Fixed-point 2^16
#define BT_A2DP_RESAMPLING_COMPENSATION 0x00100

#define BT_A2DP_MAX_SBC_FRAME_SIZE 120

#define BT_A2DP_SBC_FRAMES_COUNT_MIN 60
#define BT_A2DP_SBC_FRAMES_COUNT_MAX 120
#define BT_A2DP_SBC_ADDITIONAL_FRAMES 30

#define BT_A2DP_SAMPLE_SIZE sizeof(int16_t) 
#define BT_A2DP_CHANNELS_PER_FRAME 2
#define BT_A2DP_BYTES_PER_FRAME (BT_A2DP_SAMPLE_SIZE * BT_A2DP_CHANNELS_PER_FRAME)
#define BT_A2DP_AUDIO_STORAGE_FRAMES (128 + 16)

#define BT_A2DP_SBC_FRAME_STORAGE_SIZE ((BT_A2DP_SBC_FRAMES_COUNT_MAX + BT_A2DP_SBC_ADDITIONAL_FRAMES) * BT_A2DP_MAX_SBC_FRAME_SIZE)
#define BT_A2DP_DECODED_AUDIO_STORAGE_SIZE (BT_A2DP_AUDIO_STORAGE_FRAMES * BT_A2DP_BYTES_PER_FRAME)

typedef struct 
{
    uint8_t reconfigure;
    uint8_t num_channels;
    uint16_t sampling_frequency;
    uint8_t block_length;
    uint8_t subbands;
    uint8_t min_bitpool_value;
    uint8_t max_bitpool_value;
    btstack_sbc_channel_mode_t channel_mode;
    btstack_sbc_allocation_method_t allocation_method;
} sbc_configuration_t;

typedef struct
{
    uint8_t codec_config[4];
    uint8_t seid;
    sbc_configuration_t sbc_config;
    uint32_t sbc_frame_size;
    btstack_ring_buffer_t sbc_frame_ring_buffer;
    uint8_t sbc_frame_storage[BT_A2DP_SBC_FRAME_STORAGE_SIZE];
    btstack_ring_buffer_t decoded_audio_ring_buffer;
    uint8_t decoded_audio_storage[BT_A2DP_DECODED_AUDIO_STORAGE_SIZE];
    btstack_resample_t resampler;
    btstack_sbc_decoder_state_t sbc_decoder;
    int16_t *request_buffer;
    uint32_t request_frames;
    bool stream_started;
    bool media_initialized;
} bt_a2dp_ctx_t;

static bt_a2dp_ctx_t ctx;

/* All configurations with bitpool 2-53 are supported */
static const uint8_t sbc_capabilities[] = {
    0xFF, 0xFF, 2, 53
};

static void bt_a2dp_sbc_decoder_callback(int16_t *data, int num_frames, int num_channels, int sample_rate, void *context)
{
    /* Resample new frames */
    uint8_t output_buffer[BT_A2DP_DECODED_AUDIO_STORAGE_SIZE];
    const uint32_t resampled_frames = btstack_resample_block(&ctx.resampler, data, num_frames, (int16_t *)output_buffer);

    /* Store resampled frames in pending request buffer */
    const uint32_t frames_to_copy = btstack_min(resampled_frames, ctx.request_frames);
    const uint32_t bytes_to_copy = frames_to_copy * BT_A2DP_BYTES_PER_FRAME;
    if (frames_to_copy > 0) {
        memcpy(ctx.request_buffer, output_buffer, bytes_to_copy);
        ctx.request_frames -= frames_to_copy;
        ctx.request_buffer += frames_to_copy * BT_A2DP_CHANNELS_PER_FRAME;
    }

    /* Store remaining frames in ring buffer */
    const uint32_t frames_to_store = resampled_frames - frames_to_copy;
    if (frames_to_store > 0) {
        const uint32_t bytes_to_store = frames_to_store * BT_A2DP_BYTES_PER_FRAME;
        btstack_ring_buffer_write(&ctx.decoded_audio_ring_buffer, &output_buffer[bytes_to_copy], bytes_to_store);
    }
}

static void bt_a2dp_read_samples_callback(int16_t *buffer, uint16_t num_frames)
{
    if (ctx.sbc_frame_size == 0) {
        memset(buffer, 0, num_frames * BT_A2DP_CHANNELS_PER_FRAME);
        return;
    }

    /* Fill from already decoded audio */
    const uint32_t bytes_to_read = num_frames * BT_A2DP_BYTES_PER_FRAME;
    uint32_t bytes_read;
    btstack_ring_buffer_read(&ctx.decoded_audio_ring_buffer, (uint8_t *)buffer, bytes_to_read, &bytes_read);
    
    /* Update variables */
    const uint32_t samples_read = bytes_read / BT_A2DP_CHANNELS_PER_FRAME;
    const uint32_t frames_read = bytes_read / BT_A2DP_BYTES_PER_FRAME;
    ctx.request_buffer = &buffer[samples_read];
    ctx.request_frames = num_frames - frames_read;

    /* Start decoding new SBC frames, the remaining PCM frames from this request will be filled in SBC decoder callback */
    uint8_t sbc_frame[BT_A2DP_MAX_SBC_FRAME_SIZE];
    while ((ctx.request_frames > 0) && (btstack_ring_buffer_bytes_available(&ctx.sbc_frame_ring_buffer) >= ctx.sbc_frame_size)) {
        btstack_ring_buffer_read(&ctx.sbc_frame_ring_buffer, sbc_frame, ctx.sbc_frame_size, &bytes_read);
        btstack_sbc_decoder_process_data(&ctx.sbc_decoder, 0, sbc_frame, ctx.sbc_frame_size);
    }
}

static void bt_a2dp_media_processing_init(const sbc_configuration_t *config)
{
    if (ctx.media_initialized) {
        return;
    }

    btstack_sbc_decoder_init(&ctx.sbc_decoder, SBC_MODE_STANDARD, bt_a2dp_sbc_decoder_callback, NULL);
    btstack_ring_buffer_init(&ctx.sbc_frame_ring_buffer, ctx.sbc_frame_storage, sizeof(ctx.sbc_frame_storage));
    btstack_ring_buffer_init(&ctx.decoded_audio_ring_buffer, ctx.decoded_audio_storage, sizeof(ctx.decoded_audio_storage));
    btstack_resample_init(&ctx.resampler, config->num_channels);

    const btstack_audio_sink_t *audio = btstack_audio_sink_get_instance();
    if (audio != NULL) {
        audio->init(config->num_channels, config->sampling_frequency, bt_a2dp_read_samples_callback);
    } 

    ctx.stream_started = false;
    ctx.media_initialized = true;
}

static void bt_a2dp_media_processing_start(void)
{
    if (!ctx.media_initialized) {
        return;
    }

    const btstack_audio_sink_t *audio = btstack_audio_sink_get_instance();
    if (audio != NULL) {
        audio->start_stream();
    }

    ctx.stream_started = true;
}

static void bt_a2dp_media_processing_pause(void)
{
    if (!ctx.media_initialized) {
        return;
    }

    ctx.stream_started = false;
    
    const btstack_audio_sink_t *audio = btstack_audio_sink_get_instance();
    if (audio != NULL) {
        audio->stop_stream();
    }

    btstack_ring_buffer_reset(&ctx.decoded_audio_ring_buffer);
    btstack_ring_buffer_reset(&ctx.sbc_frame_ring_buffer);
}

static void bt_a2dp_media_processing_close(void)
{
    if (!ctx.media_initialized) {
        return;
    }

    ctx.media_initialized = false;
    ctx.stream_started = false;
    ctx.sbc_frame_size = 0;

    const btstack_audio_sink_t *audio = btstack_audio_sink_get_instance();
    if (audio != NULL) {
        audio->close();
    }
}

static btstack_sbc_channel_mode_t bt_a2dp_avdtp_to_sbc_channel_mode(uint8_t channel_mode)
{
    switch (channel_mode) {
        case AVDTP_CHANNEL_MODE_MONO:
            return SBC_CHANNEL_MODE_MONO;

        case AVDTP_CHANNEL_MODE_DUAL_CHANNEL:
            return SBC_CHANNEL_MODE_DUAL_CHANNEL;

        case AVDTP_CHANNEL_MODE_STEREO:
            return SBC_CHANNEL_MODE_STEREO;

        case AVDTP_CHANNEL_MODE_JOINT_STEREO:
            return SBC_CHANNEL_MODE_JOINT_STEREO;

        default:
            return SBC_CHANNEL_MODE_STEREO;
    }
}

static void bt_a2dp_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    if ((packet_type != HCI_EVENT_PACKET) || (hci_event_packet_get_type(packet) != HCI_EVENT_A2DP_META)) {
        return;
    }

    uint8_t allocation_method;
    uint8_t channel_mode;
    uint8_t status;

    const uint8_t event = hci_event_a2dp_meta_get_subevent_code(packet);
    switch (event) {
        case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION:
            ctx.sbc_config.reconfigure = a2dp_subevent_signaling_media_codec_sbc_configuration_get_reconfigure(packet);
            ctx.sbc_config.num_channels = a2dp_subevent_signaling_media_codec_sbc_configuration_get_num_channels(packet);
            ctx.sbc_config.sampling_frequency = a2dp_subevent_signaling_media_codec_sbc_configuration_get_sampling_frequency(packet);
            ctx.sbc_config.block_length = a2dp_subevent_signaling_media_codec_sbc_configuration_get_block_length(packet);
            ctx.sbc_config.subbands = a2dp_subevent_signaling_media_codec_sbc_configuration_get_subbands(packet);
            ctx.sbc_config.min_bitpool_value = a2dp_subevent_signaling_media_codec_sbc_configuration_get_min_bitpool_value(packet);
            ctx.sbc_config.max_bitpool_value = a2dp_subevent_signaling_media_codec_sbc_configuration_get_max_bitpool_value(packet);

            /* Convert Bluetooth spec definitions to SBC encoder expected inputs */
            allocation_method = a2dp_subevent_signaling_media_codec_sbc_configuration_get_allocation_method(packet);
            channel_mode = a2dp_subevent_signaling_media_codec_sbc_configuration_get_channel_mode(packet);
            ctx.sbc_config.allocation_method = (btstack_sbc_allocation_method_t)(allocation_method - 1);
            ctx.sbc_config.channel_mode = bt_a2dp_avdtp_to_sbc_channel_mode(channel_mode);

            break;
        
        case A2DP_SUBEVENT_STREAM_ESTABLISHED:
            status = a2dp_subevent_stream_established_get_status(packet);
            if (status != ERROR_CODE_SUCCESS) {
                break;
            }

            ctx.seid = a2dp_subevent_stream_established_get_local_seid(packet);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true); // Indicate that the stream has been established

            break;

        case A2DP_SUBEVENT_STREAM_STARTED:
            if (ctx.sbc_config.reconfigure) {
                bt_a2dp_media_processing_close();
            }
            bt_a2dp_media_processing_init(&ctx.sbc_config);
            break;

        case A2DP_SUBEVENT_STREAM_SUSPENDED:
            bt_a2dp_media_processing_pause();
            break;

        case A2DP_SUBEVENT_STREAM_RELEASED:
            bt_a2dp_media_processing_close();
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false); // Indicate that the stream has been released
            break;

        default:
            break;
    }
}

static bool bt_a2dp_read_sbc_header(uint8_t *packet, uint16_t size, uint32_t *offset, avdtp_sbc_codec_header_t *header)
{
    uint32_t pos = *offset;

    if ((size - pos) < BT_A2DP_SBC_HEADER_SIZE) {
        return false;
    }

    header->fragmentation = get_bit16(packet[pos], 7);
    header->starting_packet = get_bit16(packet[pos], 6);
    header->last_packet = get_bit16(packet[pos], 5);
    header->num_frames = packet[pos] & 0x0F;
    pos++;

    *offset = pos;

    return true;
}

static bool bt_a2dp_read_media_header(uint8_t *packet, uint16_t size, uint32_t *offset, avdtp_media_packet_header_t *header)
{
    uint32_t pos = *offset;

    if ((size - pos) < BT_A2DP_MEDIA_HEADER_SIZE) {
        return false;
    }

    header->version = packet[pos] & 0x03;
    header->padding = get_bit16(packet[pos], 2);
    header->extension = get_bit16(packet[pos], 3);
    header->csrc_count = (packet[pos] >> 4) & 0x0F;
    pos++;

    header->marker = get_bit16(packet[pos], 0);
    header->payload_type = (packet[pos] >> 1) & 0x7F;
    pos++;

    header->sequence_number = big_endian_read_16(packet, pos);
    pos += 2;

    header->timestamp = big_endian_read_32(packet, pos);
    pos += 4;

    header->synchronization_source = big_endian_read_32(packet, pos);
    pos += 4;

    *offset = pos;

    return true;
}

static void bt_a2dp_media_handler(uint8_t seid, uint8_t *packet, uint16_t size)
{
    uint32_t offset = 0;

    avdtp_media_packet_header_t media_header;
    if (!bt_a2dp_read_media_header(packet, size, &offset, &media_header)) {
        return;
    }

    avdtp_sbc_codec_header_t sbc_header;
    if (!bt_a2dp_read_sbc_header(packet, size, &offset, &sbc_header)) {
        return;
    }

    const uint32_t packet_length = size - offset;
    ctx.sbc_frame_size = packet_length / sbc_header.num_frames;
    btstack_ring_buffer_write(&ctx.sbc_frame_ring_buffer, &packet[offset], packet_length);
  
    const uint32_t frames_in_buffer = btstack_ring_buffer_bytes_available(&ctx.sbc_frame_ring_buffer) / ctx.sbc_frame_size;

    /* Decide on audio sync drift based on number of SBC frames in queue */
    uint32_t resampling_factor = BT_A2DP_RESAMPLING_FACTOR_NOMINAL;
    if (frames_in_buffer < BT_A2DP_SBC_FRAMES_COUNT_MIN) {
        resampling_factor -= BT_A2DP_RESAMPLING_COMPENSATION; // Stretch samples
    }
    else if (frames_in_buffer > BT_A2DP_SBC_FRAMES_COUNT_MAX) {
        resampling_factor += BT_A2DP_RESAMPLING_COMPENSATION; // Compress samples
    }
    btstack_resample_set_factor(&ctx.resampler, resampling_factor);

    /* Start stream if not started yet and enough frames buffered */
    if (!ctx.stream_started && (frames_in_buffer >= BT_A2DP_SBC_FRAMES_COUNT_MIN)) {
        bt_a2dp_media_processing_start();
    }
}

void bt_a2dp_init(void)
{
    const btstack_audio_sink_t *inst = bt_i2s_get_instance();
    btstack_audio_sink_set_instance(inst);

    a2dp_sink_init();

    a2dp_sink_register_packet_handler(bt_a2dp_packet_handler);
    a2dp_sink_register_media_handler(bt_a2dp_media_handler);
    
    avdtp_stream_endpoint_t *ep = a2dp_sink_create_stream_endpoint(AVDTP_AUDIO, AVDTP_CODEC_SBC, 
                                                                   sbc_capabilities, sizeof(sbc_capabilities), 
                                                                   ctx.codec_config, sizeof(ctx.codec_config));
    ctx.seid = avdtp_local_seid(ep);
}
