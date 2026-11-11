#include "wf280.h"
#include "ADS1220.h"
#include "MCP4725.h"
#include "SoftIIC.h"
#include "cmsis_os.h"
#include "adc.h"
#include <string.h>

/*
 * WF280 protocol adapter
 * ----------------------
 *
 * The transaction shape below intentionally follows WF280_OS_BURN/Soft/Src/
 * WF280_OS.c. A command write is one START followed by 0x70|W, the command
 * byte and each parameter byte, with an ACK clock after every byte. Reads are
 * a separate START with 0x70|R and a NACK on the last byte. The generic
 * product I2C protocol cannot express the command/status polling used by
 * WF280, so this adapter owns the complete transaction while retaining the
 * new-board SoftIIC electrical driver and its bus recovery.
 */

#define WF280_I2C_ADDRESS 0x70u
#define WF280_CMD_RESET 0xBAu
/* 0xAC 触发 BT（电桥+温度）测量：写 2 字节参数 CmdDat[15:8]/[7:0]，
   完成后按手册 4.3 一次读 6 字节：Status + 电桥20位 + 温度20位（打包，
   BridgeDat[3:0] 与 TempDat[19:16] 共用 1 个字节）。 */
#define WF280_CMD_BT_MEAS 0xAC

#define WF280_RETRY_COUNT 3
#define WF280_NORMAL_POLL_DELAY_MS 10
#define WF280_CMD_POLL_DELAY_MS 1u
#define WF280_OUTPUT_3V3_SETTLE_MS 20
#define WF280_OUTPUT_2V_SETTLE_MS 200
#define WF280_M1_SETTLE_MS 20
#define WF280_CURRENT_CHANNEL 0x01
#define WF280_CURRENT_SHUNT_OHM 120
#define WF280_ADC_FULL_SCALE 8388608
#define WF280_SHORT_CURRENT_UA 20000


/* 轨压回读：DUT 轨经 R8(1K)/R9(1k) 1:1 分压接到 PB1 / ADC1_IN9（VDD_FB）。
   ADC 配的是 12 位 + 16 倍过采样、右移 0，所以满量程是 4095 * 16 = 65520，
   不是 4095。VDDA 取 3.3 V，分压比 2:1。 */
#define WF280_VDDA_MV 3300
#define WF280_RAIL_DIV 2
#define WF280_ADC_OVERSAMPLED_FULL_SCALE 65520
#define WF280_RAIL_ADC_TIMEOUT_MS 10
#define WF280_RAIL_MIN_MV 1500
#define WF280_RAIL_TOLERANCE_PCT 10

/* */
#define WF280_TEST_MEASUREMENT 0x0000
#define WF280_TEST_MEASURE_MS 100
#define WF280_TEST_COEFF_COUNT 32   /* 每个 bank 全量读 32 路 */
#define WF280_TEST_COEFF_HEAD 2     /* 前 2 路额外写进兼容区 20..23 */
#define WF280_TEST_COEFF_DELAY_MS 5

#define WF280_SLEEP_ENTER_MS 200

uint16_t otp1[32];
uint16_t otp2[32];
static int32_t ADC_Value_280 = 0;

static uint8_t wf280_write(I2CInfo *bus, uint8_t command,
                           const uint8_t *params, uint8_t param_count)
{
    uint8_t frame[2u + 32u];
    uint8_t i;

    if (param_count > 32u) return WF280_STATUS_I2C;
    frame[0] = WF280_I2C_ADDRESS | IIC_WRITE;
    frame[1] = command;
    for (i = 0; i < param_count; ++i) frame[2u + i] = params[i];
    return I2C_WriteNByte(bus, frame, 2u + param_count) ?
           WF280_STATUS_I2C : WF280_STATUS_OK;
}

static uint8_t wf280_read(I2CInfo *bus, uint8_t *data, uint8_t count)
{
    if (!count) return WF280_STATUS_OK;
    return I2C_ReadNByte(bus, WF280_I2C_ADDRESS, data, count) ?
           WF280_STATUS_I2C : WF280_STATUS_OK;
}

