#include "bt.h"
#include "avrcp.h"
#include "sdp.h"
#include "a2dp.h"
#include <errno.h>
#include <hci.h>
#include <l2cap.h>
#include <btstack_event.h>
#include <btstack_run_loop.h>

typedef struct 
{
    btstack_packet_callback_registration_t hci_registration;
    bd_addr_t local_addr;
    const char *pin;
    bt_init_done_callback_t init_done_callback;
    void *init_done_arg;
} bt_ctx_t;

static bt_ctx_t ctx;

static void bt_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }

    const uint8_t type = hci_event_packet_get_type(packet);
    bd_addr_t addr;

    switch (type) {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) {
                break;
            }
            gap_local_bd_addr(ctx.local_addr);
            if (ctx.init_done_callback != NULL) {
                ctx.init_done_callback(ctx.init_done_arg);
            }
            break;

        case HCI_EVENT_PIN_CODE_REQUEST:
            hci_event_pin_code_request_get_bd_addr(packet, addr);
            gap_pin_code_response(addr, ctx.pin);
            break;

        default:
            break;
    }
}

int bt_init(const char *name, const char *pin, bt_init_done_callback_t init_done_callback, void *init_done_arg)
{
    if ((name == NULL) || (pin == NULL)) {
        return -EINVAL;
    }

    ctx.pin = pin;
    ctx.init_done_callback = init_done_callback;
    ctx.init_done_arg = init_done_arg;

    l2cap_init();

    bt_sdp_init();
    bt_a2dp_init();
    bt_avrcp_init();

    gap_set_local_name(name);
    gap_discoverable_control(1); // Allow to show up in Bluetooth inquiry
    gap_set_class_of_device(0x200414); // Service Class: Audio, Major Device Class: Audio, Minor: Loudspeaker
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH | LM_LINK_POLICY_ENABLE_SNIFF_MODE); // Allow for role switch in general and sniff mode
    gap_set_allow_role_switch(true); // Allow for role switch on outgoing connections

    ctx.hci_registration.callback = bt_packet_handler;
    hci_add_event_handler(&ctx.hci_registration);

    return 0;
}

void bt_run(void)
{
    hci_power_control(HCI_POWER_ON);
    btstack_run_loop_execute();
}
