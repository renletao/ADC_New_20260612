# ADC_New_20260612

基于 STM32G031G8Ux（Cortex-M0+）的 ADC 采集工程，外挂 TI **ADS1220** 24 位 ΔΣ ADC，
STM32CubeMX 生成 + Keil MDK 编译。

## 目录结构

| 路径 | 说明 |
| --- | --- |
| `ADC.ioc` / `.mxproject` | STM32CubeMX 工程文件，改外设配置从这里重新生成代码 |
| `Core/` | 用户代码：`main.c`、外设初始化、中断、FreeRTOS 任务等 |
| `Drivers/` | CMSIS + STM32G0xx HAL 驱动（CubeMX 生成，勿手改） |
| `Middlewares/` | FreeRTOS 等中间件 |
| `MDK-ARM/` | Keil 工程：`ADC.uvprojx`（工程文件）、`ADC.uvoptx`、启动文件 |
| `tests/` | Python 测试脚本 |
| `Schematic.png` | 原理图 |
| `ads1220.pdf` | ADS1220 数据手册 |

> `MDK-ARM/ADC/` 是编译输出目录，已被 `.gitignore` 排除，每次编译自动生成。

## 如何编译

1. 用 **Keil MDK 5** 打开 `MDK-ARM/ADC.uvprojx`
2. `Project → Rebuild all target files`（快捷键 F7）
3. 下载：接好 J-Link / ST-Link，按 F8

也可以用命令行：

```bash
UV4.exe -b MDK-ARM/ADC.uvprojx -j0 -o build.log
```

## 如何修改外设配置

用 STM32CubeMX 打开 `ADC.ioc`，改完点 `GENERATE CODE`。
注意生成的代码会覆盖 `Core/Src`、`Core/Inc` 里的初始化部分，
自己的业务逻辑请写在 `/* USER CODE BEGIN */` 与 `/* USER CODE END */` 之间，否则会被覆盖。

## 日常更新流程（重要）

代码改完后，把改动同步到 GitHub：

```bash
# 在项目根目录（就是本 README 所在目录）执行

git status              # 看看改了哪些文件
git add -A              # 把所有改动加入暂存
git commit -m "描述这次改了什么"   # 提交，写清楚改动内容
git push                # 推到 GitHub
```

只想提交部分文件时，用 `git add Core/Src/main.c` 指定具体文件。

### 首次克隆到新电脑

```bash
git clone https://github.com/zhuyanhua9910/ADC_New_20260612.git
```

克隆下来后用 Keil 打开 `MDK-ARM/ADC.uvprojx` 直接编译即可，
编译输出目录会自动生成，无需手动创建。

### 注意

- 不要提交 `MDK-ARM/ADC/` 里的内容（编译产物，体积大且无意义）
- `*.uvguix.*` 是 Keil 窗口布局的个人配置，也不要提交
- 拉取别人/自己另一台电脑的改动：`git pull`
