#pragma once

typedef void(*bt_init_done_callback_t)(void *arg);

int bt_init(const char *name, const char *pin, bt_init_done_callback_t init_done_callback, void *init_done_arg);
void bt_run(void);
