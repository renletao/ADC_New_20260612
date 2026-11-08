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
#define WF280_CMD_BT_MEAS 0xACu
/* 同族命令（见 Core/Inc/JH1400F.h）：0xAD 只测电桥，0xAE 只测温度，
   各返回 24 位原始数据（首字节是状态，后 3 字节大端）。 */
#define WF280_CMD_B_MEAS 0xADu
#define WF280_CMD_T_MEAS 0xAEu
#define WF280_RETRY_COUNT 3u
#define WF280_NORMAL_POLL_DELAY_MS 10u
#define WF280_CMD_POLL_DELAY_MS 1u
#define WF280_OUTPUT_3V3_SETTLE_MS 20u
#define WF280_OUTPUT_2V_SETTLE_MS 200u
#define WF280_M1_SETTLE_MS 20u
#define WF280_CURRENT_CHANNEL 0x01u
#define WF280_CURRENT_SHUNT_OHM 120u
#define WF280_ADC_FULL_SCALE 8388608u
#define WF280_SHORT_CURRENT_UA 20000u

/* 轨压回读：DUT 轨经 R8(1K)/R9(1k) 1:1 分压接到 PB1 / ADC1_IN9（VDD_FB）。
   ADC 配的是 12 位 + 16 倍过采样、右移 0，所以满量程是 4095 * 16 = 65520，
   不是 4095。VDDA 取 3.3 V，分压比 2:1。 */
#define WF280_VDDA_MV 3300
#define WF280_RAIL_DIV 2
#define WF280_ADC_OVERSAMPLED_FULL_SCALE 65520
#define WF280_RAIL_ADC_TIMEOUT_MS 10
#define WF280_RAIL_MIN_MV 1500
#define WF280_RAIL_TOLERANCE_PCT 10

/* AllTestFor280 的时序与固定参数。0xAC 的 2 字节测量参数在参考程序里由
   主机下发，整机测试里用固定值；如果产线需要指定测量模式，把它改成命令参数。
   整机测试不设定轨压，轨压由上位机 {1, 0x201} 设置。 */
#define WF280_TEST_MEASUREMENT 0x0000
#define WF280_TEST_MEASURE_MS 100
#define WF280_TEST_COEFF_COUNT 32   /* 每个 bank 全量读 32 路 */
#define WF280_TEST_COEFF_HEAD 2     /* 前 2 路额外写进兼容区 20..23 */
#define WF280_TEST_COEFF_DELAY_MS 5

uint16_t otp1[32];
uint16_t otp2[32];

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

/* 触发一次测量并读回：写命令 → 等测量时间 → 轮询空闲 → 读回 count 字节。
   0xAD/0xAE 与 0xAC 一样需要等待转换完成，不能只靠 3 次 10 ms 轮询。 */
static uint8_t wf280_read_meas(I2CInfo *bus, uint8_t command,
                              uint8_t *data, uint8_t count,
                              uint16_t settle_ms)
{
    uint8_t status = wf280_write(bus, command, NULL, 0u);
    if (status != WF280_STATUS_OK) return status;
    osDelay(settle_ms);
    status = wf280_wait_idle(bus, 0u, 2u, WF280_RETRY_COUNT);
    if (status != WF280_STATUS_OK) return status;
    return wf280_read(bus, data, count);
}

