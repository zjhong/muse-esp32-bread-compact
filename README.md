# Muse Gadget on ESP32-S3 Bread Compact WiFi LCD

An unofficial board port of the [Muse Gadget ESP32 SDK](https://github.com/facebookincubator/muse-gadget-sdk) for the ESP32-S3-N16R8 development kit bundled with the Bread Compact WiFi LCD expansion board.

[简体中文](#简体中文) · [English](#english)

## 简体中文

### 项目说明

本仓库基于 Meta 的 [Muse Gadget SDK](https://github.com/facebookincubator/muse-gadget-sdk)，为 **ESP32-S3-N16R8 + Bread Compact WiFi LCD** 增加板级支持，方便复用屏幕、扬声器、麦克风和 BOOT 按键。

当前适配依据这块套件的实际硬件和 `xiaozhi-esp32` 的 [Bread Compact 引脚定义](https://github.com/78/xiaozhi-esp32/blob/main/main/boards/bread-compact-wifi-lcd/config.h)。已在一块 ESP32-S3-N16R8（16 MB Flash、8 MB PSRAM）上构建、刷写并运行；屏幕和扬声器已确认可用。其他批次的扩展板可能有不同接线，请先核对引脚。

这是社区设备配置，配对流程使用 Muse 的社区设备模式，不包含制造商 eFuse 身份证明。此仓库提供源代码和可复现的构建方式，不提供预编译固件：每位使用者都应在本机填写自己的 Muse SDK token 和网络凭据后编译。

### 硬件

- ESP32-S3-N16R8：16 MB Flash、8 MB Octal PSRAM
- Bread Compact WiFi LCD 扩展板：ST7789，240 × 320 屏幕
- 板载音频编解码器、扬声器、麦克风和 BOOT 按键
- CH340C USB 转串口

### 环境准备

- macOS 或 Linux
- Espressif ESP-IDF **v6.0.1**，并已加载 `export.sh`，使 `idf.py` 可用
- USB 数据线；刷写时使用板上的 CH340C 串口
- Muse 账户中的 SDK token：`gadgets.muse.ai → Account → SDK tokens`

### 配置、构建和刷写

在仓库的 `esp32` 目录配置板型和本机凭据：

```sh
cd muse-esp32-bread-compact/esp32
idf.py -B build-muse-bread-compact-wifi-lcd \
  -DIDF_TARGET=esp32s3 \
  -DSDKCONFIG=build-muse-bread-compact-wifi-lcd/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;devices/sdkconfig.muse;devices/sdkconfig.muse-bread-compact-wifi-lcd" \
  menuconfig
```

在 **ESP32 Device SDK** 菜单中填写自己的 **Muse Gadgets SDK token**。通常可通过 Muse App 配置 Wi‑Fi；如需固件启动后直接连接指定热点，也可在此设置 **WiFi SSID override** 和 **WiFi password override**。这些值只应保存在本机生成的 `build-muse-bread-compact-wifi-lcd/sdkconfig` 中，该目录由 Git 忽略。含有这些配置的固件也包含凭据，请勿上传或转发 `.bin`、`.elf` 或整个 `build-*` 目录。

保存配置后构建并刷写：

```sh
tools/muse/board.sh build bread-lcd
tools/muse/board.sh flash bread-lcd /dev/cu.usbserial-110
```

把串口路径替换为本机识别到的 CH340C 端口。macOS 可用 `ls /dev/cu.usb*` 查看；Linux 常见路径为 `/dev/ttyUSB0` 或 `/dev/ttyACM0`。如果自动下载模式没有启动，按住 BOOT，轻按 RESET，再松开 BOOT，然后重新刷写。

普通 `flash` 会写入引导程序、分区表和应用，并保留 NVS 中已有的配对数据；不要使用 `erase-flash`，除非确实要清除配对和网络配置。

### 首次配对

1. 在 Muse App 的 **Settings → Devices** 中启用 **Developer mode**。
2. 选择 **Add Device**，连接列表中应出现 `MuseGadget-XXXXXX`。
3. 按设备屏幕提示短按 BOOT，确认配对。
4. 按 App 提示完成账户和 Wi‑Fi 设置。

本板 BOOT 键也用于语音交互；只在设备出现配对确认提示时短按确认。

### 常见问题

- **App 看得到设备但连接失败**：保持设备供电并靠近手机，退出后重开 Muse App，再尝试一次。不要连续重复提交 Wi‑Fi 密码。若仍失败，串口日志可帮助区分 BLE 连接、Wi‑Fi 认证和 Muse 服务连接阶段。
- **Wi‑Fi 认证失败**：先确认热点开启，使用 2.4 GHz WPA2 网络，并核对 SSID 和密码。部分路由器的 WPA3-only 模式、访客隔离或 MAC 白名单会阻止设备接入。
- **连接后很快掉线**：`main/muse_glue.c` 包含对编译期 Wi‑Fi 覆盖的同步处理，避免 Muse 网络管理器把该网络误判为“已忘记”而断开。
- **屏幕、音频接线不同**：先检查 `components/muse/boards/board_bread_compact_wifi_lcd.c` 中的 GPIO 和音频配置；不同版本扩展板不要盲目照搬。

### 上游来源与许可证

本项目基于 [facebookincubator/muse-gadget-sdk](https://github.com/facebookincubator/muse-gadget-sdk) 的 ESP32 源码快照（基础提交 [`b1a3822`](https://github.com/facebookincubator/muse-gadget-sdk/commit/b1a3822)），并增加 Bread Compact 板级适配和配套工具改动。上游代码采用 **Apache License 2.0**，本项目保留其许可证和原始版权声明。由于仓库包含上游代码，不能把整个派生项目简单改标为 MIT。

上游源码附带的 `esp32/dev_signing_key.pem` 是公开共享的开发测试密钥，只用于开发构建，不构成生产安全边界。正式产品请使用自己的签名密钥，并建立独立的安全启动和密钥管理流程。

## English

### About

This repository adapts Meta's [Muse Gadget ESP32 SDK](https://github.com/facebookincubator/muse-gadget-sdk) for the **ESP32-S3-N16R8 + Bread Compact WiFi LCD** kit, including its display, speaker, microphone, and BOOT button.

The port follows the kit's hardware and the [Bread Compact pin profile](https://github.com/78/xiaozhi-esp32/blob/main/main/boards/bread-compact-wifi-lcd/config.h) from `xiaozhi-esp32`. It has been built, flashed, and run on one ESP32-S3-N16R8 board (16 MB flash, 8 MB PSRAM); the display and speaker were confirmed working. Expansion boards from other batches may be wired differently, so verify their pinout first.

This is a community-device configuration. Pairing uses Muse's community-device mode and does not include manufacturer eFuse attestation. This repository provides source and reproducible build steps, not prebuilt firmware. Each user should enter their own Muse SDK token and network credentials locally before building.

### Hardware

- ESP32-S3-N16R8: 16 MB flash and 8 MB Octal PSRAM
- Bread Compact WiFi LCD expansion board: ST7789, 240 × 320 display
- On-board audio codec, speaker, microphone, and BOOT button
- CH340C USB-to-UART bridge

### Requirements

- macOS or Linux
- Espressif ESP-IDF **v6.0.1**, with `export.sh` loaded so `idf.py` is available
- USB data cable connected to the board's CH340C port
- An SDK token from `gadgets.muse.ai → Account → SDK tokens`

### Configure, build, and flash

Configure the board and local credentials from the repository's `esp32` directory:

```sh
cd muse-esp32-bread-compact/esp32
idf.py -B build-muse-bread-compact-wifi-lcd \
  -DIDF_TARGET=esp32s3 \
  -DSDKCONFIG=build-muse-bread-compact-wifi-lcd/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;devices/sdkconfig.muse;devices/sdkconfig.muse-bread-compact-wifi-lcd" \
  menuconfig
```

In the **ESP32 Device SDK** menu, enter your own **Muse Gadgets SDK token**. Wi‑Fi can usually be provisioned in the Muse app. If the firmware should connect to a specific hotspot at boot, you can also set **WiFi SSID override** and **WiFi password override** here. Keep these values only in the locally generated `build-muse-bread-compact-wifi-lcd/sdkconfig`; Git ignores this directory. Firmware built with these settings contains the credentials. Do not upload or share the `.bin`, `.elf`, or the `build-*` directory.

Build and flash:

```sh
tools/muse/board.sh build bread-lcd
tools/muse/board.sh flash bread-lcd /dev/cu.usbserial-110
```

Replace the serial path with the CH340C port on your machine. On macOS, list ports with `ls /dev/cu.usb*`; on Linux, common paths are `/dev/ttyUSB0` and `/dev/ttyACM0`. If the board does not enter download mode automatically, hold BOOT, tap RESET, release BOOT, and retry.

A normal `flash` writes the bootloader, partition table, and application while preserving existing pairing data in NVS. Do not run `erase-flash` unless you intend to clear pairing and network settings.

### Pair the device

1. In the Muse app, open **Settings → Devices** and enable **Developer mode**.
2. Select **Add Device**. The list should show `MuseGadget-XXXXXX`.
3. Briefly press BOOT when the device screen asks you to confirm pairing.
4. Follow the app to finish account pairing and Wi‑Fi setup.

On this board, BOOT is also used for voice interaction. Press it briefly for pairing only when the device displays a confirmation prompt.

### Troubleshooting

- **The app sees the device but cannot connect**: Keep the board powered and close to the phone, force-quit and reopen the Muse app, then try once. Avoid repeatedly submitting the Wi‑Fi password. If it still fails, serial logs can distinguish BLE connection, Wi‑Fi authentication, and Muse service stages.
- **Wi‑Fi authentication fails**: Make sure the hotspot is on and use a 2.4 GHz WPA2 network. Check the SSID and password. WPA3-only mode, guest isolation, or MAC allow-lists can prevent a connection.
- **Wi‑Fi drops after connecting**: `main/muse_glue.c` synchronizes the build-time Wi‑Fi override with Muse's network manager, preventing it from treating the network as forgotten and disconnecting it.
- **Display or audio wiring differs**: Check the GPIO and audio configuration in `components/muse/boards/board_bread_compact_wifi_lcd.c`. Do not assume every expansion-board revision has the same wiring.

### Upstream and license

This project is based on the ESP32 source snapshot from [facebookincubator/muse-gadget-sdk](https://github.com/facebookincubator/muse-gadget-sdk), at base commit [`b1a3822`](https://github.com/facebookincubator/muse-gadget-sdk/commit/b1a3822), with the Bread Compact board port and supporting tool changes. The upstream code is licensed under the **Apache License 2.0**; this repository retains that license and the original copyright notices. Because the repository contains upstream code, the complete derivative project cannot simply be relicensed as MIT.

The upstream `esp32/dev_signing_key.pem` is a publicly shared development/test key. It is for development builds only and is not a production security boundary. Use your own signing key and a separate secure-boot and key-management process for production products.
