#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "shared.h"

#include "rg_system.h"
#include "rg_gui.h"

#include "webui.h"

static rg_app_t *app;

static void event_handler(int event, void *arg)
{
}

static void options_handler(rg_gui_option_t *dest)
{
}

static void wifi_toggle_interactive(bool enable, int slot)
{
    rg_network_state_t target_state = enable ? RG_NETWORK_CONNECTED : RG_NETWORK_DISCONNECTED;
    int64_t timeout = rg_system_timer() + 20 * 1000000;
    rg_gui_draw_message(enable ? _("Connecting...") : _("Disconnecting..."));
    rg_network_wifi_stop();
    if (enable)
    {
        rg_wifi_config_t config = {0};
        rg_network_wifi_read_config(slot, &config);
        rg_network_wifi_set_config(&config);
        if (slot == 9000)
        {
            const rg_wifi_config_t config = {
                .ssid = "retro-go",
                .password = "retro-go",
                .channel = 6,
                .ap_mode = true,
            };
            rg_network_wifi_set_config(&config);
        }
        if (!rg_network_wifi_start())
            return;
    }
    do // Always loop at least once, in case we're in a transition
    {
        rg_task_delay(100);
        if (rg_system_timer() > timeout)
            break;
        if (rg_input_read_gamepad())
            break;
    } while (rg_network_get_info().state != target_state);
}

static rg_gui_event_t wifi_status_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    rg_network_t info = rg_network_get_info();
    if (info.state != RG_NETWORK_CONNECTED){
        strcpy(option->value, _("Not connected"));
    }
    else if (option->arg == 0x10){
        strcpy(option->value, info.name);
    }
    else if (option->arg == 0x11){
        strcpy(option->value, info.ip_addr);
    }
    return RG_DIALOG_VOID;
}

static rg_gui_event_t wifi_manage_slot_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    int slot = option->arg;
    rg_wifi_config_t config = {0};

    if (event == RG_DIALOG_INIT || event == RG_DIALOG_UPDATE || event == RG_DIALOG_ENTER)
    {
        rg_network_wifi_read_config(slot, &config);
        strcpy(option->value, config.ssid[0] ? config.ssid : _("(add network)"));
    }

    if (event == RG_DIALOG_ENTER)
    {
        if (!config.ssid[0])
        {
            // Get SSID from user
            char *ssid = rg_gui_input_str(_("Wi-Fi SSID"), _("Enter new network name:"), "");
            if (!ssid || strlen(ssid) == 0)
            {
                free(ssid);
                return RG_DIALOG_VOID;
            }

            // Get password from user
            char *password = rg_gui_input_str(_("Wi-Fi Password"), _("Enter password (leave empty for open network):"), "");
            if (!password)
                password = strdup("");

            // Save the configuration
            rg_wifi_config_t new_config = {0};
            strncpy(new_config.ssid, ssid, sizeof(new_config.ssid) - 1);
            strncpy(new_config.password, password, sizeof(new_config.password) - 1);
            new_config.channel = 0; // Auto
            new_config.ap_mode = false;

            free(ssid);
            free(password);

            if (!rg_network_wifi_write_config(slot, &new_config))
            {
                rg_gui_alert(_("Error"), _("Failed to save network configuration"));
                return RG_DIALOG_VOID;
            }

            rg_settings_commit();
            config = new_config;
            // fall through, allowing the user to connect to the new network
        }

        char title[50];
        snprintf(title, sizeof(title), "Slot %d: %.15s", slot, config.ssid);

        const rg_gui_option_t slot_options[] = {
            {1, _("Connect"),       NULL, RG_DIALOG_FLAG_NORMAL, NULL},
            {2, _("Edit SSID"),     NULL, RG_DIALOG_FLAG_NORMAL, NULL},
            {3, _("Edit Password"), NULL, RG_DIALOG_FLAG_NORMAL, NULL},
            {4, _("Delete"),        NULL, RG_DIALOG_FLAG_NORMAL, NULL},
            RG_DIALOG_END,
        };

        int action = rg_gui_dialog(title, slot_options, 0);

        switch (action)
        {
            case 1: // Connect
                rg_settings_set_boolean(NS_WIFI, SETTING_WIFI_ENABLE, true);
                rg_settings_set_number(NS_WIFI, SETTING_WIFI_SLOT, slot);
                wifi_toggle_interactive(true, slot);
                break;

            case 2: // Edit SSID
            {
                char *new_ssid = rg_gui_input_str(_("Edit SSID"), _("Enter new network name:"), config.ssid);
                if (new_ssid && strlen(new_ssid) > 0)
                {
                    strncpy(config.ssid, new_ssid, sizeof(config.ssid) - 1);
                    config.ssid[sizeof(config.ssid) - 1] = '\0';
                    rg_network_wifi_write_config(slot, &config);
                    rg_settings_commit();
                    rg_gui_alert(_("Success"), _("SSID updated"));
                }
                free(new_ssid);
                break;
            }

            case 3: // Edit Password
            {
                char *new_password = rg_gui_input_str(_("Edit Password"), _("Enter new password:"), config.password);
                if (new_password)
                {
                    strncpy(config.password, new_password, sizeof(config.password) - 1);
                    config.password[sizeof(config.password) - 1] = '\0';
                    rg_network_wifi_write_config(slot, &config);
                    rg_settings_commit();
                    rg_gui_alert(_("Success"), _("Password updated"));
                }
                free(new_password);
                break;
            }

            case 4: // Delete
                if (rg_gui_confirm(_("Delete Network"), _("Are you sure you want to delete this network configuration?"), false))
                {
                    rg_network_wifi_delete_config(slot);
                    rg_settings_commit();
                    rg_gui_alert(_("Success"), _("Network configuration deleted"));
                }
                break;
        }

        strcpy(option->value, config.ssid[0] ? config.ssid : _("(empty)"));
        return RG_DIALOG_REDRAW;
    }

    return RG_DIALOG_VOID;
}

