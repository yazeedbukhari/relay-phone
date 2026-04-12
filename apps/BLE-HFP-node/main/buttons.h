#ifndef BUTTONS_H
#define BUTTONS_H

#include "driver/gpio.h"

#define BUTTON_ANSWER_GPIO GPIO_NUM_33
#define BUTTON_REJECT_GPIO GPIO_NUM_27

void buttons_init(void);
void buttons_poll(void);

#endif