static uint8_t wf280_wait_idle(I2CInfo *bus, uint8_t command_mode,
                               uint8_t delay_before_start, uint8_t retries)
{
    uint8_t status = 0;
    uint8_t i;

    if (delay_before_start) osDelay(delay_before_start);
    for (i = 0; i < retries; ++i) {
        if (wf280_read(bus, &status, 1u) != WF280_STATUS_OK)
            return WF280_STATUS_I2C;
        if (!command_mode) {
            if ((status & 0x80u) == 0u && (status & 0x01u) != 0u)
                return WF280_STATUS_OK;
        } else if ((status & 0x80u) == 0u &&
                   (status & 0x41u) == 0x41u) {
            return WF280_STATUS_OK;
        }
        osDelay(command_mode ? WF280_CMD_POLL_DELAY_MS :
                WF280_NORMAL_POLL_DELAY_MS);
    }
    return command_mode ? WF280_STATUS_CMD_TIMEOUT : WF280_STATUS_NOR_TIMEOUT;
}

static uint8_t wf280_set_cmd(I2CInfo *bus, uint8_t command,
                             const uint8_t *params, uint8_t param_count,
                             uint8_t command_mode, uint8_t delay_before_start)
{
    uint8_t status = wf280_write(bus, command, params, param_count);
    if (status != WF280_STATUS_OK) return status;
    return wf280_wait_idle(bus, command_mode, delay_before_start,
                           WF280_RETRY_COUNT);
}

static uint8_t wf280_get_chip_data(I2CInfo *bus, uint8_t command,
                                   uint8_t *data, uint8_t count)
{
    uint8_t status = wf280_write(bus, command, NULL, 0u);
    if (status != WF280_STATUS_OK) return status;
    status = wf280_wait_idle(bus, 0u, 0u, WF280_RETRY_COUNT);
    if (status != WF280_STATUS_OK) return status;
    return wf280_read(bus, data, count);
}

static void wf280_save_adc_config(uint8_t saved[4])
{
    memcpy(saved, ADS1220.config, 4u);
}

static void wf280_restore_adc_config(const uint8_t saved[4])
{
    memcpy(ADS1220.config, saved, 4u);
    /* reg1/reg2（模式、基准、IDAC）也要一起写回：只写 reg0 的话，
       测量过程中被改掉的基准/激励配置会一直漂着。 */
    ADS1220_WriteRegister(0u, 1u, &ADS1220.config[0]);
}

static uint32_t wf280_read_current(void)
{
    ADS1220.config[0] = WF280_CURRENT_CHANNEL;
    ADS1220_WriteRegister(0u, 1u, &ADS1220.config[0]);
    osDelay(10u);
    return GET_ADS1220_Val(&ADS1220);
}

/* ADS1220 是 24 位有符号 ADC，ADS1220_ReadDataFrame() 已经把符号扩展到了
   32 位。这颗板子的电流通道读出来是负的（分流方向相反），负号只代表电流
   方向，是一条有效读数，不是“读失败”。所以统一按绝对值处理，
   不能像旧代码那样看到 bit31 就把样本丢掉 —— 那样工作/休眠电流永远是
   0 uA，wf280_is_short() 的短路判定也就永远不成立。 */
static uint32_t wf280_current_magnitude(uint32_t raw)
{
    int32_t value = (int32_t)raw;

    return (uint32_t)((value < 0) ? -value : value);
}

/* 电流换算：采样电阻 120 Ω，ADS1220 用 PGA 旁路、AVDD 做基准，所以满量程
   对应 rail_mv / 120 Ω。按 μA 返回，超过 65535 饱和（对应 65 mA，远高于
   20 mA 的短路阈值，不影响判定）。 */
static uint16_t wf280_current_ua(uint32_t raw, uint16_t rail_mv)
{
    uint64_t current_ua;

    if (!rail_mv) return 0;
    current_ua = (uint64_t)wf280_current_magnitude(raw) * rail_mv * 1000;
    current_ua /= (uint64_t)WF280_ADC_FULL_SCALE * WF280_CURRENT_SHUNT_OHM;
    return (current_ua > 0xFFFF) ? 0xFFFF : (uint16_t)current_ua;
}

static uint8_t wf280_is_short(uint32_t raw, uint16_t rail_mv)
{
    return wf280_current_ua(raw, rail_mv) >= WF280_SHORT_CURRENT_UA;
}


/* 照 WF183 的做法连读两次取较小值：刚上电或刚退出复位时电流还没落稳，
   取小值可以避开过渡态。比较时用绝对值，负号只代表电流方向，
   不能当成“无效样本”换掉另一个。 */
