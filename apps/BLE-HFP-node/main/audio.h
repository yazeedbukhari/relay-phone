#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include <stddef.h>

void audio_init(void);
void audio_start(void);
void audio_stop(void);

// Queue raw 8-bit DAC samples into the audio output ring buffer.
void audio_enqueue_dac_u8(const uint8_t *data, size_t len);

// Called from HFP data callback context with raw PCM (8kHz, 16-bit signed mono)
void audio_receive(const uint8_t *data, uint32_t len);

// Called from HFP when it needs mic data; returns bytes written into buf
uint32_t audio_send(uint8_t *buf, uint32_t len);

#endif
