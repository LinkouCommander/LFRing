#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "LFRing.h"

ringbuf_meta_t ringbuf_meta;

typedef struct test_data {
    int id;
    int b;
} test_data_t;

esp_err_t littlefs_init(const char* root, const char* label) {
    esp_vfs_littlefs_conf_t conf;
    conf.base_path = root;
    conf.partition_label = label;
    conf.format_if_mount_failed = true;
    conf.dont_mount = false;
    return esp_vfs_littlefs_register(&conf);
}

void ReadTestTask(void *pvParameters) {
    while(1) {
        test_data_t data;
        LFRingRead(&ringbuf_meta, &data, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void WriteTestTask(void *pvParameters) {
    int i = 0;
    while(1) {
        test_data_t data;
        data.id = i;
        data.b = 10;
        LFRingWrite(&ringbuf_meta, &data, 1);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void) {
    const char *root = "root";
    const char *label = "root";
    uint32_t samples = 500;

    littlefs_init(root, label);
    LFRingInit(&ringbuf_meta, root, label, sizeof(test_data_t), samples);

    xTaskCreatePinnedToCore(ReadTestTask, "ReadTestTask", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(WriteTestTask, "WriteTestTask", 4096, NULL, 5, NULL, 1);
}