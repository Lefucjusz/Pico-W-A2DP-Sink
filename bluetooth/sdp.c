#include "sdp.h"
#include <btstack.h>

typedef struct 
{
    uint8_t sdp_avdtp_sink_service_buffer[150];
    uint8_t sdp_avrcp_target_service_buffer[150];
    uint8_t sdp_avrcp_controller_service_buffer[200];
    uint8_t sdp_device_id_service_buffer[100];
} bt_sdp_ctx_t;

static bt_sdp_ctx_t ctx;

static void bt_sdp_register_avdtp_sink(void)
{
    const uint32_t handle = sdp_create_service_record_handle();
    a2dp_sink_create_sdp_record(ctx.sdp_avdtp_sink_service_buffer, handle, AVDTP_SINK_FEATURE_MASK_SPEAKER, NULL, NULL);
    sdp_register_service(ctx.sdp_avdtp_sink_service_buffer);
}

static void bt_sdp_register_avrcp_controller(void)
{
    const uint16_t supported_features = (1 << AVRCP_CONTROLLER_SUPPORTED_FEATURE_CATEGORY_PLAYER_OR_RECORDER);
    const uint32_t handle = sdp_create_service_record_handle();
    avrcp_controller_create_sdp_record(ctx.sdp_avrcp_controller_service_buffer, handle, supported_features, NULL, NULL);
    sdp_register_service(ctx.sdp_avrcp_controller_service_buffer);
}

static void bt_sdp_register_avrcp_target(void)
{
    const uint16_t supported_features = (1 << AVRCP_TARGET_SUPPORTED_FEATURE_CATEGORY_MONITOR_OR_AMPLIFIER);
    const uint32_t handle = sdp_create_service_record_handle();
    avrcp_target_create_sdp_record(ctx.sdp_avrcp_target_service_buffer, handle, supported_features, NULL, NULL);
    sdp_register_service(ctx.sdp_avrcp_target_service_buffer);
}

static void bt_sdp_register_device_id(void)
{
    const uint32_t handle = sdp_create_service_record_handle();
    device_id_create_sdp_record(ctx.sdp_device_id_service_buffer, handle, DEVICE_ID_VENDOR_ID_SOURCE_BLUETOOTH, BLUETOOTH_COMPANY_ID_INFINEON_TECHNOLOGIES_AG, 0x0001, 0x0001);
    sdp_register_service(ctx.sdp_device_id_service_buffer);
}

void bt_sdp_init(void)
{
    sdp_init();
    
    bt_sdp_register_avdtp_sink();
    bt_sdp_register_avrcp_controller();
    bt_sdp_register_avrcp_target();
    bt_sdp_register_device_id();
}
