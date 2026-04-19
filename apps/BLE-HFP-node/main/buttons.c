#include "buttons.h"
#include "gap.h"
#include "esp_log.h"

#define BUTTONS_TAG "BUTTONS"
#define BUTTON_DEBOUNCE_MS 40

typedef struct {
    gpio_num_t gpio;
    const char *label;
    int stable_level;
    int last_raw_level;
    uint32_t last_change_ms;
} button_state_t;

static button_state_t answer_btn = {
    .gpio = BUTTON_ANSWER_GPIO,
    .label = "Answer",
    .stable_level = 1,
    .last_raw_level = 1,
    .last_change_ms = 0,
};

static button_state_t reject_btn = {
    .gpio = BUTTON_REJECT_GPIO,
    .label = "Reject",
    .stable_level = 1,
    .last_raw_level = 1,
    .last_change_ms = 0,
};

static void reject_button_pressed(void)
{
    if (gap_is_ring_active()) {
        gap_reject_call();
        return;
    }

    gap_hangup_call();
}

static void button_poll_and_handle(button_state_t *btn, void (*on_pressed)(void))
{
    uint32_t now_ms = esp_log_timestamp();
    int raw_level = gpio_get_level(btn->gpio);

    if (raw_level != btn->last_raw_level) {
        btn->last_raw_level = raw_level;
        btn->last_change_ms = now_ms;
    }

    if ((now_ms - btn->last_change_ms) < BUTTON_DEBOUNCE_MS) {
        return;
    }

    if (btn->stable_level == btn->last_raw_level) {
        return;
    }

    btn->stable_level = btn->last_raw_level;

    // Active-low: trigger action once on clean press edge only.
    if (btn->stable_level == 0) {
        ESP_LOGI(BUTTONS_TAG, "%s button pressed", btn->label);
        on_pressed();
    }
}

void buttons_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_ANSWER_GPIO) | (1ULL << BUTTON_REJECT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    ESP_LOGI(BUTTONS_TAG, "Buttons initialised: answer=GPIO%d, reject=GPIO%d",
             BUTTON_ANSWER_GPIO, BUTTON_REJECT_GPIO);
}

void buttons_poll(void)
{
    button_poll_and_handle(&answer_btn, gap_answer_call);
    button_poll_and_handle(&reject_btn, reject_button_pressed);
}
