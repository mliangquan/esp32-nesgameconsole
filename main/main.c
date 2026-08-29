#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "shared.h"

#include "rg_system.h"
#include "rg_gui.h"

#define START_MODE_LAUNCHER 0
#define START_MODE_NES      1

const nes_rom_t* nes_romdata;
uint32_t nes_part_size;
uint32_t nes_part_rom_data_size;

static void find_rom() {
    ESP_LOGI(__FILE__, "Looking for nes_rom partition.");

    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "nes_rom");
    if(partition == NULL)
    {
        ESP_LOGE(__FILE__, "Fatal error, Partition 'nes_rom' not found, system halt.");
        abort();
    }
    
    nes_part_size = partition->size;
    nes_part_rom_data_size = nes_part_size - sizeof(nes_rom_t);

    ESP_LOGI(__FILE__, "Partition found, size: %d, addr: 0x%X, rom pointer: %p", partition->size, partition->address, &nes_romdata);

	static esp_partition_mmap_handle_t map_handle;
	ESP_ERROR_CHECK(esp_partition_mmap(partition, 0, partition->size, ESP_PARTITION_MMAP_DATA, (const void **)&nes_romdata, &map_handle));

    ESP_LOGI(__FILE__, "nes rom mapped, location:%p, path: %s, len: %u, crc: %x", nes_romdata, nes_romdata->path, nes_romdata->len, nes_romdata->rom_crc);
}

static int check_start_mode()
{
    // 
    // 检查用户是否按下了Launcher Key（默认是Select键）
    //
	gpio_config_t bk_gpio_config = {
		.mode = GPIO_MODE_INPUT,
		.pin_bit_mask = 1ULL << RG_GPIO_LAUNCHER_KEY,
	};
	ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));

	int launcher_key = gpio_get_level(RG_GPIO_LAUNCHER_KEY);

	if (launcher_key == 0) {
        ESP_LOGI(__FILE__, "launcher key pressed, starting launcher...");
        return START_MODE_LAUNCHER;
	}

    //
    // 检查NES_ROM分区中是否存在合法的ROM文件
    //

    if(nes_romdata->len > nes_part_rom_data_size){
        ESP_LOGW(__FILE__, "nes rom data lenght unreasonable, starting launcher...");
        return START_MODE_LAUNCHER;
    }

    uint32_t crc = rg_crc32(0, nes_romdata->rom_data, nes_romdata->len);
    if(crc != nes_romdata->rom_crc){
        ESP_LOGW(__FILE__, "nes rom crc check failed, starting launcher...");
        return START_MODE_LAUNCHER;
    }

	if (!rg_check_nes_header(nes_romdata->rom_data)) {
        ESP_LOGW(__FILE__, "nes rom header check failed, starting launcher...");
		return START_MODE_LAUNCHER;
	}

    return START_MODE_NES;
}

void app_main(void) {
    // 初始化NVS Flash
    esp_err_t ret = nvs_flash_init();
	if (ret) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
    ESP_ERROR_CHECK(ret);

    // 查找NES ROM
    find_rom();

    // 检查启动模式
    int mode = check_start_mode();
    if(mode == START_MODE_LAUNCHER)
    {
        launcher_main();
    }
	
    // 启动NES模拟器
	nes_main();

	while (1) {
        ESP_LOGE(__FILE__, "Never goes here.");
        abort();
	}
}
