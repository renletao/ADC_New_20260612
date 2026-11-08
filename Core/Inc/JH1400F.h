//JHM1400D and JHM1400F related definitions
//
//Only applicable to JHM1400D and JHM1400F, not applicable to JHM1400C

#ifndef __JHM1400D_H
#define __JHM1400D_H

	//============================================================
	//JHM1400D命令表定义_开始，客户请勿修改
	#define CMD_RESET 0xba        //复位命令
	#define CMD_START_CMD  0xa9   //进入命令模式命令
	#define CMD_START_NOR 0xa8    //进入NOR模式命令
	#define CMD_START_CYC 0xaa    //进入CYC模式命令

	#define CMD_BT_MEAS 0xac      //命令，用于触发电桥(BM)和温度(TM)测量，返回 20bit 原始数据或校准后数据
	#define CMD_B_MEAS 0xad       //命令，用于触发电桥测量(BM)，返回 24bit 原始数据数据
	#define CMD_T_MEAS 0xae       //命令，用于触发温度测量(TM)，返回 24bit 原始数据数据

	#define CMD_SET_REG_AFE_CFG1 0xeb //配置“模拟前端配置寄存器1”的命令
	#define CMD_SET_REG_AFE_CFG2 0xec //配置“模拟前端配置寄存器2”的命令
	#define CMD_SET_REG_AFE_CFG3 0xed //配置“模拟前端配置寄存器3”的命令(仅在CMD模式，内部测试用)
	#define CMD_SET_REG_SYS_CFG 0xee //配置“系统配置寄存器，SYS_CFG_REG”的命令
	#define CMD_AMP_AUTOZERO 0xf0 //放大器稳零命令
	#define CMD_READ_PRODUCT_ID 0xfe //读取芯片版本号命令，0x0D为大版本号，0x01为小版本号

	#define CMD_FIFO_RESET 0xbb //对FIFO进行复位的命令
	#define CMD_SET_REG_P_TH_H 0xe6 //修改“压力比较高阈值寄存器”的命令
	#define CMD_SET_REG_P_TH_L 0xe7 //修改“压力比较低阈值寄存器”的命令
	//JHM1400D命令表定义_结束
	//============================================================

	//============================================================
	//系统配置寄存器，参数配置值的对应关系定义，客户请勿修改
	#define INT_L (0x0<<15) //中断信号低电平有效
	#define INT_H (0x1<<15) //中断信号高电平有效
	#define INT_FIFO_DIS (0x0<<14) //FIFO_FULL中断关闭
	#define INT_FIFO_EN (0x1<<14) //FIFO_FULL中断开启
	#define INT_EOM_DIS (0x0<<13) //测量完成中断EOM关闭
	#define INT_EOM_EN (0x1<<13) //测量完成中断EOM开启
	#define FIFO_DIS (0x0<<12) //FIFO禁止
	#define FIFO_EN (0x1<<12) //FIFO使能
	#define CAL_DIS (0x0<<11) //输出原始值
	#define CAL_EN (0x1<<11) //输出校准后的值
	#define POWERON_AUTO (0x0<<10) //测量完成自动掉电
	#define POWERON_ALWAYS (0x1<<10) //测量完成不掉电
	#define INT_CMP_DIS (0x0<<9) //阈值比较中断CMP关闭
	#define INT_CMP_EN (0x1<<9) //阈值比较中断CMP开启
	#define TM_RATE1 (0x0<<6) //每次测量周期都测温度
	#define TM_RATE2 (0x1<<6) //每2次测量周期测量1次温度
	#define TM_RATE3 (0x2<<6) //每4次测量周期测量1次温度
	#define TM_RATE4 (0x3<<6) //每8次测量周期测量1次温度
	#define TM_RATE5 (0x4<<6) //每16次测量周期测量1次温度
	#define TM_RATE6 (0x5<<6) //每32次测量周期测量1次温度
	#define TM_RATE7 (0x6<<6) //每64次测量周期测量1次温度
	#define TM_RATE8 (0x7<<6) //每128次测量周期测量1次温度
	#define MEAS_CYC_8ms (0x0<<3) //CYC周期8ms(125Hz)
	#define MEAS_CYC_16ms (0x1<<3) //CYC周期16ms(62.5Hz)
	#define MEAS_CYC_32ms (0x2<<3) //CYC周期32ms(31.25Hz)
	#define MEAS_CYC_64ms (0x3<<3) //CYC周期64ms(15.6Hz)
	#define MEAS_CYC_128ms (0x4<<3) //CYC周期128ms(7.8Hz)
	#define MEAS_CYC_256ms (0x5<<3) //CYC周期256ms(3.9Hz)
	#define MEAS_CYC_512ms (0x6<<3) //CYC周期512ms(1.95Hz)
	#define MEAS_CYC_1024ms (0x7<<3) //CYC周期1024ms(1Hz)
	#define IIR_CO1 (0x0<<0) //IIR_CO为1，IIR滤波器关闭
	#define IIR_CO2 (0x1<<0) //IIR_CO为2，IIR滤波器开启，带宽0.230 × ODR
	#define IIR_CO4 (0x2<<0) //IIR_CO为4，IIR滤波器开启，带宽0.092 × ODR
	#define IIR_CO8 (0x3<<0) //IIR_CO为8，IIR滤波器开启，带宽0.043 × ODR
	#define IIR_CO16 (0x4<<0) //IIR_CO为16，IIR滤波器开启，带宽0.021 × ODR
	//系统配置寄存器，参数配置值的对应关系定义_结束
	//============================================================

	//============================================================
	//模拟前端配置寄存器1(AFE_CFG1_REG)，参数配置值的对应关系定义，客户请勿修改
	#define EXT_TEMP_N (0x0<<15) //使用内部温度传感器
	#define EXT_TEMP_Y  (0x1<<15) //使用外部温度传感器
	#define AMP_POLAR_P (0x0<<14) //信号与物理量成正比关系，VBP – VBN
	#define AMP_POLAR_N (0x1<<14) //信号与物理量成反比关系，VBN – VBP
	#define VOFFSET_POLAR_P (0x0<<13) //Offset电压极性为正
	#define VOFFSET_POLAR_N (0x1<<13) //Offset电压极性为负

	#define VOFFSET_0 (0x00<<7) //OFFSET电压0/128
	#define VOFFSET_1 (0x01<<7) //OFFSET电压1/128
	#define VOFFSET_2 (0x02<<7) //OFFSET电压2/128
	#define VOFFSET_3 (0x03<<7) //OFFSET电压3/128
	#define VOFFSET_4 (0x04<<7) //OFFSET电压4/128
	#define VOFFSET_5 (0x05<<7) //OFFSET电压5/128
	#define VOFFSET_6 (0x06<<7) //OFFSET电压6/128
	#define VOFFSET_7 (0x07<<7) //OFFSET电压7/128
	#define VOFFSET_8 (0x08<<7) //OFFSET电压8/128
	#define VOFFSET_9 (0x09<<7) //OFFSET电压9/128
	#define VOFFSET_10 (0x0A<<7) //OFFSET电压10/128
	#define VOFFSET_11 (0x0B<<7) //OFFSET电压11/128
	#define VOFFSET_12 (0x0C<<7) //OFFSET电压12/128
	#define VOFFSET_13 (0x0D<<7) //OFFSET电压13/128
	#define VOFFSET_14 (0x0E<<7) //OFFSET电压14/128
	#define VOFFSET_15 (0x0F<<7) //OFFSET电压15/128
	#define VOFFSET_16 (0x10<<7) //OFFSET电压16/128
	#define VOFFSET_17 (0x11<<7) //OFFSET电压17/128
	#define VOFFSET_18 (0x12<<7) //OFFSET电压18/128
	#define VOFFSET_19 (0x13<<7) //OFFSET电压19/128
	#define VOFFSET_20 (0x14<<7) //OFFSET电压20/128
	#define VOFFSET_21 (0x15<<7) //OFFSET电压21/128
	#define VOFFSET_22 (0x16<<7) //OFFSET电压22/128
	#define VOFFSET_23 (0x17<<7) //OFFSET电压23/128
	#define VOFFSET_24 (0x18<<7) //OFFSET电压24/128
	#define VOFFSET_25 (0x19<<7) //OFFSET电压25/128
	#define VOFFSET_26 (0x1A<<7) //OFFSET电压26/128
	#define VOFFSET_27 (0x1B<<7) //OFFSET电压27/128
	#define VOFFSET_28 (0x1C<<7) //OFFSET电压28/128
	#define VOFFSET_29 (0x1D<<7) //OFFSET电压29/128
	#define VOFFSET_30 (0x1E<<7) //OFFSET电压30/128
	#define VOFFSET_31 (0x1F<<7) //OFFSET电压31/128
	#define VOFFSET_32 (0x20<<7) //OFFSET电压32/128
	#define VOFFSET_33 (0x21<<7) //OFFSET电压33/128
	#define VOFFSET_34 (0x22<<7) //OFFSET电压34/128
	#define VOFFSET_35 (0x23<<7) //OFFSET电压35/128
	#define VOFFSET_36 (0x24<<7) //OFFSET电压36/128
	#define VOFFSET_37 (0x25<<7) //OFFSET电压37/128
	#define VOFFSET_38 (0x26<<7) //OFFSET电压38/128
	#define VOFFSET_39 (0x27<<7) //OFFSET电压39/128
	#define VOFFSET_40 (0x28<<7) //OFFSET电压40/128
	#define VOFFSET_41 (0x29<<7) //OFFSET电压41/128
	#define VOFFSET_42 (0x2A<<7) //OFFSET电压42/128
	#define VOFFSET_43 (0x2B<<7) //OFFSET电压43/128
	#define VOFFSET_44 (0x2C<<7) //OFFSET电压44/128
	#define VOFFSET_45 (0x2D<<7) //OFFSET电压45/128
	#define VOFFSET_46 (0x2E<<7) //OFFSET电压46/128
	#define VOFFSET_47 (0x2F<<7) //OFFSET电压47/128
	#define VOFFSET_48 (0x30<<7) //OFFSET电压48/128
	#define VOFFSET_49 (0x31<<7) //OFFSET电压49/128
	#define VOFFSET_50 (0x32<<7) //OFFSET电压50/128
	#define VOFFSET_51 (0x33<<7) //OFFSET电压51/128
	#define VOFFSET_52 (0x34<<7) //OFFSET电压52/128
	#define VOFFSET_53 (0x35<<7) //OFFSET电压53/128
	#define VOFFSET_54 (0x36<<7) //OFFSET电压54/128
	#define VOFFSET_55 (0x37<<7) //OFFSET电压55/128
	#define VOFFSET_56 (0x38<<7) //OFFSET电压56/128
	#define VOFFSET_57 (0x39<<7) //OFFSET电压57/128
	#define VOFFSET_58 (0x3A<<7) //OFFSET电压58/128
	#define VOFFSET_59 (0x3B<<7) //OFFSET电压59/128
	#define VOFFSET_60 (0x3C<<7) //OFFSET电压60/128
	#define VOFFSET_61 (0x3D<<7) //OFFSET电压61/128
	#define VOFFSET_62 (0x3E<<7) //OFFSET电压62/128
	#define VOFFSET_63 (0x3F<<7) //OFFSET电压63/128

	#define GAIN3_1p1 (0x0<<4) //三级增益1.1倍
	#define GAIN3_1p2 (0x1<<4) //三级增益1.2倍
	#define GAIN3_1p3 (0x2<<4) //三级增益1.3倍
	#define GAIN3_1p4 (0x3<<4) //三级增益1.4倍
	#define GAIN3_1p5 (0x4<<4) //三级增益1.5倍
	#define GAIN3_1p6 (0x5<<4) //三级增益1.6倍
	#define GAIN3_1p7 (0x6<<4) //三级增益1.7倍
	#define GAIN3_1p8 (0x7<<4) //三级增益1.8倍

	#define GAIN2_2x (0x0<<2) //二级增益2倍
	#define GAIN2_3x (0x1<<2) //二级增益3倍
	#define GAIN2_5x (0x2<<2) //二级增益5倍
	#define GAIN2_7x (0x3<<2) //二级增益7倍

	#define GAIN1_2p0 (0x0<<0) //一级增益2.0倍
	#define GAIN1_5p0 (0x1<<0) //一级增益5.0倍
	#define GAIN1_13p3 (0x2<<0) //一级增益13.3倍
	#define GAIN1_40p0 (0x3<<0) //一级增益40.0倍
	//模拟前端配置寄存器1，参数配置值的对应关系定义_结束
	//============================================================

	//============================================================
	//过采样率配置对应关系定义，客户请勿修改
	#define OSR_P_x128 (0x0<<0) //过采样率x128，测量时间69.6ms
	#define OSR_P_x64 (0x1<<0) //过采样率x64，测量时间35.6ms
	#define OSR_P_x32 (0x2<<0) //过采样率x32，测量时间18.8ms
	#define OSR_P_x16 (0x3<<0) //过采样率x16，测量时间10.4ms
	#define OSR_P_x8 (0x4<<0) //过采样率x8，测量时间6.2ms
	#define OSR_P_x4 (0x5<<0) //过采样率x4，测量时间4ms
	#define OSR_P_x2 (0x6<<0) //过采样率x2，测量时间3ms
	#define OSR_P_x1 (0x7<<0) //过采样率x1，测量时间2.4ms

	#define OSR_T_x128 (0x0<<4) //过采样率x128，测量时间69.6ms
	#define OSR_T_x64 (0x1<<4) //过采样率x64，测量时间35.6ms
	#define OSR_T_x32 (0x2<<4) //过采样率x32，测量时间18.8ms
	#define OSR_T_x16 (0x3<<4) //过采样率x16，测量时间10.4ms
	#define OSR_T_x8 (0x4<<4) //过采样率x8，测量时间6.2ms
	#define OSR_T_x4 (0x5<<4) //过采样率x4，测量时间4ms
	#define OSR_T_x2 (0x6<<4) //过采样率x2，测量时间3ms
	#define OSR_T_x1 (0x7<<4) //过采样率x1，测量时间2.4ms
	//过采样率配置对应关系定义_结束
	//============================================================

	//============================================================
	//状态字节相关定义，客户请勿修改
	#define BUSY_OF_STATUS (0x1<<7) //忙闲指示(Busy indication)
	#define MODE_OF_STATUS (0x3<<5) //当前工作模式 (Mode Status)
	#define CRC_OF_STATUS (0x1<<4) //CRC_flag/存储器数据完整性指示
	#define CAL_EN_OF_STATUS (0x1<<3) //校准计算使能(Calibration Enable)
	#define FIFO_EN_OF_STATUS (0x1<<2) //FIFO Enable
	#define FIFO_FULL_OF_STATUS (0x1<<1) //FIFO_FULL
	#define FIFO_EMPTY_OF_STATUS (0x1<<0) //FIFO_EMPTY

	#define STATUS_IS_BUSY (0x1<<7)
	#define STATUS_IS_IDLE (0x0<<7)
	#define STATUS_IS_NOR (0x0<<5)
	#define STATUS_IS_CYC (0x1<<5)
	#define STATUS_IS_CMD1 (0x2<<5)
	#define STATUS_IS_CMD2 (0x3<<5)
	#define STATUS_IS_CRC_PASS (0x0<<4)
	#define STATUS_IS_CRC_FAIL (0x1<<4)
	#define STATUS_IS_EN_CAL (0x1<<3)
	#define STATUS_IS_DIS_CAL (0x0<<3)
	#define STATUS_IS_EN_FIFO (0x1<<2)
	#define STATUS_IS_DIS_FIFO (0x0<<2)
	#define STATUS_IS_FIFO_FULL (0x1<<1)
	#define STATUS_IS_FIFO_NOT_FULL (0x0<<1)
	#define STATUS_IS_FIFO_EMPTY (0x1<<0)
	#define STATUS_IS_FIFO_NOT_EMPTY (0x0<<0)
	//状态字节相关定义_结束
	//============================================================

#endif //end of "#ifndef __JHM1400D_H"