static rg_gui_event_t wifi_manage_networks_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    if (event == RG_DIALOG_ENTER)
    {
        rg_gui_option_t slot_options[] = {
            {0, _("Slot 0"), "_", RG_DIALOG_FLAG_NORMAL, &wifi_manage_slot_cb},
            {1, _("Slot 1"), "_", RG_DIALOG_FLAG_NORMAL, &wifi_manage_slot_cb},
            {2, _("Slot 2"), "_", RG_DIALOG_FLAG_NORMAL, &wifi_manage_slot_cb},
            {3, _("Slot 3"), "_", RG_DIALOG_FLAG_NORMAL, &wifi_manage_slot_cb},
            {4, _("Slot 4"), "_", RG_DIALOG_FLAG_NORMAL, &wifi_manage_slot_cb},
            RG_DIALOG_END,
        };
        rg_gui_dialog(_("Manage Networks"), slot_options, 0);
        return RG_DIALOG_REDRAW;
    }
    return RG_DIALOG_VOID;
}

static rg_gui_event_t wifi_profile_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    int slot = rg_settings_get_number(NS_WIFI, SETTING_WIFI_SLOT, -1);
    rg_wifi_config_t config;
    if (rg_network_wifi_read_config(slot, &config))
        sprintf(option->value, "%d - %s", slot, config.ssid);
    else
        strcpy(option->value, _("None"));
    return RG_DIALOG_VOID;
}

static rg_gui_event_t wifi_access_point_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    if (event == RG_DIALOG_ENTER)
    {
        if (rg_gui_confirm(_("Wi-Fi AP"), _("Start access point?\n\nSSID: retro-go\nPassword: retro-go\n\nBrowse: http://192.168.4.1/"), true))
        {
            wifi_toggle_interactive(true, 9000);
        }
        return RG_DIALOG_REDRAW;
    }
    return RG_DIALOG_VOID;
}

static rg_gui_event_t wifi_enable_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    bool enabled = rg_settings_get_boolean(NS_WIFI, SETTING_WIFI_ENABLE, false);
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT || event == RG_DIALOG_ENTER)
    {
        enabled = !enabled;
        rg_settings_set_boolean(NS_WIFI, SETTING_WIFI_ENABLE, enabled);
        wifi_toggle_interactive(enabled, rg_settings_get_number(NS_WIFI, SETTING_WIFI_SLOT, -1));
        return RG_DIALOG_REDRAW;
    }
    strcpy(option->value, enabled ? _("On") : _("Off"));
    return RG_DIALOG_VOID;
}

static rg_gui_event_t wifi_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    if (event == RG_DIALOG_ENTER)
    {
        const rg_gui_option_t options[] = {
            {0x00, _("Wi-Fi enable"),       "-",  RG_DIALOG_FLAG_NORMAL,  &wifi_enable_cb      },
            {0x00, _("Manage networks"),    NULL, RG_DIALOG_FLAG_NORMAL,  &wifi_manage_networks_cb },
            RG_DIALOG_SEPARATOR,
            {0x00, _("Wi-Fi access point"), NULL, RG_DIALOG_FLAG_NORMAL,  &wifi_access_point_cb},
            RG_DIALOG_SEPARATOR,
            {0x00, _("Wi-Fi profile"),      "-",  RG_DIALOG_FLAG_MESSAGE, &wifi_profile_cb     },
            {0x10, _("Network"),            "-",  RG_DIALOG_FLAG_MESSAGE, &wifi_status_cb      },
            {0x11, _("IP address"),         "-",  RG_DIALOG_FLAG_MESSAGE, &wifi_status_cb      },
            RG_DIALOG_END,
        };
        rg_display_clear(0);
        rg_gui_dialog(option->label, options, 0);
        rg_display_clear(0);
    }
    
    return RG_DIALOG_VOID;
}

