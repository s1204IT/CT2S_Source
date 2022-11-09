/*
 *  Copyright (C) 2014 SANYO Techno Solutions Tottori Co., Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */

#ifndef __DTS_PXA1928_MA03_PINFUNC_H
#define __DTS_PXA1928_MA03_PINFUNC_H

#include "pxa1928-pinfunc.h"

#ifndef PXA1928_DISCRETE
#error PXA1928_DISCRETE is not defined.
#endif /* PXA1928_DISCRETE */

/*******************************************************************************
 * MPFR config
 ******************************************************************************/
#define UART3_RXD		P33  0x7	// AP_UART3_RXD
#define UART3_TXD		P34  0x7	// AP_UART3_TXD
#define TWSI6_SCL		P35  0x5	// NFC_I2C_SCL
#define TWSI6_SDA		P36  0x5	// NFC_I2C_SDA
#define MMC2_DAT3		P37  0x0	// WIFI_DAT[3]
#define MMC2_DAT2		P38  0x0	// WIFI_DAT[2]
#define MMC2_DAT1		P39  0x0	// WIFI_DAT[1]
#define MMC2_DAT0		P40  0x0	// WIFI_DAT[0]
#define MMC2_CMD		P41  0x0	// WIFI_CMD
#define MMC2_CLK		P42  0x0	// WIFI_CLK
//#define GPIO_55		P55  0x1	// TP502
//#define GPIO_56		P56  0x1	// TP503
//#define GPIO_57		P57  0x1	// TP504
//#define GPIO_58		P58  0x1	// TP505
#define MMC1_DAT3		P59  0x0	// SDCARD_DAT[3]
#define MMC1_DAT2		P60  0x0	// SDCARD_DAT[2]
#define MMC1_DAT1		P61  0x0	// SDCARD_DAT[1]
#define MMC1_DAT0		P62  0x0	// SDCARD_DAT[0]
#define MMC1_CMD		P63  0x0	// SDCARD_CMD
#define MMC1_CLK		P64  0x0	// SDCARD_CLK
//#define GPIO_65		P65  0x1	// (Unused)
//#define GPIO_66		P66  0x1	// (Unused)
#define PWR_SCL			P67  0x0	// PWR_I2C_SCL
#define CP_TWSI_SCL		P67  0x2	// (PWR_I2C_SCL)
#define PWR_SDA			P68  0x0	// PWR_I2C_SDA
#define CP_TWSI_SDA		P68  0x2	// (PWR_I2C_SDA)
#define PRI_TDI			P69  0x0	// JTAG_TDI
#define PRI_TMS			P70  0x0	// JTAG_TMS
#define PRI_TCK			P71  0x0	// JTAG_TCK
#define PRI_TDO			P72  0x0	// JTAG_TDO
//#define GPIO_73		P73  0x1	// TP524
//#define GPIO_74		P74  0x1	// TP548
//#define GPIO_75		P75  0x1	// BAT_IRQ
//#define GPIO_76		P76  0x0	// TP547
//#define GPIO_77		P77  0x1	// TP549
#define VCXO_OUT		P78  0x0	// GPS_MCLK_26MHZ
#define MN_CLK_OUT		P79  0x0	// VCLK_13KHZ_1V8
#define MMC3_DAT7		P80  0x1	// EMMC_DAT[7]
#define MMC3_DAT6		P81  0x1	// EMMC_DAT[6]
#define MMC3_DAT5		P82  0x1	// EMMC_DAT[5]
#define MMC3_DAT4		P83  0x1	// EMMC_DAT[4]
#define MMC3_DAT3		P84  0x1	// EMMC_DAT[3]
#define MMC3_DAT2		P85  0x1	// EMMC_DAT[2]
#define MMC3_DAT1		P86  0x1	// EMMC_DAT[1]
#define MMC3_DAT0		P87  0x1	// EMMC_DAT[0]
#define MMC3_CLK		P88  0x1	// EMMC_CLK
#define MMC3_CMD		P89  0x1	// EMMC_CMD
#define MMC3_RST		P90  0x1	// EMMC_RESET_N
//#define GPIO_91		P91  0x2	// BOOTSTRAP[7]
//#define GPIO_92		P92  0x2	// BOOTSTRAP[6]
//#define GPIO_93		P93  0x2	// BOOTSTRAP[5]
//#define GPIO_94		P94  0x2	// BOOTSTRAP[4]
//#define GPIO_95		P95  0x2	// BOOTSTRAP[3]
//#define GPIO_96		P96  0x2	// BOOTSTRAP[2]
//#define GPIO_97		P97  0x2	// BOOTSTRAP[1]
//#define GPIO_98		P98  0x2	// BOOTSTRAP[0]
#define GPIO_DVCO2		P99  0x7	// DVC[3]
#define MMC1_CD			P100 0x7	// ND_CS1_N
//#define GPIO_101		P101 0x2	// TP571
//#define GPIO_102		P102 0x2	// TP570
#define GPIO_DVCO3		P103 0x7	// DVC[4]
//#define GPIO_104		P104 0x2	// TP569
//#define GPIO_105		P105 0x2	// TP568
//#define GPIO_106		P106 0x2	// TP567
#define GPIO_DVCO0		P107 0x7	// DVC[1]
#define GPIO_DVCO1		P108 0x7	// DVC[2]
#define CP_UART_TXD		P109 0x3	// CP_UART1_TXD
#define CP_UART_RXD		P110 0x3	// CP_UART1_RXD
//#define GPIO_111		P111 0x0	// FCAM_RESET_N
//#define GPIO_112		P112 0x0	// NFC_RSTPD_N
//#define GPIO_113		P113 0x0	// RCAM_RESET_N
//#define GPIO_114		P114 0x0	// PDIG_RESET_N
//#define GPIO_115		P115 0x0	// PDIG_PEN_DETECT
//#define GPIO_116		P116 0x0	// PDIG_FWE[0]
//#define GPIO_117		P117 0x0	// PDIG_FWE[1]
//#define GPIO_118		P118 0x0	// WIFI_RESET_N
//#define GPIO_119		P119 0x0	// WIFI_PDN
#define CAM_MCLK_F		P120 0x2	// FCAM_MCLK_26MHZ
//#define GPIO_121		P121 0x0	// VX5_RESET_N
//#define GPIO_122		P122 0x0	// GPS_RESET_N
//#define GPIO_123		P123 0x1	// TP573
//#define GPIO_124		P124 0x1	// TP574
//#define GPIO_125		P125 0x1	// PDIG_IRQ
//#define GPIO_126		P126 0x1	// ACCEL_INT1
//#define GPIO_127		P127 0x1	// ACCEL_INT2
//#define GPIO_128		P128 0x0	// GPS_SYNC
//#define GPIO_129		P129 0x0	// CHRG_EN_N
//#define GPIO_130		P130 0x0	// GPS_1SEC_PULSE
//#define GPIO_131		P131 0x0	// FCAM_ALS_INT
//#define GPIO_132		P132 0x0	// ALS_INT
//#define GPIO_133		P133 0x0	// TP572
//#define GPIO_134		P134 0x0	// CHRG_INT
//#define GPIO_135		P135 0x0	// VBST_5V0_ON
//#define GPIO_139		P139 0x0	// GPIO_139 (for debug boot switch)
//#define GPIO_145		P145 0x0	// USB_OTG_VBUS_ON
//#define GPIO_150		P150 0x0	// LCD_POWER_ON
//#define GPIO_151		P151 0x0	// LCD_BACKLIGHT_ON
#define CAM_MCLK_R		P159 0x3	// RCAM_MCLK_26MHZ
#define KEY_VOL_UP       	P160 0x4	// KEY_VOL_UP
#define AUDIO_REFCLK		P161 0x6	// AUDIO_REF_CLK_32KHZ
#define KEY_VOL_DOWN     	P162 0x1	// KEY_VOL_DOWN
#define TWSI3_SCL_2		P163 0x4	// (FCAM_I2C_SCL)
#define TWSI2_ISP_SCL		P163 0x6	// FCAM_I2C_SCL
#define TWSI3_SDA_2		P164 0x4	// (FCAM_I2C_SDA)
#define TWSI2_ISP_SDA		P164 0x6	// FCAM_I2C_SDA
//#define GPIO_165		P165 0x0	// (Unused)
#define I2S1_BITCLK		P166 0x1	// CODEC_I2S_BCLK
#define I2S1_SYNC		P167 0x1	// CODEC_I2S_SYNC
#define I2S1_DATA_OUT		P168 0x1	// CODEC_I2S_DA_DATA
#define I2S1_SDATA_IN		P169 0x1	// CODEC_I2S_AD_DATA
#define I2S2_SYNC		P170 0x6	// BT_I2S_SYNC
#define I2S2_BITCLK		P171 0x6	// BT_I2S_BITCLK
#define I2S2_DATA_OUT		P172 0x6	// BT_I2S_DA_DATA
#define I2S2_SDATA_IN		P173 0x6	// BT_I2S_AD_DATA
#define TWSI5_SCL		P174 0x7	// TP/VX5_I2C_SCL
#define TWSI5_SDA		P175 0x7	// TP/VX5_I2C_SDA
#define TWSI1_ISP_SCL		P176 0x6	// RCAM_I2C_SCL
#define TWSI3_SCL_1		P176 0x7	// (RCAM_I2C_SCL)
#define TWSI1_ISP_SDA		P177 0x6	// RCAM_I2C_SDA
#define TWSI3_SDA_1		P177 0x7	// (RCAM_I2C_SDA)
#define TWSI2_SCL		P178 0x6	// INERT_I2C_SCL
#define TWSI2_SDA		P179 0x6	// INERT_I2C_SDA
#define TWSI4_SDA		P180 0x6	// ALS_I2C_SDA
#define TWSI4_SCL		P181 0x6	// ALS_I2C_SCL
#define UART4_TXD		P182 0x2	// CPU_TXD-GPS_RXD
#define UART4_RXD		P183 0x2	// GPS_TXD-CPU_RXD
#define UART4_CTS		P184 0x2	// GPS_CTS-CPU_CTS
#define UART4_RTS		P185 0x2	// CPU_RTS-GPU_RTS
//#define GPIO_186		P186 0x0	// (Unused)
//#define GPIO_187		P187 0x0	// TP_INT
//#define GPIO_188		P188 0x0	// GPS_PWR_ON
//#define GPIO_189		P189 0x0	// TP_ON/OFF_SW
//#define GPIO_190		P190 0x0	// INERT_INT2_A/G
//#define GPIO_191		P191 0x0	// INERT_INT1_A/G
//#define GPIO_192		P192 0x0	// INERT_DEN_A/G
//#define GPIO_193		P193 0x0	// INERT_INT_M
//#define GPIO_194		P194 0x0	// INERT_DRDY_M
//#define GPIO_195		P195 0x0	// TP559
//#define GPIO_196		P196 0x0	// TP558
//#define GPIO_197		P197 0x0	// TP560

