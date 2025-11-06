#include "LFRing.h"
#include "nvs.h"
#include <string.h>
#include "esp_log.h"
#include <errno.h>

static const char *TAG = "LFRING";

int save_ringbuf_meta(ringbuf_meta_t *meta);
void ringbuf_get_path(ringbuf_meta_t *meta, char *path);

// -------------------- meta data -------------------- //
/**
 * @brief Reset the ring buffer metadata and save it to NVS.
 *
 * This function initializes the ring buffer metadata with the given item size
 * and item number, resets the head and tail pointers to 0, and persists the
 * new metadata to NVS.
 *
 * @param meta Pointer to the ring buffer metadata structure to reset.
 * @param itemSize Size of each item in the ring buffer (in bytes).
 * @param itemNum Total number of items in the ring buffer.
 *
 * @return
 *      - LFRB_OK : Metadata successfully reset and saved.
 *      - Propagate errors from save_ringbuf_meta().
 */
int reset_ringbuf_meta(ringbuf_meta_t *meta, uint32_t itemSize, uint32_t itemNum) {
    meta->head = 0;
    meta->tail = 0;
    meta->item_size = itemSize;
    meta->item_num = itemNum;
    return save_ringbuf_meta(meta);
}

/**
 * @brief Initialize ring buffer metadata from NVS or create new metadata.
 *
 * This function attempts to load ring buffer metadata (head, tail, item size,
 * number of items) from the specified NVS namespace. If the NVS namespace does
 * not exist or the structure has changed, it resets the metadata to the provided
 * item size and item number.
 *
 * @param meta Pointer to the ring buffer metadata structure to initialize.
 * @param nvs_namespace The NVS namespace used to store metadata.
 * @param itemSize Size of each item in the ring buffer (in bytes).
 * @param itemNum Total number of items in the ring buffer.
 *
 * @return
 *      - LFRB_OK : Metadata successfully initialized or reset.
 *      - Propagate errors from reset_ringbuf_meta().
 */
int init_ringbuf_meta(ringbuf_meta_t *meta, const char *nvs_namespace, uint32_t itemSize, uint32_t itemNum) {
    // Copy NVS namespace into meta
    strncpy(meta->nvs_namespace, nvs_namespace, sizeof(meta->nvs_namespace)-1);
    meta->nvs_namespace[sizeof(meta->nvs_namespace)-1] = '\0';

    // Try to open NVS namespace in read-only mode
    nvs_handle_t handle;
    esp_err_t err = nvs_open(meta->nvs_namespace, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        // Successfully opened NVS, read existing metadata
        ESP_LOGI(TAG, "Opened NVS namespace: %s", meta->nvs_namespace);
        nvs_get_u32(handle, "head", &meta->head);
        nvs_get_u32(handle, "tail", &meta->tail);
        nvs_get_u32(handle, "size", &meta->item_size);
        nvs_get_u32(handle, "num", &meta->item_num);
        nvs_close(handle);

        ESP_LOGI(TAG, "%u/%u", (unsigned int)meta->item_size, (unsigned int)meta->item_num);

        // Check if the saved metadata matches the expected item size/num
        if (meta->item_size != itemSize || meta->item_num != itemNum) {
            ESP_LOGW(TAG, "Item structure changed. Resetting ring buffer meta.");
            int status = reset_ringbuf_meta(meta, itemSize, itemNum);
            if (status < 0) {
                ESP_LOGE(TAG, "Failed to reset meta, status=%d", status);
                return status;
            }
        } else {
            ESP_LOGI(TAG, "Meta loaded successfully. No reset needed.");
        }
    } else {
        // NVS open failed, initialize new metadata
        ESP_LOGI(TAG, "NVS open failed (%s). Initializing new meta.", esp_err_to_name(err));
        int status = reset_ringbuf_meta(meta, itemSize, itemNum);
        if (status < 0) {
            ESP_LOGE(TAG, "Failed to initialize new meta, status=%d", status);
            return status;
        }
    }

    ESP_LOGI(TAG, "Loaded meta from NVS: head=%" PRIu32 " tail=%" PRIu32 " size=%" PRIu32 " num=%" PRIu32,
            meta->head, meta->tail, meta->item_size, meta->item_num);

    return LFRB_OK;
}

