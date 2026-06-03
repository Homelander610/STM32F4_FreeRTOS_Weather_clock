# STM32F4 FreeRTOS 桌面天气预报时钟

本项目来源于B站up主“梅花嵌入式”所出的开源项目，旨在借此项目学习在STM32F4平台上掌握三种常用的通信协议（USART/I2C/SPI），并应用FreeRTOS对项目进行优化，摈弃纯裸机架构下的循环等待，而是采用FreeRTOS的抢占式调度以及软件定时器实现具体功能。本项目上传至仓库仅为了熟悉github使用，如需此项目进行具体实现，不妨移步至梅花嵌入式本人的git仓库观其源码。

基于 STM32F407 + FreeRTOS 的桌面天气预报时钟，通过 ESP32 WiFi 模块联网获取天气与网络时间，配合 AHT20 传感器采集室内温湿度，在 ST7789 TFT 屏幕上分模块展示。

## 硬件

| 组件 | 型号 | 接口 |
|------|------|------|
| 主控 | STM32F407ZGT6 (Cortex-M4, 168MHz) | — |
| 显示屏 | ST7789 2.4" TFT LCD (240×320, RGB565) | SPI2 |
| WiFi 模块 | ESP32 (AT 固件) | USART3 |
| 温湿度传感器 | AHT20 | I2C1 |
| RTC 时钟 | 片内 RTC + 外部 32.768kHz LSE | — |
| 串口调试 | USART1 (printf 重定向) | — |

## 功能

- **网络时间同步**：通过 ESP32 SNTP 协议获取北京时间，同步到片内 RTC，每小时自动校准
- **实时时间显示**：主页面顶部模块以大字显示时:分，附带日期与星期
- **室内环境监测**：AHT20 采集温湿度，数值变动时实时刷新
- **天气预报**：通过心知天气 API 获取室外温度与天气状况，按天气码匹配对应图标（晴/多云/阴/雨/雪/雷阵雨等）
- **WiFi 状态**：实时显示当前连接 WiFi 的 SSID，断开时即时提示
- **多页面切换**：欢迎页 → WiFi 连接等待页 → 主页面，异常时跳转错误页

## 软件架构

```
┌──────────────────────────────────────┐
│              main.c                  │
│   board_lowlevel_init → 时钟树配置   │
│   workqueue_init       → 工作队列    │
│   xTaskCreate(main_init)             │
└──────────────┬───────────────────────┘
               │
     ┌─────────▼──────────┐
     │  main_init 任务     │
     │  · board_init       │
     │  · ui_init          │
     │  · welcome_page     │
     │  · wifi_init → 配网  │
     │  · main_page_display│
     │  · app_init → 定时器 │
     └─────────────────────┘
```

**定时器调度**（FreeRTOS 软件定时器）：

| 定时器 | 任务 | 周期 | 执行方式 |
|--------|------|------|---------|
| time_update | RTC 时间刷新屏幕 | 1秒 | 定时器回调直接执行 |
| wifi_update | WiFi 状态检测 | 5秒 | 工作队列代理 |
| inner_update | AHT20 温湿度读取 | 3秒 | 工作队列代理 |
| time_sync | SNTP 网络对时 | 1小时（失败则1秒重试） | 单次定时器 + 工作队列 |
| outdoor_update | 天气 API 拉取 | 10分钟 | 单次定时器 + 工作队列 |

> 耗时操作（AT 指令交互、HTTP 请求、I2C 等待）通过 **工作队列（workqueue）** 代理执行，避免阻塞 FreeRTOS 定时器服务任务 `TmrSvc`。

## 目录结构

```
├── app/                     # 应用层
│   ├── main.c               # 入口
│   ├── board.c/h            # 板级初始化（时钟、外设、重定向 printf）
│   ├── app.c/h              # 定时器创建与调度
│   ├── ui.c/h               # UI 基础绘图（填充、字符串、图片）
│   ├── weather.c/h          # 心知天气 JSON 解析
│   ├── wifi.c/h             # WiFi 连接流程
│   ├── workqueue.c/h        # 工作队列实现
│   ├── stm32f4xx_it.c/h     # 中断服务
│   ├── page/                # 页面渲染
│   │   ├── welcome_page.c
│   │   ├── wifi_page.c
│   │   ├── main_page.c      # 主界面（时间/室内/室外三模块）
│   │   └── error_page.c
│   ├── font/                # 点阵字库（16~76px）
│   └── image/               # 天气图标位图
├── driver/                  # 外设驱动
│   ├── aht20/               # AHT20 温湿度传感器
│   ├── esp32/               # ESP32 AT 指令驱动（含 HTTP GET / SNTP）
│   ├── st7789/              # ST7789 屏幕驱动
│   ├── rtc/                 # 片内 RTC 驱动
│   ├── console/             # USART 控制台
│   └── tim_delay/           # 微秒延时
├── firmware/                # STM32F4 标准外设库 + CMSIS
├── third_lib/               # FreeRTOS v10.x
├── mdk/                     # Keil MDK 工程文件
│   ├── stm32f407.uvprojx    # 工程文件
│   └── stm32f407.uvoptx     # 工程选项
└── resources/               # 设计稿与参考图片
```

## 构建

1. 安装 **Keil MDK-ARM v5**（需 STM32F4 Device Family Pack）
2. 打开 `mdk/stm32f407.uvprojx`
3. 编译（F7），下载（F8）

## 使用

1. 修改 `app/wifi.h` 中的 `WIFI_SSID` 和 `WIFI_PASSWD` 为实际 WiFi 信息
2. 确认天气 API key 有效（`app/app.c` 中 `weather_url` 的 `key=` 参数，当前使用心知天气免费版）
3. 上电后自动连接 WiFi → 同步时间 → 拉取天气 → 进入主界面

## 依赖

- [STM32F4 Standard Peripheral Library](https://www.st.com/en/embedded-software/stm32-standard-peripheral-libraries.html)
- [FreeRTOS](https://www.freertos.org/)
- [心知天气 API](https://www.seniverse.com/)
