#include "platform_fs.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_vfs_fat.h"

#define SD_MOUNT_POINT "/sdcard"
#define SD_SPI_HOST SPI3_HOST
#define SD_GPIO_CS 10
#define SD_GPIO_MOSI 11
#define SD_GPIO_CLK 12
#define SD_GPIO_MISO 13

static const char *TAG = "platform_fs";

static void log_sd_idle_levels(void)
{
    const gpio_config_t input_config = {
        .pin_bit_mask = (1ULL << SD_GPIO_CS) |
                        (1ULL << SD_GPIO_MOSI) |
                        (1ULL << SD_GPIO_CLK) |
                        (1ULL << SD_GPIO_MISO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&input_config);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Unable to sample SD GPIO levels: %s",
                 esp_err_to_name(err));
        return;
    }

    esp_rom_delay_us(100);
    ESP_LOGI(TAG, "SD idle levels (external pulls only): CS=%d MOSI=%d CLK=%d MISO=%d",
             gpio_get_level(SD_GPIO_CS),
             gpio_get_level(SD_GPIO_MOSI),
             gpio_get_level(SD_GPIO_CLK),
             gpio_get_level(SD_GPIO_MISO));
}

static esp_err_t enable_sd_internal_pullups(void)
{
    const gpio_num_t pins[] = {
        SD_GPIO_CS,
        SD_GPIO_MOSI,
        SD_GPIO_CLK,
        SD_GPIO_MISO,
    };

    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i)
    {
        esp_err_t err = gpio_set_pull_mode(pins[i], GPIO_PULLUP_ONLY);
        if (err != ESP_OK)
        {
            return err;
        }
    }

    return ESP_OK;
}

static void log_sd_root(void)
{
    DIR *directory = opendir(SD_MOUNT_POINT);
    if (!directory)
    {
        ESP_LOGE(TAG, "Unable to list %s", SD_MOUNT_POINT);
        return;
    }

    ESP_LOGI(TAG, "Files at the SD card root:");
    bool empty = true;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL)
    {
        if (entry->d_name[0] == '.' &&
            (entry->d_name[1] == '\0' ||
             (entry->d_name[1] == '.' && entry->d_name[2] == '\0')))
        {
            continue;
        }

        char path[256];
        int path_length = snprintf(path, sizeof(path), "%s/%s",
                                   SD_MOUNT_POINT, entry->d_name);
        if (path_length < 0 || path_length >= (int)sizeof(path))
        {
            ESP_LOGW(TAG, "  %s (path too long)", entry->d_name);
            continue;
        }

        struct stat file_info;
        if (stat(path, &file_info) == 0 && S_ISDIR(file_info.st_mode))
        {
            ESP_LOGI(TAG, "  [DIR] %s", entry->d_name);
        }
        else if (stat(path, &file_info) == 0)
        {
            ESP_LOGI(TAG, "  %s (%lld bytes)", entry->d_name,
                     (long long)file_info.st_size);
        }
        else
        {
            ESP_LOGI(TAG, "  %s", entry->d_name);
        }
        empty = false;
    }

    if (empty)
    {
        ESP_LOGW(TAG, "  (empty SD card)");
    }
    closedir(directory);
}

int platform_fs_open(const char *path, int flags)
{
    return open(path, flags);
}

ssize_t platform_fs_read(int fd, void *buffer, size_t size)
{
    return read(fd, buffer, size);
}

off_t platform_fs_seek(int fd, off_t offset, int whence)
{
    return lseek(fd, offset, whence);
}

int platform_fs_size(int fd)
{
    struct stat fileinfo;

    if (fstat(fd, &fileinfo) == -1) {
        return -1;
    }

    return (int)fileinfo.st_size;
}

int platform_fs_close(int fd)
{
    return close(fd);
}

bool platform_fs_init(void)
{
    ESP_LOGI(TAG, "Initializing microSD over SPI3: CS=%d MOSI=%d CLK=%d MISO=%d",
             SD_GPIO_CS, SD_GPIO_MOSI, SD_GPIO_CLK, SD_GPIO_MISO);
    log_sd_idle_levels();

    const spi_bus_config_t bus_config = {
        .mosi_io_num = SD_GPIO_MOSI,
        .miso_io_num = SD_GPIO_MISO,
        .sclk_io_num = SD_GPIO_CLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = 16 * 1024,
    };
    esp_err_t err = spi_bus_initialize(SD_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Unable to initialize SD SPI bus: %s", esp_err_to_name(err));
        return false;
    }

    err = enable_sd_internal_pullups();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Unable to enable SD GPIO pull-ups: %s",
                 esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "Internal pull-ups enabled on the four SD lines");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;
    host.max_freq_khz = 10000;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = SD_SPI_HOST;
    slot_config.gpio_cs = SD_GPIO_CS;

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 6,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_card_t *card = NULL;
    err = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config,
                                  &mount_config, &card);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Unable to mount the SD card at %s: %s",
                 SD_MOUNT_POINT, esp_err_to_name(err));
        if (err == ESP_ERR_TIMEOUT)
        {
            ESP_LOGE(TAG, "The card did not answer: check that it is fully inserted, "
                          "then power-cycle the board");
        }
        else if (err == ESP_FAIL)
        {
            ESP_LOGE(TAG, "The card answered but FAT could not be mounted; "
                          "format it as FAT32");
        }
        return false;
    }

    ESP_LOGI(TAG, "SD card mounted at %s (%llu MiB)", SD_MOUNT_POINT,
             (unsigned long long)(card->csd.capacity * card->csd.sector_size /
                                  (1024ULL * 1024ULL)));
    log_sd_root();
    return true;
}