static uint32_t wf280_read_min_current(void)
{
    uint32_t first = wf280_read_current();
    uint32_t second = wf280_read_current();

    return (wf280_current_magnitude(second) < wf280_current_magnitude(first)) ?
           second : first;
}

/* 一个字节一个寄存器，和 WF183 的电流字段写法一致。 */
static void wf280_store_u32(uint16_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
    dst[2] = (uint8_t)(value >> 16);
    dst[3] = (uint8_t)(value >> 24);
}

static void wf280_power_off(void)
{
    MCP4725_WriteData_Voltage(0);
}

static uint8_t wf280_rail_adc_ready;

/* 读一次 DUT 轨压。ADC1 被 MX_ADC1_Init() 配成连续转换，这里 Start/读/Stop，
   用完就关，不长期占用 ADC。连续模式下第一帧可能是启动前的旧值，先丢一帧。 */
uint16_t WF280ReadRailMv(void)
{
    uint32_t code = 0;
    uint32_t mv;

    if (!wf280_rail_adc_ready) {
        /* G0 的 ADC 不校准有几个 mV 量级的失调，第一次使用前校一次。 */
        (void)HAL_ADCEx_Calibration_Start(&hadc1);
        wf280_rail_adc_ready = 1;
    }
    if (HAL_ADC_Start(&hadc1) != HAL_OK) return 0;
    (void)HAL_ADC_PollForConversion(&hadc1, WF280_RAIL_ADC_TIMEOUT_MS);
    (void)HAL_ADC_GetValue(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, WF280_RAIL_ADC_TIMEOUT_MS) == HAL_OK)
        code = HAL_ADC_GetValue(&hadc1);
    (void)HAL_ADC_Stop(&hadc1);

    /* VDD = code / 65520 * VDDA * 2 */
    mv = code * (uint32_t)WF280_VDDA_MV * WF280_RAIL_DIV /
         WF280_ADC_OVERSAMPLED_FULL_SCALE;
    return (uint16_t)((mv > 0xFFFF) ? 0xFFFF : mv);
}

static void wf280_open(I2CInfo *bus)
{
    IIC_Init(bus, I2C_SDA_GPIO_Port, I2C_SDA_Pin,
             I2C_SCL_GPIO_Port, I2C_SCL_Pin);
}

static void wf280_close(I2CInfo *bus)
{
    IIC_DeInit(bus);
}

/* Keep the legacy OTP command (cmd[0] == 11) available. It now uses the
   same direct command/read transaction as the new WF280 test commands and
   propagates NACK/timeout instead of comparing stale bytes. */
void WF280CheckOtpReg(uint16_t *out, uint16_t out_lens)
{
    I2CInfo bus;
    uint8_t i;
    uint8_t data[3] = {0};
    uint8_t status;

    if (!out || out_lens < WF280_RESULT_LENGTH) return;
    memset(out, 0, out_lens * sizeof(*out));
    memset(otp1, 0, sizeof(otp1));
    memset(otp2, 0, sizeof(otp2));
    wf280_open(&bus);
    for (i = 0; i < 32u; ++i) {
        status = wf280_get_chip_data(&bus, i, data, 3u);
        if (status != WF280_STATUS_OK) {
            out[0] = 0xFFu; out[1] = i + 1u; wf280_close(&bus); return;
        }
        otp1[i] = (uint16_t)(((uint16_t)data[1] << 8) | data[2]);
        osDelay(2u);
    }
    for (i = 0; i < 32u; ++i) {
        status = wf280_get_chip_data(&bus, (uint8_t)(0x80u + i), data, 3u);
        if (status != WF280_STATUS_OK) {
            out[0] = 0xFFu; out[1] = i + 1u; wf280_close(&bus); return;
        }
        otp2[i] = (uint16_t)(((uint16_t)data[1] << 8) | data[2]);
        osDelay(2u);
        if (otp2[i] != otp1[i]) {
            out[0] = 0xFFu; out[1] = i + 1u; wf280_close(&bus); return;
        }
    }
    wf280_close(&bus);
    out[0] = 1u;
    out[1] = 0u;
}

/* Run the 0xAC bridge/temperature measurement in either 3.3 V or 2.0 V
   mode. Results are {status, six raw bytes}. A normal run keeps the rail
   powered so the host can read it back with {1, 0x211}; the short-circuit
   path still removes the supply immediately. */
