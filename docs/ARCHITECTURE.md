# 架构说明

AllNES 采用分层设计，依赖方向严格单向：

```
        host/  (SDL2 前端: 窗口/音频设备/输入/主循环)
          │
          │ 实现 core/include/port 中的接口
          ▼
   core/port  (IVideoSink, IAudioSink, IInputSource)
          ▲
          │ 只依赖抽象接口
          │
        core/  (Emulator, CPU, PPU, APU, MainBus, Mapper, Cartridge)
```

- **core 不知道 host 的存在**。它不 include 任何 SDL/OS 头文件，只使用
  标准库和 `core/include/port` 中的纯虚接口。
- **host 是组合根（composition root）**：创建具体实现，注入 `Emulator`，
  负责窗口、事件、帧率与音频设备。
- 新增平台（例如 Qt、WebAssembly、Android）只需要再写一份 host，实现三个
  port 即可；新增无窗口用途（录制、自动化测试）复用 `tools/headless.cpp`。

## 启动器层

`launcher/` 与 `core/` 同级，不依赖模拟器内核。启动器只有一个 UI 实现
（LVGL），但它不直接接触窗口系统：UI 之下是 display 抽象层，由 host 通过
`--display` 选择具体后端。

```
host/main.cpp
  └─ launcher/ui/lvgl_launcher.h   LvglLauncherApp::run() -> LaunchRequest
       │   （只依赖 display 接口，不 include SDL）
       ├─ launcher/display/display.h         Display 抽象 + 事件/配置/Backend
       ├─ launcher/display/display_sdl.cpp   SDL2 窗口、纹理、事件、计时
       ├─ launcher/display/display_headless.cpp  离屏 ARGB 缓冲（CI/截图）
       ├─ launcher/model/                    扫描 ROM、iNES 头探测、配置持久化
       └─ launcher/lvgl/lv_conf.h            LVGL 构建配置（项目所有）
```

依赖方向严格为 `host → ui → display`；`display` 不依赖 LVGL，`ui` 不依赖
SDL。`LaunchRequest` 定义在 `launcher/types.h`，是 host 与 UI 之间的边界类型。

- 交互状态（页签、选中项、搜索词、最近游玩、分辨率）都在
  `lvgl_launcher.cpp` 的 `Impl` 中；UI 只消费 display 归一化后的事件。
- 该层编译为两个静态库：`allnes_display`（接口 + 后端）和
  `allnes_launcher`（UI + model）。`allnes_launcher_model_test` 只编译
  `model/`，`allnes_display_headless_test` 只链接 `allnes_display`，两者都
  不需要窗口系统，可在无头 CI 中运行。

### Display 抽象层

`launcher/display/display.h` 是纯 C++ 接口，像素格式统一为 ARGB8888
（`uint32_t` 的 `0xAARRGGBB`）：

```cpp
class Display {
    virtual bool init(const Config&, std::string& error) = 0;
    virtual void shutdown() = 0;
    virtual bool ready() const = 0;

    virtual void updateRect(int x, int y, int w, int h,
                            const void* pixels, int stride) = 0;  // 暂存脏矩形
    virtual void present() = 0;                                   // 真正显示
    virtual bool readPixels(std::vector<std::uint32_t>&, int&, int&) = 0;

    virtual bool pollEvent(Event&) = 0;       // 归一化事件，false = 已排空
    virtual void setTextInput(bool) = 0;

    virtual void show() = 0;
    virtual void hide() = 0;
    virtual void setFullscreen(bool) = 0;
    virtual bool fullscreen() const = 0;

    virtual std::uint32_t ticks() const = 0;  // 毫秒
    virtual void delay(std::uint32_t) = 0;

    virtual int canvasWidth() const = 0;
    virtual int canvasHeight() const = 0;
    virtual int renderScale() const = 0;      // 相对 1920×1080 的倍率（1 或 2）
};
```

关键契约：

- `updateRect` 只把 LVGL 的脏矩形上传到后端纹理；`present()` 才清屏、
  合成并显示。
- `readPixels` 在 `present()` 之前也能拿到完整当前帧（SDL 后端先
  `compose()` 再 `SDL_RenderReadPixels`，不调用 `SDL_RenderPresent`），
  因此截图不会得到空白帧。
