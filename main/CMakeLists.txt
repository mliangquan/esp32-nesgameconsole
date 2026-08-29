idf_component_register(
    SRC_DIRS            # list the source files of this component
        "."
    INCLUDE_DIRS        # optional, add here public include directories
        "."
    PRIV_INCLUDE_DIRS   # optional, add here private include directories
    REQUIRES            # optional, list the public requirements (component names)
    PRIV_REQUIRES       # optional, list the private requirements
        "nofrendo"
	    "retro-go" 
        "esp_netif"
        "esp_wifi"
        "esp_http_server"
)

# 添加测试文件
# littlefs_create_partition_image(fs ../assets/initfs FLASH_IN_PROJECT)