uint8_t WF280TestOutput(uint16_t supply_mv, uint16_t measurement,
                        uint16_t settle_ms, uint16_t *out,
                        uint16_t out_lens)
{
    I2CInfo bus;
    uint8_t saved_config[4];
    uint8_t params[2];
    uint8_t data[6] = {0};
    uint8_t status = WF280_STATUS_OK;
    uint32_t current;

    if (!out || out_lens < WF280_OUTPUT_RESULT_LENGTH ||
        (supply_mv != WF280_SUPPLY_1V8_MV && supply_mv != WF280_SUPPLY_3V3_MV))
        return WF280_STATUS_I2C;
    memset(out, 0, out_lens * sizeof(*out));
    wf280_save_adc_config(saved_config);
    MCP4725_WriteData_Voltage(supply_mv);
    /* The reference firmware gives the 2.0 V/M1 path substantially longer
       rail settling time before its short-circuit/current sample. */
    osDelay(supply_mv == WF280_SUPPLY_1V8_MV ? WF280_OUTPUT_2V_SETTLE_MS :
            WF280_OUTPUT_3V3_SETTLE_MS);
    current = wf280_read_current();
    if (wf280_is_short(current, supply_mv)) {
        out[0] = WF280_STATUS_SHORT;
        out[1] = (uint8_t)current;
        out[2] = (uint8_t)(current >> 8);
        out[3] = (uint8_t)(current >> 16);
        out[4] = (uint8_t)(current >> 24);
        wf280_restore_adc_config(saved_config);
        wf280_power_off();
        return WF280_STATUS_SHORT;
    }

    wf280_open(&bus);
    status = wf280_set_cmd(&bus, WF280_CMD_RESET, NULL, 0u, 0u, 10u);
    if (status == WF280_STATUS_OK) {
        osDelay(50u);
        params[0] = (uint8_t)(measurement >> 8);
        params[1] = (uint8_t)measurement;
        status = wf280_write(&bus, WF280_CMD_BT_MEAS, params, 2u);
    }
    if (status == WF280_STATUS_OK) {
        osDelay(settle_ms);
        status = wf280_wait_idle(&bus, 0u, 2u, WF280_RETRY_COUNT);
    }
    if (status == WF280_STATUS_OK) status = wf280_read(&bus, data, 6u);
    wf280_close(&bus);
    out[0] = status;
    if (status == WF280_STATUS_OK) {
        uint8_t i;
        for (i = 0; i < 6u; ++i) out[i + 1u] = data[i];
    }
    wf280_restore_adc_config(saved_config);
    /* 正常结束保持供电，方便紧接着用 {1, 0x211} 读回实测轨压；
       短路路径仍然立即断电。 */
    return status;
}

/* Read the normal (0x00..0x1F) or M1 (0x80..0x9F) coefficient bank. */
uint8_t WF280ReadCoefficients(uint8_t start, uint8_t count,
                              uint16_t *out, uint16_t out_lens)
{
    I2CInfo bus;
    uint8_t saved_config[4];
    uint8_t data[3] = {0};
    uint8_t i;
    uint8_t status = WF280_STATUS_OK;
    uint16_t supply_mv;

    if (!out || !count || count > WF280_COEFF_MAX ||
        out_lens < (uint16_t)(count + 1u)) return WF280_STATUS_I2C;
    memset(out, 0, out_lens * sizeof(*out));
    wf280_save_adc_config(saved_config);
    supply_mv = (start & 0x80) ? WF280_SUPPLY_1V8_MV : WF280_SUPPLY_3V3_MV;
    MCP4725_WriteData_Voltage(supply_mv);
    osDelay(100);
		//
    if (wf280_is_short(wf280_read_current(), supply_mv)) {
        out[0] = WF280_STATUS_SHORT;
        wf280_restore_adc_config(saved_config);
        wf280_power_off();
        return WF280_STATUS_SHORT;
    }
    wf280_open(&bus);
    /* The reference program resets the device before every coefficient
       sequence.  Keep the command/NOR state deterministic when a host
       requests coefficients after an earlier measurement. */
    status = wf280_set_cmd(&bus, WF280_CMD_RESET, NULL, 0u, 0u, 10u);
    if (status == WF280_STATUS_OK) osDelay(10u);
    for (i = 0; i < count; ++i) {
        if (status != WF280_STATUS_OK) break;
        status = wf280_get_chip_data(&bus, (uint8_t)(start + i), data, 3u);
        if (status != WF280_STATUS_OK) break;
        out[i + 1u] = (uint16_t)(((uint16_t)data[1] << 8) | data[2]);
        osDelay(5u);
    }
    wf280_close(&bus);
    out[0] = status;
    wf280_restore_adc_config(saved_config);
    wf280_power_off();
    return status;
}