/**
 * @brief Save ring buffer metadata (head, tail, item size, and number of items) to NVS.
 *
 * This function writes the current state of the ring buffer into the specified
 * NVS namespace. It ensures that the metadata is persisted across reboots.
 *
 * @param meta Pointer to the ring buffer metadata structure containing the
 *             current head, tail, item size, and number of items.
 *
 * @return
 *      - LFRB_OK : Metadata successfully saved to NVS.
 *      - LFRB_NVS_ERROR: NVS namespace cannot be opened.
 */
int save_ringbuf_meta(ringbuf_meta_t *meta) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(meta->nvs_namespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_set_u32(handle, "head", meta->head);
        nvs_set_u32(handle, "tail", meta->tail);
        nvs_set_u32(handle, "size", meta->item_size);
        nvs_set_u32(handle, "num", meta->item_num);
        nvs_commit(handle);
        nvs_close(handle);
        return LFRB_OK;
    }
    return -LFRB_NVS_ERROR;
}

/**
 * @brief Load ring buffer metadata (head and tail) from NVS.
 *
 * This function reads the current head and tail indices of the ring buffer
 * from the specified NVS namespace. If successful, the metadata in the
 * provided structure is updated.
 *
 * @param meta Pointer to the ring buffer metadata structure containing
 *             the NVS namespace and fields to be updated.
 *
 * @return
 *      - LFRB_OK: Metadata successfully loaded from NVS.
 *      - LFRB_NVS_ERROR: NVS namespace cannot be opened.
 */
int load_ringbuf_meta(ringbuf_meta_t *meta) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(meta->nvs_namespace, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        nvs_get_u32(handle, "head", &meta->head);
        nvs_get_u32(handle, "tail", &meta->tail);
        nvs_close(handle);
        return LFRB_OK;
    } else {
        return -LFRB_NVS_ERROR;
    }
}

// -------------------- LFS ring buf -------------------- //
/**
 * @brief Reset the LittleFS ring buffer file.
 *
 * This function clears the contents of the ring buffer file by opening
 * it in write-binary mode ("wb"), effectively truncating it to zero
 * length. It is typically called when the buffer is empty or corrupted.
 *
 * @param meta Pointer to the ring buffer metadata structure.
 *
 * @return
 *      - LFRB_OK: File successfully reset.
 *      - LFRB_LFS_ERROR: Failed to open file
 */
int reset_ringbuf_lfs(ringbuf_meta_t *meta) {
    char path[LFRB_MAX_PATH];
    ringbuf_get_path(meta, path);
    ESP_LOGI(TAG, "Resetting ring buffer file: %s", path);

    FILE* f = fopen(path, "wb");
    if(f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reset: errno=%d", errno);
        return -LFRB_LFS_ERROR;
    }

    fclose(f);
    ESP_LOGI(TAG, "Ring buffer file reset successfully");
    return LFRB_OK;
}

/**
 * @brief Initialize the LittleFS-based ring buffer file.
 *
 * This function sets the root directory for the ring buffer and ensures
 * the filesystem path exists. If the ring buffer is empty (head and tail
 * are both zero), it resets the corresponding LittleFS file to prepare
 * it for writing.
 *
 * @param meta Pointer to the ring buffer metadata structure.
 * @param root Path to the root directory for storing ring buffer data.
 *
 * @return
 *      - LFRB_OK: Initialization succeeded.  
 *      - LFRB_ROOT_NOT_FOUND_ERROR: LittleFS root path not found
 *      - Propagate errors from reset_ringbuf_lfs()
 */
int init_ringbuf_lfs(ringbuf_meta_t *meta, const char *root) {
    // Check if the specified root directory exists
    struct stat st;
    if(stat(root, &st) != 0) {
        ESP_LOGE(TAG, "Root path not found: %s", root);
        return -LFRB_ROOT_NOT_FOUND_ERROR;
    }

    // Copy root path into the metadata
    strncpy(meta->root, root, sizeof(meta->root)-1);
    meta->root[sizeof(meta->root)-1] = '\0';
    ESP_LOGI(TAG, "Ring buffer root set to: %s", meta->root);

    // Reset ring buffer if empty
    if(meta->head == 0 && meta->tail == 0) {
        ESP_LOGI(TAG, "Ring buffer empty, resetting file");
        return reset_ringbuf_lfs(meta);
    }

    ESP_LOGI(TAG, "Ring buffer already initialized (head=%u, tail=%u)", (unsigned int)meta->head, (unsigned int)meta->tail);
    return LFRB_OK;
}

