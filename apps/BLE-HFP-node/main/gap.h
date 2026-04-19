#ifndef GAP_H
#define GAP_H

#include <stdbool.h>

#ifndef BT_PIN
#define BT_PIN "0000"
#endif

#ifndef REMOTE_DEVICE_NAME
#define REMOTE_DEVICE_NAME ""
#endif

void gap_init(void);
bool gap_is_ring_active(void);
bool gap_is_audio_active(void);
void gap_answer_call(void);
void gap_reject_call(void);
void gap_hangup_call(void);

#endif
