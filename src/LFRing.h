#pragma once
#include <stdint.h>
#include "nvs_flash.h"
#include "esp_vfs.h"
#include "freertos/semphr.h"

#ifndef LFRING_H
#define LFRING_H

#ifdef __cplusplus
extern "C" {
#endif

#define LFRB_MAX_PATH 64

typedef enum {
    LFRB_OK = 0,
    LFRB_NVS_ERROR = 1,
    LFRB_LFS_ERROR = 2,
    LFRB_ROOT_NOT_FOUND_ERROR = 3,
    LFRB_NFILE_ERROR = 4
} ringbuf_error_t;

typedef struct {
    char root[ESP_VFS_PATH_MAX];
    char nvs_namespace[NVS_KEY_NAME_MAX_SIZE];
    uint32_t head;
    uint32_t tail;
    uint32_t item_size;
    uint32_t item_num;
    SemaphoreHandle_t lock;
} ringbuf_meta_t;

int LFRingInit(ringbuf_meta_t *meta, const char *root, const char *nvs_namespace, uint32_t itemSize, uint32_t itemNum);
int LFRingIsEmpty(ringbuf_meta_t *meta);
int LFRingWrite(ringbuf_meta_t *meta, void* data, size_t num);
int LFRingRead(ringbuf_meta_t *meta, void* out_data, size_t num);

#ifdef __cplusplus
}
#endif

#endif