- 事件里的指针坐标已经换算到渲染画布像素，UI 直接交给 LVGL，不再做坐标
  缩放。鼠标坐标由 SDL 自己换算：`SDL_RenderSetLogicalSize` 会安装
  renderer event watch，在事件到达应用前就把鼠标坐标改写成画布坐标。
  **不要**再对鼠标事件调用 `SDL_RenderWindowToLogical`，否则会对已缩放
  的值二次缩放，产生随窗口大小变化的偏移（这正是之前点击位置偏移的
  原因）。触摸事件不在 watch 的统一约定内，见下一条。
- 触摸由 `SDL_FINGERDOWN/MOTION/UP` 归一化到同一种鼠标事件。两个 SDL
  实现对 finger 事件坐标的处理并不一致：SDL2 的 renderer event watch 会
  把它改写成相对 viewport 的 0–1 比例（会扣掉黑边），sdl2-compat 则保持
  驱动给出的、相对窗口的 0–1 比例。后端因此**不直接使用事件里的坐标**，
  而是从 `SDL_GetTouchFinger` 读取未被 watch 改写的触摸设备状态（两边都是
  窗口相对比例），乘以窗口尺寸后再用 `SDL_RenderWindowToLogical` 换算到
  画布，并对落在黑边上的点夹到画布边缘。抬起事件到达时手指已从设备状态
  中移除，所以按下/移动时缓存最后画布坐标，抬起沿用该位置。同时关闭
  SDL 的 touch↔mouse 合成，避免同一次点击被两套坐标各处理一遍。
- `canvasWidth/Height = 设计尺寸 × renderScale`。UI 用该尺寸创建 LVGL
  display，并把所有布局尺寸、字号、圆角、边框、内边距乘以 `renderScale`
  （`LV_SIZE_CONTENT`、`LV_PCT` 等特殊坐标值除外）。

### SDL2 后端

- 窗口默认 1600×900 点、`RESIZABLE | ALLOW_HIGHDPI`，最小 960×540；
  `SDL_RENDERER_ACCELERATED | PRESENTVSYNC`，失败退回软件渲染器。
- 由 drawable 尺寸计算倍率：`ceil(drawableWidth / 1920)` 并限制在 1–2。
  Retina 上 3200×1800 的 drawable → `renderScale = 2`、画布 3840×2160；
  SDL 把画布降采样到窗口，文字与封面比 1920 画布直接放大更锐利。
- `SDL_RenderSetLogicalSize(renderer, canvasW, canvasH)` 让窗口坐标直接映射
  到画布坐标，并安装 renderer event watch 自动改写输入事件坐标；流式
  `ARGB8888` 纹理以画布尺寸创建，`BLENDMODE_NONE`。
- 事件翻译集中在 `translate()`/`keyFromScancode()`：只保留 UI 需要的按键，
  Shift/Ctrl/Alt 从 `KMOD_*` 映射；`SDL_WINDOWEVENT` 的 LEAVE/FOCUS_LOST
  归一化为 `FocusLost`，EXPOSED/RESTORED/SIZE_CHANGED 归一化为
  `Expose`/`Resize`。
- 触摸：`SDL_HINT_TOUCH_MOUSE_EVENTS` / `SDL_HINT_MOUSE_TOUCH_EVENTS` 关闭
  合成，只用 finger 事件驱动指针；多指只跟踪第一根手指（第二根手指不会
  移动指针或释放按键）。坐标取自 `SDL_GetTouchFinger` 的设备状态并缓存，
  抬起时沿用最后一次画布位置。设置 `ALLNES_LAUNCHER_INPUT_DEBUG=1` 可打印
  事件坐标、window/drawable/画布尺寸和最终画布坐标，用来排查平台缩放导致
  的偏移。
- `ticks/delay` 使用 `std::chrono::steady_clock`，不依赖 `SDL_GetTicks`。

### Headless 后端

- 不初始化 SDL video、不创建窗口；内部维护 1920×1080 的 ARGB 缓冲。
- `present()` 只累计帧号；`readPixels` 返回整幅；`pollEvent` 恒为 false；
  `show/hide/setFullscreen/setTextInput` 为空操作。
- `renderScale()` 固定为 1，因此无头截图始终是 1920×1080。
- `delay()` 真正 sleep，保持与窗口后端一致的帧节奏，不会占满 CPU。

### LVGL UI 层

