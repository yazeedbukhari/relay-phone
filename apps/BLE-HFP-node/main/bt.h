#ifndef BT_H
#define BT_H

#include <stdbool.h>

#ifndef BT_PIN
#define BT_PIN "0000"
#endif

#ifndef REMOTE_DEVICE_NAME
#define REMOTE_DEVICE_NAME ""
#endif

void bt_gap_init(void);

bool hfp_is_ring_active(void);
bool hfp_is_audio_active(void);
void hfp_answer_call(void);
void hfp_reject_call(void);
void hfp_hangup_call(void);

#endif
