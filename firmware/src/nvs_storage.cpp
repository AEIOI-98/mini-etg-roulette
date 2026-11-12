#include "nvs_storage.hpp"
#include <esp_partition.h>
#include <nvs_flash.h>

namespace mini_etg::firmware {
namespace {
constexpr char partition[] = "roulette";
constexpr std::size_t minimum_bytes = 512 * 1024;
}

Error NvsBlobs::begin() noexcept {
    if (handle_) return Error::Ok;
    const auto* part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                ESP_PARTITION_SUBTYPE_DATA_NVS, partition);
    if (!part || part->size < minimum_bytes) return Error::InvalidConfig;
    // Dedicated partition: at most 64 immutable ~1.4 KiB blocks and one <16 KiB
    // head. The remaining space allows NVS old/new copies and page collection.
    // An incompatible or damaged partition must never be erased on startup.
    esp_err_t err = nvs_flash_init_partition(partition);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
        return Error::StorageCorrupt;
    if (err != ESP_OK) return Error::StorageIoError;
    err = nvs_open_from_partition(partition, "table", NVS_READWRITE, &handle_);
    return err == ESP_OK ? Error::Ok : Error::StorageIoError;
}

Error NvsBlobs::get(const char* key, std::uint8_t* out, std::size_t capacity,
                    std::size_t& length) noexcept {
    if (!handle_) return Error::NotReady;
    std::size_t size = capacity;
    const esp_err_t err = nvs_get_blob(handle_, key, out, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) return Error::NoJournal;
    if (err == ESP_ERR_NVS_INVALID_LENGTH || err == ESP_ERR_NVS_TYPE_MISMATCH)
        return Error::StorageCorrupt;
    if (err != ESP_OK) return Error::StorageIoError;
    length = size;
    return Error::Ok;
}

Error NvsBlobs::put(const char* key, const std::uint8_t* data, std::size_t length) noexcept {
    if (!handle_) return Error::NotReady;
    const esp_err_t err = nvs_set_blob(handle_, key, data, length);
    if (err == ESP_ERR_NVS_NOT_ENOUGH_SPACE) return Error::StorageFull;
    if (err != ESP_OK || nvs_commit(handle_) != ESP_OK) return Error::CommitOutcomeUnknown;
    return Error::Ok;
}
} // namespace mini_etg::firmware
