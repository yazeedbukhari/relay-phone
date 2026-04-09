# Relay Phone ESP32 Workspace

This repository is organized as two separate ESP-IDF apps, one per ESP32 board:

- `apps/transmitter/` - firmware for the transmitter board
- `apps/receiver/` - firmware for the receiver board (currently outputs DAC tone on GPIO25)

## Prerequisites

- ESP-IDF v5.5+ installed and exported in your shell
- Two ESP32-WROOM-32D boards

## Build And Flash

### Receiver Board

```bash
cd apps/receiver
idf.py -p /dev/cu.usbserial-RECEIVER flash monitor
```

### Transmitter Board

```bash
cd apps/transmitter
idf.py -p /dev/cu.usbserial-TRANSMITTER flash monitor
```

## Notes

- Receiver DAC output pin: `GPIO25` (`DAC_CHAN_0`)
- Use separate serial ports for each board