- LVGL 以 `third_party/lvgl/` 原样 vendored（v10.0.0-dev），源码不做任何修改；
  项目配置是 `launcher/lvgl/lv_conf.h`，顶层 CMake 用 `LV_BUILD_CONF_PATH`
  传给 LVGL，并用 INTERFACE 编译定义保证所有 consumer 看到同一份配置。
  依赖 FreeType；找不到时整个 UI 在 CMake 阶段关闭，直接传 ROM 仍可运行。
- **不使用 `lv_sdl_window_create()`**：它会安装自己的 SDL 事件定时器，且用
  窗口尺寸直接决定 display 分辨率。LVGL 只作为渲染库使用：UI 用
  `lv_display_create(canvasW, canvasH)` + 自定义 flush 回调把脏矩形交给
  display 后端。
- 计时：主循环每轮 `lv_tick_inc(now - lastTick)`，并用
  `lv_delay_set_cb` 把 LVGL 内部的等待接到 `Display::delay()`，避免默认
  `lv_delay_ms` 忙等一个只在主循环里推进的 tick。
- 输入：LVGL 只注册 pointer indev（鼠标/触摸）；键盘由 UI 在事件循环里直接
  处理（Tab、方向键、Enter、`/`、`[`/`]`、F5、F11 等），搜索框由代码调用
  `lv_textarea_add_text/delete_char` 编辑，不注册 keypad indev。
- 指针事件不直接覆盖“当前状态”，而是进入一个小队列；indev 的 read 回调
  每帧把队列排空。这样按下与抬起即使落在同一帧内也会被 LVGL 依次看到，
  触摸屏上短促的 tap 不会丢失，也不会让 LVGL 继续使用上一帧的旧坐标。
- 详情面板是一个默认隐藏的抽屉：卡片/最近游玩/圆点被单击时打开，`Esc`、
  右上角 `×` 或切换页签时关闭；键盘选择只更新内容，不主动弹出。
- 字体：FreeType 按候选路径（`ALLNES_FONT` → 各平台系统 CJK 字体）直接创建
  11–52px 的位图模式字体（字号乘以 `renderScale`）；封面用 LVGL 的 PNG/JPEG
  解码器从 `A:/path` 加载，按路径缓存 `lv_image_dsc_t`。
- 封面使用 `LV_IMAGE_ALIGN_COVER`。图片控件必须作为与封面区域同尺寸容器的
  子控件：COVER 缩放后的位图可能大于控件，LVGL 会按 ext draw size 扩大裁剪
  区，只有父容器能把它限制在封面矩形内。
- 窗口隐藏/显示、错误提示、重新扫描和截图（`ALLNES_LAUNCHER_SHOT*`）都通过
  display 接口完成；截图在 `present()` 之前读取，避免空白帧。

## Ports

### IVideoSink

```cpp
virtual void present(const std::uint32_t* pixels, int width, int height) = 0;
```

`pixels` 是 `width * height` 的行主序 `0xAARRGGBB` 缓冲区，来自 PPU 的
`m_pictureBuffer`。每完成一帧调用一次。

### IAudioSink

```cpp
virtual void pushSamples(const float* samples, std::size_t count, int sampleRate) = 0;
```

APU 以“每两个 CPU 时钟周期一个样本”的节奏产生单声道 float 样本，原生采样率
约 894,454 Hz。`Emulator::runFrame()` 在帧末把累积的样本一次性交给 sink；
重采样、缓冲、设备打开都由 host 负责。

### IInputSource

```cpp
virtual std::uint8_t buttonState(int player) const = 0;  // player: 0 或 1
```

返回按键位掩码，位序与 NES 手柄移位寄存器一致：

| bit | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 按键 | A | B | Select | Start | Up | Down | Left | Right |

`Controller` 在 strobe 下降沿采样该状态，随后逐位读出。SDK 前端每帧调用
`SDL_GetKeyboardState` 和 `SDL_GameControllerGetButton`，把结果写入缓存。

## 一帧的数据流

```
host main loop
  └─ input.poll()                 // 键盘/手柄 → buttonState
  └─ Emulator::runFrame()
       ├─ 循环：3 × PPU::step() + CPU::step() + APU::step()
       │    （约 29,781 次，559 ns/次；PPU 在 PostRender 置 frameReady）
       ├─ APU::flushAudio()       // → IAudioSink::pushSamples
       └─ IVideoSink::present()   // PPU 帧缓冲 → 窗口纹理
  └─ 按 29,781 × 559 ns 做帧率控制
```

