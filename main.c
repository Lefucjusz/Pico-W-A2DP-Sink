#include <stdio.h>
#include <pico/stdlib.h>
#include <pico/cyw43_arch.h>
#include <bt.h>

static void bt_init_done_callback(void *arg)
{
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false); // Turn off LED when configured
}

int main(void)
{
    stdio_init_all();

    if (cyw43_arch_init()) {
        panic("Failed to init cyw43_arch!");
    }

    /* Turn on LED until BT is configured */
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);

    /* Initialize BT */
    bt_init("Lefucjusz's Pico Speaker", "0000", bt_init_done_callback, NULL);

    /* Run main BT loop */
    bt_run();
}