/**
 * @brief Write items to the ring buffer stored in LittleFS.
 *
 * This function writes up to @p num items into the ring buffer file,
 * starting from the current head position. If the file cannot be opened,
 * it attempts to reset the ring buffer metadata and recreate the file.
 *
 * @param meta Pointer to the ring buffer metadata structure.
 * @param data Pointer to the data to be written into the ring buffer.
 * @param num Number of items to write.
 *
 * @return
 *      - Number of items successfully written.
 *      - LFRB_NFILE_ERROR: failed to recreate file
 */
int ringbuf_write(ringbuf_meta_t *meta, const void* data, size_t num) {
    // Calculate byte offset based on the current head position
    uint32_t pos = meta->head * meta->item_size;

    // Construct full path to the ring buffer file using root and namespace
    char path[LFRB_MAX_PATH];
    ringbuf_get_path(meta, path);

    // Open the ring buffer file in read-write binary mode
    FILE* f = fopen(path, "rb+");
    if(f == NULL) {
        // If the file cannot be opened, reset metadata and the file
        reset_ringbuf_meta(meta, meta->item_size, meta->item_num);
        reset_ringbuf_lfs(meta);

        // Attempt to reopen the file after reset
        f = fopen(path, "rb+");
        if(f == NULL) {
            ESP_LOGE(TAG, "ringbuf_write: failed to recreate file %s", path);
            return -LFRB_NFILE_ERROR;
        }
    }

    // Write up to 'num' items from 'data' into the file
    fseek(f, pos, SEEK_SET);
    size_t n = fwrite(data, meta->item_size, num, f);
    fclose(f);

    return n;
}

/**
 * @brief Read items from the ring buffer stored in LittleFS.
 *
 * This function reads up to @p num items from the ring buffer file,
 * starting from the current tail position. If the file cannot be opened,
 * it resets both the ring buffer metadata and the corresponding file
 * to recover from potential corruption.
 *
 * @param meta Pointer to the ring buffer metadata structure.
 * @param out_data Pointer to the buffer where read data will be stored.
 * @param num Number of items to read.
 *
 * @return
 *      - Number of items successfully read.
 *      - 0 if the file could not be opened or no data is available.
 */
int ringbuf_read(ringbuf_meta_t *meta, void* out_data, size_t num) {
    uint32_t pos = meta->tail * meta->item_size;

    char path[LFRB_MAX_PATH];
    ringbuf_get_path(meta, path);

    // Open the ring buffer file in read-binary mode
    FILE* f = fopen(path, "rb");
    if(f == NULL) {
        // If the file cannot be opened, reset both metadata and file to recover
        reset_ringbuf_meta(meta, meta->item_size, meta->item_num);
        reset_ringbuf_lfs(meta);
        return 0;
    }

    // Read up to 'num' items from the file into 'out_data'
    fseek(f, pos, SEEK_SET);
    size_t n = fread(out_data, meta->item_size, num, f);
    fclose(f);

    return n;
}

/**
 * @brief Construct the full file path for the ring buffer data file.
 *
 * This function generates the absolute path to the ring buffer’s
 * storage file on LittleFS, using the NVS namespace as the file name.
 * The resulting file will have a ".bin" extension.
 *
 * Example:
 *     root = "/ringbuf", nvs_namespace = "sensor"
 *     → path = "/ringbuf/sensor.bin"
 *
 * @param meta Pointer to the ring buffer metadata structure containing
 *             the root directory and NVS namespace.
 * @param path Output buffer to store the generated file path.
 *             Must be at least LFRB_MAX_PATH bytes long.
 */
void ringbuf_get_path(ringbuf_meta_t *meta, char *path) {
    snprintf(path, LFRB_MAX_PATH, "%s/%s.bin", meta->root, meta->nvs_namespace);
}

// -------------------- User Layer -------------------- //
/**
 * @brief Initialize the LittleFS-based ring buffer system.
 *
 * This function sets up both the NVS-stored metadata and the LittleFS
 * storage backend for the ring buffer. It initializes key parameters
 * such as the item size, total number of items, and the file system root
 * path where data will be stored. A mutex lock is also created to ensure
 * thread-safe operations.
 *
 * @param meta Pointer to the ring buffer metadata structure.
 * @param root Path to the LittleFS directory used for storing ring buffer data.
 * @param nvs_namespace Name of the NVS namespace used to store metadata.
 * @param itemSize Size (in bytes) of each data item in the ring buffer.
 * @param itemNum Total number of data items the ring buffer can store.
 *
 * @return Propagate errors from init_ringbuf_meta() and init_ringbuf_lfs()
 */