`PPU` 在 `PostRender` 阶段把整帧写入 `m_pictureBuffer` 并置位
`m_frameReady`；`Emulator::runFrame()` 只在此时返回，因此 host 拿到的永远是
完整帧。CPU 的 NMI 回调、mapper 的 IRQ 回调、DMA 回调都在 core 内部完成。

## 目录

```
core/include/port/   三个端口 + Null 实现（headless/静音用）
core/include/        对外头文件
core/src/            实现
core/src/APU/        APU 各声道与帧计数器
launcher/model/      ROM 扫描、iNES 头探测、launcher.conf 读写、配置目录
launcher/display/    Display 接口、SDL2 后端、headless 后端、工厂
launcher/ui/         LVGL 控件树、布局、状态机、键鼠处理、截图
launcher/types.h     LaunchRequest（host 与 UI 的边界类型）
launcher/lvgl/       LVGL 配置（lv_conf.h）
host/                SDL2 实现：sdl_video / sdl_audio / sdl_input / main
third_party/lvgl/    vendored LVGL 10（只读依赖）
third_party/stb_*    stb_image / stb_image_write（封面解码与 PNG 截图）
tools/headless.cpp   无 SDL 的驱动，可哈希、导出 PPM、校验测试图
```

## 实现要点

1. **平台依赖隔离**：内核不依赖任何窗口/音频库；PPU 直接输出
   `uint32_t` ARGB 缓冲区，输入通过 `IInputSource` 注入。
2. **音频端口化**：APU 只负责产生样本，经 `IAudioSink` 交给前端。SDL 前端
   用 `SDL_AudioStream` 做重采样。
3. **主循环外置**：`loadRom()/reset()/runFrame()` 由前端驱动，前端拥有时钟
   与事件循环。
4. **APU 修复**：三角波线性计数器控制位使用 `(value >> 7) & 1`，DMC 状态位
   使用 `has_more_samples() << 4`。
5. **PPU 渲染使能判断**：滚动地址恢复与 MMC3 扫描线计数在“背景或精灵任一
   开启”时都应运行，避免只开背景时画面错位。
6. **帧缓冲接口**：新增 `frameBuffer()/frameReady()/clearFrameReady()`，
   让视频输出与模拟逻辑解耦。
7. **新增 headless 工具与生成的测试 ROM**，让 CI 在没有 SDL、没有游戏 ROM
   的环境中也能验证画面与确定性。

## 扩展指引

### 增加一个 mapper

1. 在 `core/include/` 新建 `MapperXXX.h`，继承 `Mapper`。
2. 在 `core/src/` 实现 PRG/CHR bank 切换与（如需要）IRQ。
3. 在 `Mapper.h` 的 `Board` 枚举和 `Mapper.cpp` 的 `createMapper` 中注册。
4. 如果只是 MMC3 的变种（例如 mapper 245），可以继承 `MapperMMC3` 并覆盖
   `prgBankNumber()` / `linearChrRam()`，不必重写整套窗口逻辑。

### 增加一个前端

实现 `IVideoSink`、`IAudioSink`、`IInputSource`，然后：

```cpp
allnes::Emulator emu(video, audio, input);
emu.loadRom(path);
while (running)
    emu.runFrame();
```

不需要修改 core 的任何代码。

### 增加一个 display 后端（OpenGL / FFmpeg / …）

1. 在 `launcher/display/` 新建 `display_xxx.{h,cpp}`，实现 `Display` 的全部
   纯虚函数；像素输入始终是 ARGB8888。
2. 在 `display.h` 的 `Backend` 枚举中加值，在 `display.cpp` 的
   `createDisplay()`/`backendName()` 中注册。
3. 在 `host/main.cpp` 的 `--display` 解析表中加同名取值。
4. UI 不需要任何改动。可以直接复用 SDL 后端的事件翻译逻辑（OpenGL 后端仍用
   SDL 创建窗口和上下文时），或像 headless 后端一样恒不产生事件（FFmpeg
   录制）。

示例：

- **OpenGL**：用 SDL 创建 `SDL_WINDOW_OPENGL` 上下文，把 ARGB 画布上传为
  GL 纹理并绘制全屏 quad；`present()` 里 `SDL_GL_SwapWindow`。
- **FFmpeg**：无窗口后端，`updateRect` 维护整幅画布，`present()` 把当前帧
  送进 libavcodec/swscale；`pollEvent` 可接外部控制输入或恒 false。