/* M1 mode returns standby/work current followed by the 0x80 coefficient
   bank. Current occupies four byte-valued Modbus registers, matching the
   reference program's ADC[0]/ADC[3] fields. */
uint8_t WF280ReadM1(uint8_t count, uint16_t *out, uint16_t out_lens)
{
    I2CInfo bus;
    uint8_t saved_config[4];
    uint8_t data[3] = {0};
    uint8_t i;
    uint8_t status = WF280_STATUS_OK;
    uint32_t standby = 0u;
    uint32_t working = 0u;

    if (!out || !count || count > WF280_COEFF_MAX ||
        out_lens < (uint16_t)(5u + count)) return WF280_STATUS_I2C;
    memset(out, 0, out_lens * sizeof(*out));
    wf280_save_adc_config(saved_config);
    /* The 0x80..0x9F bank is the M1/2.0 V calibration bank in the
       reference firmware.  The new board selects that rail with the DAC. */
    MCP4725_WriteData_Voltage(WF280_SUPPLY_1V8_MV);
    osDelay(WF280_M1_SETTLE_MS);
    standby = wf280_read_current();
    if (wf280_is_short(standby, WF280_SUPPLY_1V8_MV)) {
        out[0] = WF280_STATUS_SHORT;
        out[1] = (uint8_t)standby;
        out[2] = (uint8_t)(standby >> 8);
        wf280_restore_adc_config(saved_config);
        wf280_power_off();
        return WF280_STATUS_SHORT;
    }
    wf280_open(&bus);
    status = wf280_set_cmd(&bus, WF280_CMD_RESET, NULL, 0u, 0u, 10u);
    if (status == WF280_STATUS_OK) {
        osDelay(50u);
        working = wf280_read_current();
        /* Preserve the reference host layout: each result register carries
           one byte (ADC[0] low/high, then ADC[3] low/high). */
        out[1] = (uint8_t)standby;
        out[2] = (uint8_t)(standby >> 8);
        out[3] = (uint8_t)working;
        out[4] = (uint8_t)(working >> 8);
    }
    for (i = 0; status == WF280_STATUS_OK && i < count; ++i) {
        status = wf280_get_chip_data(&bus, (uint8_t)(0x80u + i), data, 3u);
        if (status == WF280_STATUS_OK)
            out[5u + i] = (uint16_t)(((uint16_t)data[1] << 8) | data[2]);
    }
    wf280_close(&bus);
    out[0] = status;
    wf280_restore_adc_config(saved_config);
    wf280_power_off();
    return status;
}

/* WF280 全套测试（主机写 0x14 触发，完成后从 30000 起读 76 个输入寄存器）。
 *
 * 流程：
 *   1. 1.8V 下读 M1 bank（0x80~0x9F）32 路系数；
 *   2. 切 3.3V 读 NOR bank（0x00~0x1F），逐路和 M1 比较；
 *   3. 任何一路不同就写 0xE3 立即返回，不再测电流；
 *   4. 全部一致才测轨压、工作电流、0xAC 原始数据、休眠电流；
 *   5. 成功后把 M1/NOR 原始值追加到帧尾。
 *
 * 结果帧：
 *   [0]      状态：0=成功，1=I2C 失败，2=NOR 超时，0xE3=系数不一致
 *   [1]      轨压 mV
 *   [2..3]   工作电流原始码（低 16 位在前）
 *   [4..5]   休眠电流原始码（低 16 位在前）
 *   [6..11]  0xAC 的 6 字节（Status + 电桥20位 + 温度20位）
 *   [12..43] M1 bank 32 路原始值
 *   [44..75] NOR bank 32 路原始值
 *   失败时只有 [0] 有错误码，[1..75] 全 0。
 */
