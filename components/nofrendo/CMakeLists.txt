# 注册组件
idf_component_register(
    SRC_DIRS            # list the source files of this component
        "."
        "nes"
        "mappers"
    	
    INCLUDE_DIRS        # optional, add here public include directories
        "."
    	
    PRIV_INCLUDE_DIRS   # optional, add here private include directories
    REQUIRES            # optional, list the public requirements (component names)
    PRIV_REQUIRES       # optional, list the private requirements
    	"retro-go"
)

# 组件编译选项
component_compile_options(
	-O3
	-Wno-array-bounds
	-Wno-error=format
	-Wno-format
    -DRETRO_GO
#   -DNOFRENDO_DEBUG
)