static rg_gui_event_t webui_switch_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    bool enabled = rg_settings_get_number(NS_APP, SETTING_WEBUI, 0);
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT || event == RG_DIALOG_ENTER)
    {
        enabled = !enabled;
        webui_stop();
        if (enabled){
            extern void webui_start();
            webui_start();
        }
        rg_settings_set_number(NS_APP, SETTING_WEBUI, enabled);
        rg_settings_commit();
    }
    strcpy(option->value, enabled ? _("On") : _("Off"));
    return RG_DIALOG_VOID;
}

static int rom_scan_index = 0;

static int scanroms_cb(const rg_scandir_t *file, void *arg)
{
    rg_gui_option_t* options = arg;
    
    size_t filename_len = strlen(file->basename);
    if(filename_len > RG_FILE_NAME_LEN_MAX){
        return RG_SCANDIR_CONTINUE;
    }
    
    char* namebuffer = malloc(filename_len + 1);
    if(namebuffer == NULL){
        rg_gui_alert("Fetal error", "memory overflow");
        ESP_LOGE(__FILE__, "memory overflow");
        abort();
    }
    
    memset(namebuffer, 0, filename_len + 1);
    memcpy(namebuffer, file->basename, filename_len);

    options[rom_scan_index] = (rg_gui_option_t){rom_scan_index, namebuffer, NULL, RG_DIALOG_FLAG_NORMAL, NULL};
    rom_scan_index++;

    if(rom_scan_index >= RG_FILE_LIST_MAX){
         return RG_SCANDIR_STOP;
    }
    return RG_SCANDIR_CONTINUE;
}

static rg_gui_event_t switch_game_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    if (event == RG_DIALOG_ENTER) 
    {
        rom_scan_index = 0;
        rg_gui_option_t options[RG_FILE_LIST_MAX + 1];
        rg_storage_scandir(RG_BASE_PATH_ROMS, scanroms_cb, &options, RG_SCANDIR_FILES);
        
        // 没有游戏ROM文件
        if(rom_scan_index == 0)
        {
            rg_display_clear(0);
            rg_gui_alert("No File", "There's no ROM file in the file system, Use 'WiFi Upload' function to add your ROMs.");
            rg_display_clear(0);
            return RG_DIALOG_VOID;
        }

        // 显示游戏ROM选择菜单
        options[rom_scan_index] = (rg_gui_option_t)RG_DIALOG_END;

        rg_display_clear(0);
        while(1){
            int sel = rg_gui_dialog(option->label, options, 0);

            if(sel >= 0){
                ESP_LOGI(__FILE__, "new nes rom file selected, check it: %s", options[sel].label);
            
                char path[RG_PATH_MAX] = RG_BASE_PATH_ROMS "/";
                strcat(path, options[sel].label);

                rg_display_clear(0);
                rg_gui_draw_message("Now loading: \"%s\" \nPlease wait..." , path);
                bool ret = rg_storage_load_rom_to_flash(path);
                if(!ret){
                    rg_display_clear(0);
                    rg_gui_alert("Error", "Not a vaild NES ROM file");    
                    continue;
                }

                rg_display_clear(0);
                rg_gui_alert("success", "NES rom loaded. \nSystem will restart.");
                esp_restart();
            }
            else{
                break;
            }
        }

        rg_display_clear(0);
    }
    return RG_DIALOG_VOID;
}

static void launcher_menu() {
	const rg_gui_option_t options[] = {
        {0, _("Switch game"),   NULL, RG_DIALOG_FLAG_NORMAL, &switch_game_cb},
		{0, _("Wi-Fi options"), NULL, RG_DIALOG_FLAG_NORMAL, &wifi_cb},
        {0, _("File Server"),   "-",  RG_DIALOG_FLAG_NORMAL, &webui_switch_cb},
        RG_DIALOG_SEPARATOR,
		{2, _("Restart"),         NULL, RG_DIALOG_FLAG_NORMAL, NULL},
		RG_DIALOG_END,
	};

    int ret = -1;
	while (true) {
        ret = rg_gui_dialog(_("Main menu"), options, ret);
		if(ret == 2){
            ESP_LOGI(__FILE__, "system restart requested, commit settings.");
            rg_settings_commit();
            if (rg_gui_confirm(_("Restart machine?"), NULL, false)) {
                esp_restart();
            }
            rg_display_clear(0);
		}
	}

    esp_restart();
}

void launcher_main() {
    ESP_ERROR_CHECK(nvs_flash_init());

    const rg_handlers_t handlers = {
		.event = &event_handler,
        .options = &options_handler,
	};

	app = rg_system_init(32000, &handlers, NULL);
    app->isLauncher = true;

	if (!rg_storage_ready()) {
		rg_display_clear(C_SKY_BLUE);
		rg_gui_alert(_("Storage Error"), _("Storage mount failed.\nMake sure the card is FAT32."));
	} else {
		rg_storage_mkdir(RG_BASE_PATH_CACHE);
		rg_storage_mkdir(RG_BASE_PATH_CONFIG);
		rg_storage_mkdir(RG_BASE_PATH_ROMS);
	}

    rg_network_init();
    launcher_menu();

	while ((1)) {
        vTaskDelay(1000);
	}

    esp_restart();
}