void AllTestFor280(uint16_t *out, uint16_t out_lens)
{
    /* 测试期间的临时数据：
       - m1_coeff / nor_coeff：两个 bank 各 32 路原始值，成功后回传；
       - coeff_data：单路读回的 3 字节，[0] 状态/回显，[1..2] 16 位系数（大端）；
       - data：0xAC 读回的 6 字节。 */
    I2CInfo bus;
    uint8_t saved_config[4];
    uint8_t status = WF280_STATUS_OK;    // 运行时操作的状态
    uint16_t m1_coeff[WF280_COEFF_MAX];  // 32 路 M1 系数 
    uint16_t nor_coeff[WF280_COEFF_MAX]; // 32 路 NOR 原始值
    uint8_t coeff_data[3];               // 每路读回的 3 字节
    uint8_t k;                           // 系数路数循环变量
    uint8_t params[2];                   // 0xAC 测量参数
    uint8_t data[WF280_R_RAW_LEN] = {0}; // 0xAC 读回的 6 字节
    uint16_t rail_mv;                    // 实测轨压
    uint32_t sleep_code;                 // 休眠电流原始码
    uint32_t work_code = 0;              // 工作电流原始码
    uint16_t nor;                        // NOR 系数
    uint8_t nor_mismatch = 0;            // 不一致的路号，0=全部一致
    uint8_t i;                           // 原始数据循环变量
    

    // 先清除缓存区，防止数据残留
    memset(out, 0, out_lens * sizeof(*out));
    // 先保存ADS1220配置，测试结束后恢复
    wf280_save_adc_config(saved_config);


    // 1、开 1.8V。
    //    M1 bank（0x80~0x9F）只能在 M1/1.8V 模式下读；本板没有 M1_EN 引脚，
    //    用 DAC 输出 1.8V 代替。等 300ms 让 DUT 电源稳定。
    MCP4725_WriteData_Voltage(WF280_SUPPLY_1V8_MV);
    // 等待电压稳定
    osDelay(300);                         

    // 2、读 M1 系数（0x80~0x9F，32 路）。
    //    先 0xBA 复位（等 10ms）；复位后再给 50ms 启动时间，然后逐路读。
    //    每路 3 字节：[0] 状态/回显，[1..2] 16 位系数（大端），间隔 1ms。
    wf280_open(&bus); // 开启i2c
    status = wf280_set_cmd(&bus, WF280_CMD_RESET, NULL, 0u, 0u, 10u);
    if (status == WF280_STATUS_OK) {
        osDelay(50u);
        for (k = 0u; k < WF280_COEFF_MAX; ++k) {
            status = wf280_get_chip_data(&bus, (uint8_t)(0x80u + k),
                                         coeff_data, 3u);
            if (status != WF280_STATUS_OK) break;
            m1_coeff[k] = ((uint16_t)coeff_data[1] << 8) | coeff_data[2];
            osDelay(1u);
        }
    }
    wf280_close(&bus);

    if (status != WF280_STATUS_OK) {       /* M1 读失败：恢复配置并断电后退出 */
        out[WF280_R_STATUS] = status;
        wf280_restore_adc_config(saved_config);
        wf280_power_off();
        return;
    }


    // 3、切 3.3V。
    //    NOR bank（0x00~0x1F）在正常 3.3V 模式下访问，等 100ms 稳定。
    MCP4725_WriteData_Voltage(WF280_SUPPLY_3V3_MV);
    // 等待电压稳定
    osDelay(100);

    // 4、读 NOR（0x00~0x1F），读一路比一路。
    //    复位后参考工程只等 10ms，这里给 50ms 余量，只影响速度不影响结果。
    //    每路 3 字节，取 [1..2] 当 16 位系数；先存 nor_coeff，再和 m1_coeff 比。
    //    某一路不同就记下路号并退出循环，每路间隔 5ms。
    wf280_open(&bus);
    status = wf280_set_cmd(&bus, WF280_CMD_RESET, NULL, 0u, 0u, 10u);
    if (status == WF280_STATUS_OK) {
        osDelay(50u);
        for (k = 0u; k < WF280_COEFF_MAX; ++k) {
            status = wf280_get_chip_data(&bus, k, coeff_data, 3u);
            if (status != WF280_STATUS_OK) break;
            nor = ((uint16_t)coeff_data[1] << 8) | coeff_data[2];
            nor_coeff[k] = nor;                     // 保存 NOR 原始值
            if (nor != m1_coeff[k]) {               // 有一位不同
                nor_mismatch = (uint8_t)(k + 1u);   // 记录第几路，1~32
                break;
            }
            osDelay(5u);
        }
    }
    wf280_close(&bus);

    // 5、失败立即返回：首字写错误码，[1..75] 保持 0。
    //    status != 0：NOR 读失败（1=I2C，2=NOR 超时）；
    //    nor_mismatch != 0：M1 与 NOR 系数不一致，写 0xE3。
    //    两条路径都恢复 ADS1220 配置并断电。
    if (status != WF280_STATUS_OK) {                // NOR 读失败
        out[WF280_R_STATUS] = status;
        wf280_restore_adc_config(saved_config);
        wf280_power_off();
        return;
    }
    if (nor_mismatch) {                             // 系数不一致
        out[WF280_R_STATUS] = 0xE3;                 // 0xE3：M1 与 NOR 不一致
        wf280_restore_adc_config(saved_config);
        wf280_power_off();
        return;
    }

    // 6、测轨压、工作电流、0xAC 原始数据和休眠电流。
    //    全部成功时首字才写 0，后面按帧格式填真实数据。
    // 6.1 回读实际轨压。
    //     3.3V 已经稳定，这里读到的值只作为结果数据。
    osDelay(WF280_OUTPUT_3V3_SETTLE_MS);
    rail_mv = WF280ReadRailMv();                    // 结果帧 [1]
    out[WF280_R_RAIL_MV] = rail_mv;

    // 6.2 复位后发 0xAC 测量命令。
    //     工作电流必须在测量进行中抓，等测量结束芯片就进低功耗了。
    //     参数按大端拆成两个字节；测量约 100ms，然后轮询 NOR 空闲。
    wf280_open(&bus);
    status = wf280_set_cmd(&bus, WF280_CMD_RESET, NULL, 0u, 0u, 10u);
    if (status == WF280_STATUS_OK) {
        osDelay(50);
        params[0] = (uint8_t)(WF280_TEST_MEASUREMENT >> 8);
        params[1] = (uint8_t)WF280_TEST_MEASUREMENT;
        status = wf280_write(&bus, WF280_CMD_BT_MEAS, params, 2);
    }
    if (status == WF280_STATUS_OK) {
        work_code = wf280_read_current();
        out[WF280_R_WORK_RAW]     = (uint16_t)(work_code & 0xFFFFu);        // 结果帧 [2..3] 工作电流原始码
        out[WF280_R_WORK_RAW + 1] = (uint16_t)(work_code >> 16);

        osDelay(WF280_TEST_MEASURE_MS);
        status = wf280_wait_idle(&bus, 0, 2, WF280_RETRY_COUNT);
    }

    // 6.3 测量完成后一次读 6 字节原始数据：
    //     [0]=Status，[1..5]=电桥 20 位 + 温度 20 位（按手册 4.3 打包）。
    if (status == WF280_STATUS_OK)
        status = wf280_read(&bus, data, WF280_R_RAW_LEN);
    wf280_close(&bus);

    // 6.4 任一环节失败：整个结果帧清 0，只写首字错误码，
    //     恢复配置并断电后返回（不再继续等休眠）。
    if (status != WF280_STATUS_OK) {
        memset(out, 0, out_lens * sizeof(*out));
        out[WF280_R_STATUS] = status;
        wf280_restore_adc_config(saved_config);
        wf280_power_off();
        return;
    }

    for (i = 0; i < WF280_R_RAW_LEN; ++i)
        out[WF280_R_RAW + i] = data[i];
    out[WF280_R_STATUS] = 0;                    // 0=成功

    // 6.5 等 1s 让 DUT 进入休眠，再读休眠电流原始码。
    //     读电流的函数内部连读两次取较小值，避开刚进休眠的过渡态。
    osDelay(1000);
    sleep_code = wf280_read_min_current();
    out[WF280_R_SLEEP_RAW]     = (uint16_t)(sleep_code & 0xFFFF);              // 结果帧 [4..5] 休眠电流原始码
    out[WF280_R_SLEEP_RAW + 1] = (uint16_t)(sleep_code >> 16);

    // 6.6 把 M1/NOR 两个 bank 的 32 路原始值追加到帧尾，
    //     上位机可以直接核对每一路，不用再单独发 0x207/0x209。
    for (i = 0; i < WF280_COEFF_MAX; ++i) {
        out[WF280_R_M1 + i]  = m1_coeff[i];    // [12..43]
        out[WF280_R_NOR + i] = nor_coeff[i];   // [44..75]
    }

    // 恢复 ADS1220 配置（电流测量改过通道/增益）。
    // 测试结束关掉 DUT 电压。
    wf280_restore_adc_config(saved_config);
    wf280_power_off();
}