int LFRingInit(ringbuf_meta_t *meta, const char *root, const char *nvs_namespace, uint32_t itemSize, uint32_t itemNum) {
    int status;
    status = init_ringbuf_meta(meta, nvs_namespace, itemSize, itemNum);
    if(status < 0) return status;
    status = init_ringbuf_lfs(meta, root);
    meta->lock = xSemaphoreCreateMutex();
    return status;
}

/**
 * @brief Check if the LittleFS-based ring buffer is empty.
 *
 * This function loads the current ring buffer metadata and determines
 * whether the buffer contains any unread data. It returns a boolean-like
 * value indicating the buffer’s empty state.
 *
 * @param meta Pointer to the ring buffer metadata structure.
 *
 * @return
 *      - 1 : Ring buffer is empty.  
 *      - 0 : Ring buffer is not empty.  
 *      - <0 : Error code (if loading metadata fails).
 */
int LFRingIsEmpty(ringbuf_meta_t *meta) {
    int err = load_ringbuf_meta(meta);
    if(err < 0) return err;
    return meta->tail == meta->head;
}

/**
 * @brief Write items to the LittleFS-based ring buffer.
 *
 * This function writes data into the ring buffer stored in LittleFS.
 * It ensures thread safety using a mutex and maintains metadata consistency
 * by updating the head and tail pointers. If the buffer becomes full,
 * the oldest data will be overwritten.
 *
 * @param meta Pointer to the ring buffer metadata structure.
 * @param data Pointer to the data to be written into the ring buffer.
 * @param num Number of items to write to the ring buffer.
 *
 * @return >= 0 as number of items successfully written, or:
 *          - Propagate errors from ringbuf_write();
 */
int LFRingWrite(ringbuf_meta_t *meta, void* data, size_t num) {
    xSemaphoreTake(meta->lock, portMAX_DELAY);

    // Read meta data
    load_ringbuf_meta(meta);

    // Reject requests that exceed buffer capacity
    if(num > meta->item_num-1) {
        xSemaphoreGive(meta->lock);
        return -LFRB_ENUM_EXCEED;
    }

    // Attempt to write data into the ring buffer
    // Case 1: Enough space from current head to the end of buffer
    // -> Write data in one contiguous block
    int n = 0;
    if(meta->item_num - meta->head >= num) {
        n += ringbuf_write(meta, data, num);
    }
    // Case 2: Data to write exceeds remaining space at buffer end
    // -> Split into two writes (wrap-around)
    else {
        int write_num = meta->item_num - meta->head;
        n += ringbuf_write(meta, data, write_num);
        meta->head = 0;
        n += ringbuf_write(meta, (uint8_t*)data + write_num * meta->item_size, num - write_num);
    }

    // Update head & tail ptr
    // Calculate how much of the buffer is currently used
    uint32_t used = (meta->head >= meta->tail)
                    ? (meta->head - meta->tail)
                    : (meta->item_num - meta->tail + meta->head);
    // Advance the head pointer
    meta->head = (meta->head + n) % meta->item_num;
    // Handle buffer overflow (overwrite oldest data)
    if(used + n > meta->item_num-1) {
        uint32_t overwrite = used + n - meta->item_num + 1;
        meta->tail = (meta->tail + overwrite) % meta->item_num;
        ESP_LOGW(TAG, "LFRingWrite: buffer overflow, overwrote %u old items", (unsigned int)overwrite);
    }

    // Update meta date
    save_ringbuf_meta(meta);

    xSemaphoreGive(meta->lock);
    return n;
}

/**
 * @brief  Read items from the LittleFS-based ring buffer.
 *
 * This function reads up to `num` items from the ring buffer into `out_data`.
 * The function is thread-safe; it locks the buffer with a semaphore during access.
 *
 * @param meta      Pointer to the ring buffer metadata structure.
 * @param out_data  Pointer to a buffer where the read items will be stored.
 * @param num       Number of items to read.
 * 
 * @return Number of items successfully read.
 */
int LFRingRead(ringbuf_meta_t *meta, void* out_data, size_t num) {
    xSemaphoreTake(meta->lock, portMAX_DELAY);

    // Read meta data from NVS
    load_ringbuf_meta(meta);

    // Check if the buffer is empty
    if(meta->tail == meta->head) {
        xSemaphoreGive(meta->lock);
        return 0;
    }

    // Read data from the ring buffer
    int n = ringbuf_read(meta, out_data, num);

    // Update the tail pointer after reading
    meta->tail = (meta->tail + n) % meta->item_num;
    save_ringbuf_meta(meta);

    xSemaphoreGive(meta->lock);
    return n;
}