/* 芯片的 24 位数据是有符号补码，符号扩展成 32 位方便上位机按 int32 解读。 */
static uint32_t wf280_sign_extend24(uint32_t value)
{
    if (value & 0x00800000u) value |= 0xFF000000u;
    return value;
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
    MCP4725_WriteData_Voltage(0u);
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
        (supply_mv != 2000u && supply_mv != 3300u))
        return WF280_STATUS_I2C;
    memset(out, 0, out_lens * sizeof(*out));
    wf280_save_adc_config(saved_config);
    MCP4725_WriteData_Voltage(supply_mv);
    /* The reference firmware gives the 2.0 V/M1 path substantially longer
       rail settling time before its short-circuit/current sample. */
    osDelay(supply_mv == 2000u ? WF280_OUTPUT_2V_SETTLE_MS :
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
    supply_mv = (start & 0x80u) ? 2000u : 3300u;
    MCP4725_WriteData_Voltage(supply_mv);
    osDelay(100u);
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
    MCP4725_WriteData_Voltage(2000u);
    osDelay(WF280_M1_SETTLE_MS);
    standby = wf280_read_current();
    if (wf280_is_short(standby, 2000u)) {
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

/* WF280 整机综合测试，流程照 WF183：上电 → 回读轨压 → 休眠电流 →
   短路判定 → 0xBA 复位 + 0xAC 测量（测量进行中抓工作电流）→ 读两路系数
   → 恢复 ADC 配置 → 断电。结果帧偏移见 wf280.h 的 WF280_R_*。

   和 WF280TestOutput 的区别：那条命令是主机逐条控制、同步执行；这个是走
   IsTaskDone 异步队列的整机测试，主机轮询 0x400/0x401 取结果。 */
void AllTestFor280(uint16_t *out, uint16_t out_lens)
{
    I2CInfo bus;
    uint8_t saved_config[4];
    uint8_t params[2];
    uint8_t data[6] = {0};
    uint8_t coeff[3] = {0};
    uint8_t status = WF280_STATUS_OK;
    uint8_t i;
    uint16_t rail_mv;
    uint32_t sleep_code;
    uint32_t work_code = 0;

    if (!out || out_lens < WF280_TEST_RESULT_LENGTH) return;
    memset(out, 0, out_lens * sizeof(*out));
    wf280_save_adc_config(saved_config);

    /* 1) 轨压完全由上位机用 {1, 0x201} 设置：固件这里既不写 DAC，也不
       记忆任何值，只等它稳定。 */
    osDelay(WF280_OUTPUT_3V3_SETTLE_MS);

    /* 2) 回读实际轨压。PB1/ADC1_IN9 经 1:1 分压接在 VDD 上，读到的是
       真实输出。后面所有电流换算都以这个实测值为基准。 */
    rail_mv = WF280ReadRailMv();
    out[WF280_R_RAIL_MV] = rail_mv;
    if (rail_mv) out[WF280_R_RAIL_FLAG] |= WF280_RAIL_FLAG_MEASURED;
    if (rail_mv < WF280_RAIL_MIN_MV) {
        /* 轨没起来，后面测芯片没有意义，直接报轨异常。 */
        out[WF280_R_RAIL_FLAG] |= WF280_RAIL_FLAG_UNDER;
        out[WF280_R_STATUS] = WF280_STATUS_RAIL;
        wf280_restore_adc_config(saved_config);
        wf280_power_off();
        return;
    }

    /* 3) 休眠（待机）电流：上电后还没发过任何命令，连读两次取小值。 */
    sleep_code = wf280_read_min_current();
    out[WF280_R_SLEEP_UA] = wf280_current_ua(sleep_code, rail_mv);
    wf280_store_u32(&out[WF280_R_SLEEP_CODE], sleep_code);

    /* 4) 短路保护，阈值与 WF280TestOutput 相同。 */
    if (wf280_is_short(sleep_code, rail_mv)) {
        out[WF280_R_STATUS] = WF280_STATUS_SHORT;
        wf280_restore_adc_config(saved_config);
        wf280_power_off();
        return;
    }

    /* 5) 复位 + 0xAC 测量，并在测量进行中抓工作电流。 */
    wf280_open(&bus);
    status = wf280_set_cmd(&bus, WF280_CMD_RESET, NULL, 0, 0, 10);
    if (status == WF280_STATUS_OK) {
        osDelay(50);
        params[0] = (uint8_t)(WF280_TEST_MEASUREMENT >> 8);
        params[1] = (uint8_t)WF280_TEST_MEASUREMENT;
        status = wf280_write(&bus, WF280_CMD_BT_MEAS, params, 2);
    }
    if (status == WF280_STATUS_OK) {
        work_code = wf280_read_current();
        out[WF280_R_WORK_UA] = wf280_current_ua(work_code, rail_mv);
        wf280_store_u32(&out[WF280_R_WORK_CODE], work_code);
        osDelay(WF280_TEST_MEASURE_MS);
        status = wf280_wait_idle(&bus, 0, 2, WF280_RETRY_COUNT);
    }
    if (status == WF280_STATUS_OK) {
        status = wf280_read(&bus, data, 6);
        if (status == WF280_STATUS_OK)
            for (i = 0; i < 6; ++i) out[WF280_R_BT_DATA + i] = data[i];
    }

    /* 6) 电桥(压力) 与 温度 单独测量。0xAD/0xAE 各返回 4 字节：
       状态 + 24 位大端原始码。这里原样给出，物理量的换算留给上位机。 */
    if (status == WF280_STATUS_OK) {
        status = wf280_read_meas(&bus, WF280_CMD_B_MEAS, data, 4,
                                 WF280_TEST_MEASURE_MS);
        if (status == WF280_STATUS_OK)
            wf280_store_u32(&out[WF280_R_BRIDGE],
                            wf280_sign_extend24(((uint32_t)data[1] << 16) |
                                                ((uint32_t)data[2] << 8) |
                                                (uint32_t)data[3]));
    }
    if (status == WF280_STATUS_OK) {
        status = wf280_read_meas(&bus, WF280_CMD_T_MEAS, data, 4,
                                 WF280_TEST_MEASURE_MS);
        if (status == WF280_STATUS_OK)
            wf280_store_u32(&out[WF280_R_TEMP],
                            wf280_sign_extend24(((uint32_t)data[1] << 16) |
                                                ((uint32_t)data[2] << 8) |
                                                (uint32_t)data[3]));
    }

    /* 7) 两个 bank 各 32 路系数全量读回；前 2 路另写一份到兼容区 20..23，
       保证旧上位机不用改。 */
    for (i = 0; status == WF280_STATUS_OK && i < WF280_TEST_COEFF_COUNT; ++i) {
        status = wf280_get_chip_data(&bus, i, coeff, 3);
        if (status == WF280_STATUS_OK) {
            out[WF280_R_COEFF_ALL + i] =
                (uint16_t)(((uint16_t)coeff[1] << 8) | coeff[2]);
            if (i < WF280_TEST_COEFF_HEAD)
                out[WF280_R_COEFF + i] = out[WF280_R_COEFF_ALL + i];
        }
        osDelay(WF280_TEST_COEFF_DELAY_MS);
    }
    for (i = 0; status == WF280_STATUS_OK && i < WF280_TEST_COEFF_COUNT; ++i) {
        status = wf280_get_chip_data(&bus, (uint8_t)(0x80 + i), coeff, 3);
        if (status == WF280_STATUS_OK) {
            out[WF280_R_M1_COEFF_ALL + i] =
                (uint16_t)(((uint16_t)coeff[1] << 8) | coeff[2]);
            if (i < WF280_TEST_COEFF_HEAD)
                out[WF280_R_M1_COEFF + i] = out[WF280_R_M1_COEFF_ALL + i];
        }
        osDelay(WF280_TEST_COEFF_DELAY_MS);
    }
    wf280_close(&bus);
    out[WF280_R_STATUS] = status;

    /* 8) 恢复 ADC 配置并断电 */
    wf280_restore_adc_config(saved_config);
    wf280_power_off();
}