/*******************************************************************************
 * GPIO port number
 ******************************************************************************/
#define MA01_AP_UART3_RXD	33
#define MA01_NFC_I2C_SCL	35
#define MA01_NFC_I2C_SDA	36
#define MA01_WIFI_DAT1		39
#define MA01_TP502		55
#define MA01_TP503		56
#define MA01_TP504		57
#define MA01_TP505		58
#define MA01_PWR_I2C_SCL	67
#define MA01_PWR_I2C_SDA	68
#define MA01_TP524		73
#define MA01_TP548		74
#define MA01_BAT_IRQ		75
#define MA01_TP547		76
#define MA01_TP549		77
#define MA01_BOOTSTRAP7		91
#define MA01_BOOTSTRAP6		92
#define MA01_BOOTSTRAP5		93
#define MA01_BOOTSTRAP4		94
#define MA01_BOOTSTRAP3		95
#define MA01_BOOTSTRAP2		96
#define MA01_BOOTSTRAP1		97
#define MA01_BOOTSTRAP0		98
#define MA01_TP571		101
#define MA01_TP570		102
#define MA01_TP569		104
#define MA01_TP568		105
#define MA01_TP567		106
#define MA01_FCAM_RESET_N	111
#define MA01_NFC_RSTPD_N	112
#define MA01_RCAM_RESET_N	113
#define MA01_PDIG_RESET_N	114
#define MA01_PDIG_PEN_DETECT	115
#define MA01_PDIG_FWE0		116
#define MA01_PDIG_FWE1		117
#define MA01_WIFI_RESET_N	118
#define MA01_WIFI_PDN		119
#define MA01_FCAM_MCLK_26MHZ	120
#define MA01_VX5_RESET_N	121
#define MA01_GPS_RESET_N	122
#define MA01_TP573		123
#define MA01_TP574		124
#define MA01_PDIG_IRQ		125
#define MA01_ACCEL_INT1		126
#define MA01_ACCEL_INT2		127
#define MA01_GPS_SYNC		128
#define MA01_CHRG_EN_N		129
#define MA01_GPS_1SEC_PULSE	130
#define MA01_FCAM_ALS_INT	131
#define MA01_ALS_INT		132
#define MA01_TP572		133
#define MA01_CHRG_INT		134
#define MA01_LCD_PWR_ON	135
#define MA01_GPIO_139		139
#define MA01_USB_OTG_VBUS_ON	145
#define MA01_TP_RESET_N		147
#define MA01_LCD_POWER_ON	150
#define MA01_LCD_BACKLIGHT_ON	151
#define MA01_RCAM_STBY_N	157
#define MA01_FCAM_STBY_N	158
#define MA01_RCAM_MCLK_26MHZ	159
#define MA01_FCAM_I2C_SCL	163
#define MA01_FCAM_I2C_SDA	164
#define MA01_TPVX5_I2C_SCL	174
#define MA01_TPVX5_I2C_SDA	175
#define MA01_RCAM_I2C_SCL	176
#define MA01_RCAM_I2C_SDA	177
#define MA01_INERT_I2C_SCL	178
#define MA01_INERT_I2C_SDA	179
#define MA01_ALS_I2C_SDA	180
#define MA01_ALS_I2C_SCL	181
#define MA01_TP_INT		187
#define MA01_GPS_PWR_ON		188
#define MA01_HOME_KEY		189
#define MA01_INERT_INT2_AG	190
#define MA01_INERT_INT1_AG	191
#define MA01_INERT_DEN_AG	192
#define MA01_INERT_INT_M	193
#define MA01_INERT_DRDY_M	194
#define MA01_LCD_MICOM		195
#define MA01_LCD_ORIENT		196
#define MA01_LCD_POWER_TYPE		197

#endif /* __DTS_PXA1928_MA03_PINFUNC_H */
