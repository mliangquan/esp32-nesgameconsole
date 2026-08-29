# ESP32-NESEMU

基于 [retro-go](https://github.com/ducalex/retro-go) 框架与 nofrendo 核心二次开发的 ESP32 NES 模拟器。

此项目是[oshwhub开源平台](https://oshwhub.com/)上的[基于ESP32的NES游戏掌机](https://oshwhub.com/micespring/esp32-nesemu)的官方固件。

此项目仅保留了 retro-go 的核心框架以及 NES 模拟器功能（所以只能运行 NES / FC 游戏），并进行了一定程度的精简和魔改。硬件上，与 retro-go 最大的区别是无需 SD 卡，完全利用片上 Flash 来存储游戏 ROM 、存档、配置和其他文件。同时，无需使用带有 PSRAM 扩展的 ESP32 模组。这大幅度降低了硬件成本。当然，受限于模组 Flash 的大小，你可能无法将你完整的游戏收藏都打包进去。但是，你可以精选一些你最喜欢的游戏来随时游玩:)

特性：

- 使用 mmap 映射的方式从 Flash 加载 NES ROM，无需PSRAM扩展
- 通过在 Flash 上的虚拟文件系统同时存储多份游戏（或其他文件），无需外部 SD 卡。
- 支持连接 WiFi 后通过网页上传游戏或备份文件
- 支持存档保存 / 加载、调色板、缩放、跳帧等选项
- 通过内置DAC支持游戏音频

## 快速开始

如果你只想简单的享受游戏，而且你的硬件配置与开源平台上的项目完全一致。那么请根据你使用的 ESP32 模组的 Flash 大小，选择 `release` 目录中的固件烧录至 Flash 地址 `0x0` 即可。

因为我手边没有 16MB Flash 容量的 ESP32-WROOM 模组，所以我没有编译对应的固件。你可以参考下边的教程自行编译。

## 目录结构

```
ESP32-NESEMU/
├── main/                    # 主程序（main.c 启动逻辑、launcher 启动器、webui 网页上传等）
├── components/
│   ├── retro-go/            # retro-go 框架（显示、音频、输入、文件系统、GUI）
│   └── nofrendo/            # NES 模拟器核心（PPU/CPU/APU/mappers）
├── assets/initfs/           # fs 分区的初始内容
├── release                  # 一些预编译好的固件，烧录至Flash的0x0地址即可
├── partitions_4M_512K.csv   # 4MB Flash 分区表（nes_rom 512KB）
├── partitions_4M_1024K.csv  # 4MB Flash 分区表（nes_rom 1MB）
├── partitions_8M.csv        # 8MB Flash 分区表
├── partitions_16M.csv       # 16MB Flash 分区表
└── sdkconfig                # ESP-IDF menuconfig 配置
```

## 自行编译固件

### 环境准备

1. 安装 ESP-IDF（推荐 v5.5.x）。参考官方文档：<https://docs.espressif.com/projects/esp-idf/>

2. 每次重新打开终端后，都需要先导入 IDF 环境：
   
   Linux
   ```bash
   # 定位到IDF的安装目录
   cd ~/.espressif/v5.5.5/esp-idf/

   # 执行脚本
   source export.sh
   ```
   
   Windows请参考：[在 Windows 上通过命令行构建项目](https://docs.espressif.com/projects/esp-idf/zh_CN/stable/esp32/get-started/windows-start-project.html)

   成功后 `idf.py`、`esptool.py` 等命令可用。

3. 测试环境：
   ```bash
   idf.py --version
   ```
   此时控制台应该输出当前使用的IDF的版本，类似于：
   ```bash
   ESP-IDF v5.5.5
   ```

### 配置、编译与烧录

#### 配置menuconfig

1. 运行
   ```bash
   idf.py menuconfig
   ```

2. 切换到`Serial flasher config`页面，根据你的模组型号，修改Flash大小，默认的大小为8MB

3. 切换到`Partition Table`页面，根据你的模组型号，选择对应的`partitions_XX.csv`，比如 8M Flash 的模组，就选择`partitions_8M.csv`
   > 注意，适用于 4M Flash 模组的分区文件有两个，其尾缀分别为"4M_512K"和"4M_1024K"。最后的数字表示 NES_ROM 分区的大小，它决定你可以运行的游戏 ROM 文件大小的上限（实际为这个大小减去文件头的大小，约300Byte）。更大的分区可以运行更大的游戏，但是可供文件系统使用的空间就会变小。

4. 保存后关闭

#### 编译和烧录固件

```bash
# 编译并烧录
idf.py build flash
```

### 打包导出固件

如果需要将固件打包为单个 bin 文件，以方便分享和保存，可以执行以下的命令。注意，这里面的参数对应 8M Flash 的模组，其他的模组请自行修改。
```
esptool.py --chip esp32 merge_bin \
-o release/esp32_nesemu_8M.bin \
-f raw --flash_mode dio --flash_freq 80m \
--flash_size 8MB \
0x1000   build/bootloader/bootloader.bin \
0x8000   build/partition_table/partition-table.bin \
0x10000  build/ESP32-NESEMU.bin \
0x2d0000 build/fs.bin 
```

## 游戏管理与使用

- **启动器**：开机（或复位，下同）时按住选择键（select键）即可进入启动器（`launcher`）。可执行切换游戏（加载游戏到 nes_rom）、配置 WiFi 、启动 WiFi 网页上传功能等操作。如果启动时 `nes_rom` 分区中没有有效的游戏 ROM，那么也会自动进入启动器。
- **WiFi 网页上传**：连接 WiFi 后，可在WiFi选项菜单中查看本机 IP 地址。此时在电脑上通过浏览器**直接访问此IP地址**(类似 `http://192.168.XX.XX`)，即可通过网页上传游戏或执行其他文件管理操作
- **ROM 文件存放位置**：将 NES ROM 文件（`*.nes`）上传到 `/fs/roms/`下即可。注意文件名中尽量不要带有中文和特殊字符。已经上传的 ROM 文件会在“切换游戏”中显示。
- **已加载的 ROM**：如果已经通过启动器的“切换游戏”功能，加载了游戏 ROM 到 `nes_rom` 分区中。则开机后会直接进入游戏。如果游戏有可用的存档，还会直接加载最新的存档。
- **游戏内菜单**：游戏内按菜单键（Flash键）唤出菜单。可以更改选项以及执行保存、读取存档等操作。
- **存档文件**：存档存储在 `fs` 分区的 `retro-go/saves/` 目录中。
- **配置文件**：存档存储在 `fs` 分区的 `retro-go/config/` 目录中。

## 版本历史

- **v0.3.0** 2026-08-16 00:19
  - 在模拟器菜单中增加一个间隔 N 帧跳 1 帧的选项，以规避默认跳 1 帧的显示相位与游戏中某些闪烁的相位重叠，导致精灵无法正常显示的问题。
- **v0.2.0** 2026-08-02 02:03
  - 加载 NES ROM 时，弹窗显示状态
  - 关闭 VRAM 写入检查，以支持一些没有按照 NES 标准开发的游戏
  - 优化加载游戏UI界面和操作逻辑
  - 优化启动器网络部分相关逻辑
- **v0.1.1**
  - 交换 AB 键映射，使其和 NES 默认布局一致
  - 修复了一个汉化版《星之卡比》运行时精灵错误显示的问题
- **v0.1.0** 2026-07-25 12:46
  - 基本实现目标功能的第一版本：
    - NES 模拟器基本功能
    - 存档保存 / 加载功能
    - 多项自定义选项，比如调色板、缩放模式等
    - 可以一次性存储多个游戏，随时切换
    - 支持连接 WiFi，然后通过网页上传游戏
    - 内部文件管理功能
  - 已知问题：
    - 因为内部 SRAM 空间不足，所以存档时无法保存截图
- 2026-07-23 19:21
  - 可以正常从 Flash 加载 ROM 并运行

## TODOS

- [ ] retro-go 默认以 30FPS 模拟游戏，可否提升？
- [ ] 删除或条件化编译无用或作用不大的选项（比如音量调节）
- [ ] 优化文件上传的 UI 交互，增加更清晰的状态指示
- [ ] 在文件上传时禁用相关操作
- [ ] 精简无用代码
- [ ] 修复存档的截图问题

## 依赖

```bash
idf.py add-dependency "joltwallet/littlefs^1.22.2"
idf.py add-dependency "espressif/cjson^1.7.19~2"
```
