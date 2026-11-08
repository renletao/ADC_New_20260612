# WF280 全功能测试工具

原「多功能小板测试」是编译好的 exe，没有源码、加不进新命令，所以单独做了这个工具，
专门覆盖 **WF280 的全部功能**，与原工具并存，互不干扰。

## 覆盖的命令

| 功能 | 命令 | 方式 | 返回 |
| --- | --- | --- | --- |
| 整机测试 | `cmd[0] = 14` | 异步，走 `IsTaskDone` 队列 | 32 个寄存器：状态、轨压、电流、系数 |
| 轨压回读 | `{1, 0x211}` | 同步，随时可读 | `out[0]` 状态、`out[1]` mV |
| OTP 校验 | `{11}` | 同步 | `out[0]` 1=通过 / 0xFF=失败，`out[1]` 失败路号 |
| 输出测试 3.3 V | `{1, 0x208, 测量参数, 稳定时间}` | 同步 | 状态 + 6 个原始字节 |
| 输出测试 2.0 V | `{1, 0x210, 测量参数, 稳定时间}` | 同步 | 状态 + 6 个原始字节 |
| 读正常 bank 系数 | `{1, 0x209, 路数}` | 同步 | 状态 + N 路 16 位系数 |
| 读 M1 bank 系数 | `{1, 0x207, 路数}` | 同步 | 状态 + 待机/工作电流码 + N 路系数 |
| 设定 DAC 轨压（辅助） | `{1, 0x201, lo, hi}` | 同步 | 状态 |

命令码、结果偏移、判定阈值全部对应固件 `Core/Src/wf280.c`、`Core/Inc/wf280.h`。

## 环境

- Python 3.8+（Windows 用官方 Python 即可，自带 tkinter）
- `pip install pyserial`
- 不需要装 pymodbus，Modbus RTU 是自己实现的（`mb_rtu.py`）

板子侧：USART2，**115200 8N1**，Modbus RTU 从机地址 **1**，与固件一致。

## 图形界面

```bash
python wf280_tool.py
```

- 选串口 → 连接，会自动读一次轨压做通讯自检
- ① 整机测试：点一下就等结果，表格逐项列出，PASS / NG 直接显示
- ② 轨压回读：可以勾「连续监测」，0.5 s 刷一次
- ③ 单项命令：OTP 校验、输出测试 3.3 V / 2.0 V、读系数、读 M1，参数在顶部填

## 命令行

```bash
python wf280_tool.py ports                         # 列出串口
python wf280_tool.py --port COM5 full              # 整机测试
python wf280_tool.py --port COM5 rail              # 读轨压
python wf280_tool.py --port COM5 otp               # OTP 校验
python wf280_tool.py --port COM5 out33 --meas 0x0003 --settle 120
python wf280_tool.py --port COM5 out20
python wf280_tool.py --port COM5 coeff --count 8
python wf280_tool.py --port COM5 m1 --count 8
python wf280_tool.py --port COM5 vdd --mv 3300
```

退出码：`0` 通过，`1` 测试 NG，`2` 通讯/参数错误 —— 方便产线脚本判断。

## 离线自检

没有板子也能验证工具本身对不对：

```bash
python selftest.py
```

它用一个假从机模拟固件，逐条检查 13 项：CRC、寄存器地址、每条命令的帧格式、
异步轮询流程、超时保护、结果解码。改动代码后先跑这个。

## 两个容易踩的坑

1. **寄存器地址不是 0 基偏移**
   固件里 `eMBRegHoldingCB` / `eMBRegInputCB` 直接比较 `40001` / `30001`，
   所以 PDU 里必须写 40001 和 30001 本身。按标准 Modbus 习惯写 0 会返回异常码 2。

2. **轨压读到的是实测值，不是设定值**
   `0x211` 走的是 PB1/ADC1_IN9 分压回采，读到的是 DUT 真实轨压。
   所以「设了 3300 但读到 1200」说明轨没起来，不是命令没生效。

## 文件说明

| 文件 | 作用 |
| --- | --- |
| `wf280_tool.py` | 入口：图形界面 / 命令行 |
| `gui.py` | tkinter 界面 |
| `wf280_proto.py` | 命令封装与结果解码（无 UI 依赖，可被脚本复用） |
| `mb_rtu.py` | 极简 Modbus RTU 主站 |
| `selftest.py` | 离线自检 |
