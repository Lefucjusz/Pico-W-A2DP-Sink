#include "avrcp.h"
#include <btstack.h>

typedef struct
{
    uint16_t cid;
    uint8_t volume;
} bt_avrcp_ctx_t;

static bt_avrcp_ctx_t ctx;

static void bt_avrcp_volume_change_callback(uint8_t volume)
{
    const btstack_audio_sink_t *audio = btstack_audio_sink_get_instance();
    if (audio != NULL) {
        audio->set_volume(volume);
    }
}

static void bt_avrcp_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    if ((packet_type != HCI_EVENT_PACKET) || (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META)) {
        return;
    }

    const uint8_t subevent = hci_event_avrcp_meta_get_subevent_code(packet);
    uint8_t status;

    switch (subevent) {
        case AVRCP_SUBEVENT_CONNECTION_ESTABLISHED:
            status = avrcp_subevent_connection_established_get_status(packet);
            if (status != ERROR_CODE_SUCCESS) {
                ctx.cid = 0;
                break;
            }

            ctx.cid = avrcp_subevent_connection_established_get_avrcp_cid(packet);

            avrcp_target_support_event(ctx.cid, AVRCP_NOTIFICATION_EVENT_VOLUME_CHANGED);
            avrcp_target_support_event(ctx.cid, AVRCP_NOTIFICATION_EVENT_BATT_STATUS_CHANGED);

            avrcp_target_battery_status_changed(ctx.cid, AVRCP_BATTERY_STATUS_EXTERNAL); // TODO send real status when battery powered

            /* Set default volume */
            avrcp_target_volume_changed(ctx.cid, BT_AVRCP_DEFAULT_VOLUME);
            bt_avrcp_volume_change_callback(BT_AVRCP_DEFAULT_VOLUME);

            avrcp_controller_get_supported_events(ctx.cid);
            break;

        case AVRCP_SUBEVENT_CONNECTION_RELEASED:
            ctx.cid = 0;
            break;

        default:
            break;
    }
}

static void bt_avrcp_controller_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    if ((packet_type != HCI_EVENT_PACKET) || (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META)) {
        return;
    }
    if (ctx.cid == 0) {
        return;
    }

    const uint8_t subevent = hci_event_avrcp_meta_get_subevent_code(packet);
    uint8_t play_status;

    switch (subevent) {
        case AVRCP_SUBEVENT_GET_CAPABILITY_EVENT_ID_DONE:
            avrcp_controller_enable_notification(ctx.cid, AVRCP_NOTIFICATION_EVENT_PLAYBACK_STATUS_CHANGED);
            avrcp_controller_enable_notification(ctx.cid, AVRCP_NOTIFICATION_EVENT_NOW_PLAYING_CONTENT_CHANGED);
            avrcp_controller_enable_notification(ctx.cid, AVRCP_NOTIFICATION_EVENT_TRACK_CHANGED);
            avrcp_controller_enable_notification(ctx.cid, AVRCP_NOTIFICATION_EVENT_VOLUME_CHANGED);
            break;

        default:
            break;
    }

}

static void bt_avrcp_target_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    if ((packet_type != HCI_EVENT_PACKET) || (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META)) {
        return;
    }

    const uint8_t subevent = hci_event_avrcp_meta_get_subevent_code(packet);

    switch (subevent) {
        case AVRCP_SUBEVENT_NOTIFICATION_VOLUME_CHANGED:
            ctx.volume = avrcp_subevent_notification_volume_changed_get_absolute_volume(packet);
            bt_avrcp_volume_change_callback(ctx.volume);
            break;
        
        default:
            break;
    }
}

void bt_avrcp_init(void)
{
    avrcp_init();
    avrcp_controller_init();
    avrcp_target_init();

    avrcp_register_packet_handler(bt_avrcp_packet_handler);
    avrcp_controller_register_packet_handler(bt_avrcp_controller_packet_handler);
    avrcp_target_register_packet_handler(bt_avrcp_target_packet_handler);
}
