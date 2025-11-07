# LFRing — Persistent LittleFS-Based Ring Buffer for ESP32

`LFRing` is a lightweight, fault-tolerant circular buffer implementation designed for ESP-IDF.
It uses LittleFS (based on [joltwallet/esp_littlefs](https://github.com/joltwallet/esp_littlefs)) for persistent storage and NVS for metadata management, ensuring data integrity even during power loss or unexpected resets.

[![PlatformIO Registry](https://badges.registry.platformio.org/packages/linkoucommander/library/LFRing.svg)](https://registry.platformio.org/libraries/linkoucommander/LFRing)

## Features
- **Circular buffer** with automatic overwrite of oldest data

- **Persistent storage** via LittleFS (data survives power cycles)

- **NVS-backed metadata** for head/tail/size tracking

- **Thread-safe** access using FreeRTOS mutex

- **Configurable** item structure and buffer capacity

- **Self-healing** — automatically resets on metadata or file corruption

## Architecture Overview

```pgsql
┌──────────────────────────────────────┐
│        User API                      │
│  LFRingInit / Read / Write / IsEmpty │
└────────────┬─────────────────────────┘
             │
┌────────────▼─────────────────────────┐
│          LFS Ring Buffer             │
│       Stores actual data in          │
│     LittleFS as <namespace>.bin      │
└────────────┬─────────────────────────┘
             │
┌────────────▼─────────────────────────┐
│           NVS Metadata               │
│     Tracks head/tail/item info       │
└──────────────────────────────────────┘
```

## Usage
### Initialization
```c
// Initializes the ring buffer by setting up NVS metadata and verifying/creating the corresponding .bin file in LittleFS.
int LFRingInit(
    ringbuf_meta_t *meta,
    const char *root,
    const char *nvs_namespace,
    uint32_t itemSize,
    uint32_t itemNum
);
```

### Writing Data
```c
// Writes one or more items to the ring buffer.
// Automatically handles wrap-around and overwriting of old data when full.
int LFRingWrite(ringbuf_meta_t *meta, void* data, size_t num);
```

### Reading Data
```c
// Reads items from the buffer starting at the tail.
int LFRingRead(ringbuf_meta_t *meta, void* out_data, size_t num);
```

### Check If Buffer Is Empty
```c
// Returns 1 if the buffer is empty, otherwise 0.
int LFRingIsEmpty(ringbuf_meta_t *meta);
```

## Installation

### Prerequisite
Install and configure [joltwallet/esp_littlefs](https://github.com/joltwallet/esp_littlefs) before using this library.

### Option 1: PlatformIO Registry
Add the following to your `platformio.ini`:
```ini
lib_deps = linkoucommander/LFRing
```

### Option 2: Local Library
Clone LFRing.c and LFRing.h into your lib folder:
```lua
|-- lib
|   |-- LFRing
|       |- LFRing.c
|       |- LFRing.h
```


## References
- [joltwallet/esp_littlefs](https://github.com/joltwallet/esp_littlefs)
