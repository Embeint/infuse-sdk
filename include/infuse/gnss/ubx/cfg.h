/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * UBX modem configuration interface, as introduced in Protocol Version 23.01.
 */

#ifndef INFUSE_GNSS_UBX_CFG_H_
#define INFUSE_GNSS_UBX_CFG_H_

#include <zephyr/net_buf.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/gnss/ubx/protocol.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup ubx_cfg UBX Modem Configuration
 * @{
 *
 * Expected usage of configuration interface:
 * @code{.c}
 * struct ubx_msg_cfg_valset_v0 *valset;
 * NET_BUF_SIMPLE_DEFINE(cfg_buf, 32);
 *
 * ubx_msg_prepare_valset(&cfg_buf, UBX_MSG_CFG_VALSET_LAYERS_RAM | UBX_MSG_CFG_VALSET_LAYERS_BBR);
 * UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_RATE_MEAS, fix_interval_ms);
 * UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_RATE_NAV, 1);
 * ubx_msg_finalise(&cfg_buf);
 * @endcode
 */

/** Bits 30-28 of 32 bit key ID */
enum _ubx_cfg_key_size {
	_UBX_CFG_KEY_SIZE_1BIT_ = (0x01 << 28),
	_UBX_CFG_KEY_SIZE_1BYTE = (0x02 << 28),
	_UBX_CFG_KEY_SIZE_2BYTE = (0x03 << 28),
	_UBX_CFG_KEY_SIZE_4BYTE = (0x04 << 28),
	_UBX_CFG_KEY_SIZE_8BYTE = (0x05 << 28),
	_UBX_CFG_KEY_SIZE_MASK = (0x07 << 28),
};

/** Bits 23-16 of 32 bit key ID */
enum _ubx_cfg_key_grp {
	/** AssistNow Autonomous and Offline configuration */
	_UBX_CFG_KEY_GRP_ANA = (0x23 << 16),
	/** Batched output configuration */
	_UBX_CFG_KEY_GRP_BATCH = (0x26 << 16),
	/** BeiDou system configuration */
	_UBX_CFG_KEY_GRP_BDS = (0x34 << 16),
	/** Hardware configuration */
	_UBX_CFG_KEY_GRP_HW = (0xa3 << 16),
	/** Configuration of the I2C interface */
	_UBX_CFG_KEY_GRP_I2C = (0x51 << 16),
	/** Input protocol configuration of the I2C interface */
	_UBX_CFG_KEY_GRP_I2CINPROT = (0x71 << 16),
	/** Output protocol configuration of the I2C interface */
	_UBX_CFG_KEY_GRP_I2COUTPROT = (0x72 << 16),
	/** Information message configuration */
	_UBX_CFG_KEY_GRP_INFMSG = (0x92 << 16),
	/** Jamming and interference monitor configuration */
	_UBX_CFG_KEY_GRP_ITFM = (0x41 << 16),
	/** Motion detector configuration */
	_UBX_CFG_KEY_GRP_MOT = (0x25 << 16),
	/** Message output configuration */
	_UBX_CFG_KEY_GRP_MSGOUT = (0x91 << 16),
	/** Standard precision navigation configuration */
	_UBX_CFG_KEY_GRP_NAVSPG = (0x11 << 16),
	/** NMEA protocol configuration */
	_UBX_CFG_KEY_GRP_NMEA = (0x93 << 16),
	/** Odometer and low-speed course over ground filter configuration */
	_UBX_CFG_KEY_GRP_ODO = (0x22 << 16),
	/** Configuration for receiver power management */
	_UBX_CFG_KEY_GRP_PM = (0xd0 << 16),
	/** QZSS system configuration */
	_UBX_CFG_KEY_GRP_QZSS = (0x37 << 16),
	/** Navigation and measurement rate configuration */
	_UBX_CFG_KEY_GRP_RATE = (0x21 << 16),
	/** Remote inventory */
	_UBX_CFG_KEY_GRP_RINV = (0xc7 << 16),
	/** SBAS configuration */
	_UBX_CFG_KEY_GRP_SBAS = (0x36 << 16),
	/** Security configuration */
	_UBX_CFG_KEY_GRP_SEC = (0xf6 << 16),
	/** Satellite systems (GNSS) signal configuration */
	_UBX_CFG_KEY_GRP_SIGNAL = (0x31 << 16),
	/** Configuration of the SPI interface */
	_UBX_CFG_KEY_GRP_SPI = (0x64 << 16),
	/** Input protocol configuration of the SPI interface */
	_UBX_CFG_KEY_GRP_SPIINPROT = (0x79 << 16),
	/** Output protocol configuration of the SPI interface */
	_UBX_CFG_KEY_GRP_SPIOUTPROT = (0x7a << 16),
	/** Time pulse configuration */
	_UBX_CFG_KEY_GRP_TP = (0x05 << 16),
	/** TX ready configuration */
	_UBX_CFG_KEY_GRP_TXREADY = (0xa2 << 16),
	/** Configuration of the UART1 interface */
	_UBX_CFG_KEY_GRP_UART1 = (0x52 << 16),
	/** Input protocol configuration of the UART1 interface */
	_UBX_CFG_KEY_GRP_UART1INPROT = (0x73 << 16),
	/** Output protocol configuration of the UART1 interface */
	_UBX_CFG_KEY_GRP_UART1OUTPROT = (0x74 << 16),
};

#define _UBX_CFG_KEY_NAME(group, name) UBX_CFG_KEY_##group##_##name

/**
 * @brief Define a UBX configuration key
 *
 * @param name Name of key value
 * @param size Size of value (@ref _ubx_cfg_key_size)
 * @param group Configuration group key belongs to (@ref _ubx_cfg_key_grp)
 * @param id Unique identifier within group
 */
#define _UBX_CFG_KEY_DEFINE(size, group, id, name)                                                 \
	_UBX_CFG_KEY_NAME(group, name) = (size | (_UBX_CFG_KEY_GRP_##group) | id)

/**
 * @addtogroup UBX_CFG_KEY_ANA
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_ana {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, ANA, 0x01, USE_ANA),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_2BYTE, ANA, 0x02, ORBMAXERR),
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_BATCH
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_batch {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, BATCH, 0x13, ENABLE),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, BATCH, 0x14, PIOENABLE),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_2BYTE, BATCH, 0x15, MAXENTRIES),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_2BYTE, BATCH, 0x16, WARNTHRS),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, BATCH, 0x18, PIOACTIVELOW),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, BATCH, 0x19, PIOID),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, BATCH, 0x1a, EXTRAPVT),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, BATCH, 0x1b, EXTRAODO),
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_BDS
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_bds {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, BDS, 0x14, USE_GEO_PRN),
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_HW
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_hw {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, HW, 0x2E, ANT_CFG_VOLTCTRL),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, HW, 0x2F, ANT_CFG_SHORTDET),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, HW, 0x30, ANT_CFG_SHORTDET_POL),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, HW, 0x31, ANT_CFG_OPENDET),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, HW, 0x32, ANT_CFG_OPENDET_POL),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, HW, 0x33, ANT_CFG_PWRDOWN),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, HW, 0x34, ANT_CFG_PWRDOWN_POL),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, HW, 0x35, ANT_CFG_RECOVER),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, HW, 0x36, ANT_SUP_SWITCH_PIN),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, HW, 0x37, ANT_SUP_SHORT_PIN),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, HW, 0x38, ANT_SUP_OPEN_PIN),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_2BYTE, HW, 0x3C, ANT_ON_SHORT_US),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, HW, 0x54, ANT_SUP_ENGINE),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, HW, 0x55, ANT_SUP_SHORT_THR),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, HW, 0x56, ANT_SUP_OPEN_THR),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, HW, 0x57, RF_LNA_MODE),
};

enum ubx_cfg_key_hw_ant_sup_engine {
	UBX_CFG_HW_ANT_SUP_ENGINE_EXT = 0,
	UBX_CFG_HW_ANT_SUP_ENGINE_MADC = 1,
};

enum ubx_cfg_key_hw_rf_lna_mode {
	UBX_CFG_HW_RF_LNA_MODE_NORMAL = 0,
	UBX_CFG_HW_RF_LNA_MODE_LOWGAIN = 1,
	UBX_CFG_HW_RF_LNA_MODE_BYPASS = 2,
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_I2C
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_i2c {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, I2C, 1, ADDRESS),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, I2C, 2, EXTENDEDTIMEOUT),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, I2C, 3, ENABLED),
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_I2CINPROT
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_i2cinprot {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, I2CINPROT, 1, UBX),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, I2CINPROT, 2, NMEA),
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_I2COUTPROT
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_i2coutprot {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, I2COUTPROT, 1, UBX),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, I2COUTPROT, 2, NMEA),
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_INFMSG
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_infmsg {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, INFMSG, 0x01, UBX_I2C),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, INFMSG, 0x02, UBX_UART1),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, INFMSG, 0x05, UBX_SPI),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, INFMSG, 0x06, NMEA_I2C),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, INFMSG, 0x07, NMEA_UART1),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, INFMSG, 0x0a, NMEA_SPI),
};

/* All messages in this class are always from this enum */
enum ubx_cfg_key_infmsg_all {
	UBX_CFG_INFMSG_ALL_ERROR = 0x01,
	UBX_CFG_INFMSG_ALL_WARNING = 0x02,
	UBX_CFG_INFMSG_ALL_NOTICE = 0x04,
	UBX_CFG_INFMSG_ALL_TEST = 0x08,
	UBX_CFG_INFMSG_ALL_DEBUG = 0x10,
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_ITFM
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_itfm {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, ITFM, 0x01, BBTHRESHOLD),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, ITFM, 0x02, CWTHRESHOLD),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, ITFM, 0x0d, ENABLE),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, ITFM, 0x10, ANTSETTING),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, ITFM, 0x13, ENABLE_AUX),
};

enum ubx_cfg_key_itfm_antsetting {
	UBX_CFG_ITFM_ANTSETTING_UNKNOWN = 0,
	UBX_CFG_ITFM_ANTSETTING_PASSIVE = 1,
	UBX_CFG_ITFM_ANTSETTING_ACTIVE = 2,
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_MOT
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_mot {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, MOT, 0x38, GNSSSPEED_THRS),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_2BYTE, MOT, 0x3b, GNSSDIST_THRS),
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_MSGOUT
 * @ingroup ubx_cfg
 * @{
 */

/**
 * @brief Define the MSGOUT keys for all three interfaces
 *
 * Happily the key values follow a defined pattern
 */
#define _UBX_CFG_MSGOUT_KEY_DEFINE(i2c_val, name)                                                  \
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, MSGOUT, (i2c_val), name##_I2C),               \
		_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, MSGOUT, (i2c_val + 1), name##_UART1), \
		_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, MSGOUT, (i2c_val + 4), name##_SPI)

enum ubx_cfg_key_msgout {
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00a6, NMEA_ID_DTM_I2C),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00aa, NMEA_ID_DTM_SPI),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00a7, NMEA_ID_DTM_UART1),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00dd, NMEA_ID_GBS_I2C),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00e1, NMEA_ID_GBS_SPI),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00de, NMEA_ID_GBS_UART1),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00ba, NMEA_ID_GGA_I2C),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00be, NMEA_ID_GGA_SPI),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00bb, NMEA_ID_GGA_UART1),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00c9, NMEA_ID_GLL_I2C),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00cd, NMEA_ID_GLL_SPI),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00ca, NMEA_ID_GLL_UART1),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00b5, NMEA_ID_GNS_I2C),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00b9, NMEA_ID_GNS_SPI),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00b6, NMEA_ID_GNS_UART1),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00ce, NMEA_ID_GRS_I2C),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00d2, NMEA_ID_GRS_SPI),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00cf, NMEA_ID_GRS_UART1),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00bf, NMEA_ID_GSA_I2C),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00c3, NMEA_ID_GSA_SPI),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00c0, NMEA_ID_GSA_UART1),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00d3, NMEA_ID_GST_I2C),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00d7, NMEA_ID_GST_SPI),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00d4, NMEA_ID_GST_UART1),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00c4, NMEA_ID_GSV_I2C),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00c8, NMEA_ID_GSV_SPI),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00c5, NMEA_ID_GSV_UART1),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0400, NMEA_ID_RLM_I2C),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0404, NMEA_ID_RLM_SPI),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0401, NMEA_ID_RLM_UART1),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00ab, NMEA_ID_RMC_I2C),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00af, NMEA_ID_RMC_SPI),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00ac, NMEA_ID_RMC_UART1),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00e7, NMEA_ID_VLW_I2C),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00eb, NMEA_ID_VLW_SPI),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00e8, NMEA_ID_VLW_UART1),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00b0, NMEA_ID_VTG_I2C),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00b4, NMEA_ID_VTG_SPI),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00b1, NMEA_ID_VTG_UART1),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00d8, NMEA_ID_ZDA_I2C),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00dc, NMEA_ID_ZDA_SPI),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00d9, NMEA_ID_ZDA_UART1),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00ec, PUBX_ID_POLYP_I2C),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00f0, PUBX_ID_POLYP_SPI),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00ed, PUBX_ID_POLYP_UART1),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00f1, PUBX_ID_POLYS_I2C),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00f5, PUBX_ID_POLYS_SPI),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00f2, PUBX_ID_POLYS_UART1),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00f6, PUBX_ID_POLYT_I2C),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00fa, PUBX_ID_POLYT_SPI),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x00f7, PUBX_ID_POLYT_UART1),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x034f, UBX_MON_COMMS),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x01b9, UBX_MON_HW2),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0354, UBX_MON_HW3),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x01b4, UBX_MON_HW),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x01a5, UBX_MON_IO),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0196, UBX_MON_MSGPP),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0359, UBX_MON_RF),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x01a0, UBX_MON_RXBUF),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0187, UBX_MON_RXR),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x038b, UBX_MON_SPAN),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x019b, UBX_MON_TXBUF),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0079, UBX_NAV_AOPSTATUS),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0065, UBX_NAV_CLOCK),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0083, UBX_NAV_COV),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0038, UBX_NAV_DOP),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x015f, UBX_NAV_EOE),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x007e, UBX_NAV_ODO),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0010, UBX_NAV_ORB),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0024, UBX_NAV_PL),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0024, UBX_NAV_POSECEF),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0029, UBX_NAV_POSLLH),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0006, UBX_NAV_PVT),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0015, UBX_NAV_SAT),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x006a, UBX_NAV_SBAS),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0345, UBX_NAV_SIG),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0336, UBX_NAV_SLAS),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x001a, UBX_NAV_STATUS),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0051, UBX_NAV_TIMEBDS),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0056, UBX_NAV_TIMEGAL),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x004c, UBX_NAV_TIMEGLO),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0047, UBX_NAV_TIMEGPS),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0060, UBX_NAV_TIMELS),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0386, UBX_NAV_TIMEQZSS),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x005b, UBX_NAV_TIMEUTC),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x003d, UBX_NAV_VELECEF),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0042, UBX_NAV_VELNED),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0643, UBX_RXM_MEAS20),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0648, UBX_RXM_MEAS50),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x063e, UBX_RXM_MEASC12),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0639, UBX_RXM_MEASD12),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0204, UBX_RXM_MEASX),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x025e, UBX_RXM_RLM),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0231, UBX_RXM_SFRBX),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0178, UBX_RXM_TIM_TM2),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x017d, UBX_RXM_TIM_TP),
	_UBX_CFG_MSGOUT_KEY_DEFINE(0x0092, UBX_RXM_TIM_VRFY),
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_NAVSPG
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_navspg {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, NAVSPG, 0x11, FIXMODE),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, NAVSPG, 0x13, INIFIX3D),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_2BYTE, NAVSPG, 0x17, WKNROLLOVER),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, NAVSPG, 0x1c, UTCSTANDARD),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, NAVSPG, 0x21, DYNMODEL),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, NAVSPG, 0x25, ACKAIDING),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, NAVSPG, 0xa1, INFIL_MINSVS),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, NAVSPG, 0xa2, INFIL_MAXSVS),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, NAVSPG, 0xa3, INFIL_MINCNO),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, NAVSPG, 0xa4, INFIL_MINELEV),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, NAVSPG, 0xaa, INFIL_NCNOTHRS),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, NAVSPG, 0xab, INFIL_CNOTHRS),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_2BYTE, NAVSPG, 0xb1, OUTFIL_PDOP),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_2BYTE, NAVSPG, 0xb2, OUTFIL_TDOP),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_2BYTE, NAVSPG, 0xb3, OUTFIL_PACC),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_2BYTE, NAVSPG, 0xb4, OUTFIL_TACC),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_2BYTE, NAVSPG, 0xb5, OUTFIL_FACC),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_4BYTE, NAVSPG, 0xc1, CONSTR_ALT),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_4BYTE, NAVSPG, 0xc2, CONSTR_ALTVAR),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, NAVSPG, 0xc4, CONSTR_DGNSSTO),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, NAVSPG, 0xd6, SIGATTCOMP),
};

enum ubx_cfg_key_navspg_fixmode {
	UBX_CFG_NAVSPG_FIXMODE_2DONLY = 1,
	UBX_CFG_NAVSPG_FIXMODE_3DONLY = 2,
	UBX_CFG_NAVSPG_FIXMODE_AUTO = 3,
};

enum ubx_cfg_key_navspg_utcstandard {
	/** Automatically selected based on receiver configuration */
	UBX_CFG_NAVSPG_UTCSTANDARD_AUTO = 0,
	/** U.S. Naval Observatory (GPS) */
	UBX_CFG_NAVSPG_UTCSTANDARD_USNO = 3,
	/** Derived from multiple European laboratories (Galileo) */
	UBX_CFG_NAVSPG_UTCSTANDARD_EU = 5,
	/** Former Soviet Union (GLONASS) */
	UBX_CFG_NAVSPG_UTCSTANDARD_SU = 6,
	/** National Time Service Center, China (BeiDou) */
	UBX_CFG_NAVSPG_UTCSTANDARD_NTSC = 7,
	/** National Physics Laboratory, India (NAVIC) */
	UBX_CFG_NAVSPG_UTCSTANDARD_NPLI = 8,
	/** National Institute of Information and Communications Technology, Japan (QZSS) */
	UBX_CFG_NAVSPG_UTCSTANDARD_NICT = 9,
};

/**
 * Dynamic model for the modem.
 * Changes filtering of the position solution and sanity-check limits.
 * Documented limits are from MAX-M10S integration manual.
 */
enum ubx_cfg_key_navspg_dynmodel {
	/* Max Alt: 12 km Max Hor Vel: 310 m/s Max Ver Vel: 50 m/s */
	UBX_CFG_NAVSPG_DYNMODEL_PORTABLE = 0,
	/* Max Alt: 9 km Max Hor Vel: 10 m/s Max Ver Vel: 6 m/s */
	UBX_CFG_NAVSPG_DYNMODEL_STATIONARY = 2,
	/* Max Alt: 9 km Max Hor Vel: 30 m/s Max Ver Vel: 20 m/s */
	UBX_CFG_NAVSPG_DYNMODEL_PEDESTRIAN = 3,
	/* Max Alt: 6 km Max Hor Vel: 100 m/s Max Ver Vel: 15 m/s */
	UBX_CFG_NAVSPG_DYNMODEL_AUTOMOTIVE = 4,
	/* Max Alt: 0.5 km Max Hor Vel: 25 m/s Max Ver Vel: 5 m/s */
	UBX_CFG_NAVSPG_DYNMODEL_SEA = 5,
	/* Max Alt: 80 km Max Hor Vel: 100 m/s Max Ver Vel: 6400 m/s */
	UBX_CFG_NAVSPG_DYNMODEL_AIRBORNE1G = 6,
	/* Max Alt: 80 km Max Hor Vel: 250 m/s Max Ver Vel: 10000 m/s */
	UBX_CFG_NAVSPG_DYNMODEL_AIRBORNE2G = 7,
	/* Max Alt: 80 km Max Hor Vel: 500 m/s Max Ver Vel: 20000 m/s */
	UBX_CFG_NAVSPG_DYNMODEL_AIRBORNE4G = 8,
	/* Max Alt: 9 km Max Hor Vel: 30 m/s Max Ver Vel: 20 m/s */
	UBX_CFG_NAVSPG_DYNMODEL_WRIST = 9,
	/* Unknown */
	UBX_CFG_NAVSPG_DYNMODEL_BIKE = 10,
	/* Unknown */
	UBX_CFG_NAVSPG_DYNMODEL_MOWER = 11,
	/* Unknown */
	UBX_CFG_NAVSPG_DYNMODEL_ESCOOTER = 12,
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_NMEA
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_nmea {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, NMEA, 0x01, PROTVER),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, NMEA, 0x02, MAXSVS),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, NMEA, 0x03, COMPAT),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, NMEA, 0x04, CONSIDER),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, NMEA, 0x05, LIMIT82),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, NMEA, 0x06, HIGHPREC),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, NMEA, 0x07, SVNUMBERING),

	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, NMEA, 0x11, FILST_GPS),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, NMEA, 0x12, FILST_SBAS),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, NMEA, 0x13, FILST_GAL),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, NMEA, 0x15, FILST_QZSS),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, NMEA, 0x16, FILST_GLO),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, NMEA, 0x17, FILST_BDS),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, NMEA, 0x21, FILST_INVFIX),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, NMEA, 0x22, FILST_MSKFIX),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, NMEA, 0x23, FILST_INVTIME),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, NMEA, 0x24, FILST_INVDATE),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, NMEA, 0x25, FILST_ONLYGPS),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, NMEA, 0x26, FILST_FROZENCOG),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, NMEA, 0x31, MAINTALKERID),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, NMEA, 0x32, GSVTALKERID),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_2BYTE, NMEA, 0x33, BDSTALKERID),
};

enum ubx_cfg_key_nmea_svnumbering {
	UBX_CFG_NMEA_SVNUMBERING_STRICT = 0,
	UBX_CFG_NMEA_SVNUMBERING_EXTENDED = 1,
};

enum ubx_cfg_key_nmea_maintalkerid {
	UBX_CFG_NMEA_MAINTALKERID_AUTO = 0,
	UBX_CFG_NMEA_MAINTALKERID_GP = 1,
	UBX_CFG_NMEA_MAINTALKERID_GL = 2,
	UBX_CFG_NMEA_MAINTALKERID_GN = 3,
	UBX_CFG_NMEA_MAINTALKERID_GA = 4,
	UBX_CFG_NMEA_MAINTALKERID_GB = 5,
	UBX_CFG_NMEA_MAINTALKERID_GQ = 7,
};

enum ubx_cfg_key_nmea_gsvtalkerid {
	UBX_CFG_NMEA_GSVTALKERID_GNSS = 0,
	UBX_CFG_NMEA_GSVTALKERID_MAIN = 1,
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_ODO
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_odo {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, ODO, 0x01, USE_ODO),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, ODO, 0x02, USE_COG),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, ODO, 0x03, OUTLPVEL),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, ODO, 0x04, OUTLPCOG),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, ODO, 0x05, PROFILE),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, ODO, 0x21, COGMAXSPEED),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, ODO, 0x22, COGMAXPOSACC),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, ODO, 0x31, VELLPGAIN),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, ODO, 0x32, COGLPGAIN),
};

enum ubx_cfg_key_odo_profile {
	UBX_CFG_ODO_PROFILE_RUN = 0,
	UBX_CFG_ODO_PROFILE_CYCL = 1,
	UBX_CFG_ODO_PROFILE_SWIM = 2,
	UBX_CFG_ODO_PROFILE_CAR = 3,
	UBX_CFG_ODO_PROFILE_CUSTOM = 4,
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_PM
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_pm {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, PM, 0x01, OPERATEMODE),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_4BYTE, PM, 0x02, POSUPDATEPERIOD),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_4BYTE, PM, 0x03, ACQPERIOD),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_4BYTE, PM, 0x04, GRIDOFFSET),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_2BYTE, PM, 0x05, ONTIME),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, PM, 0x06, MINACQTIME),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, PM, 0x07, MAXACQTIME),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, PM, 0x08, DONOTENTEROFF),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, PM, 0x09, WAITTIMEFIX),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, PM, 0x0a, UPDATEEPH),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, PM, 0x0c, EXTINTWAKE),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, PM, 0x0d, EXTINTBACKUP),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, PM, 0x0e, EXTINTINACTIVE),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_4BYTE, PM, 0x0f, EXTINTINACTIVITY),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, PM, 0x10, LIMITPEAKCURR),
};

enum ubx_cfg_key_pm_operatemode {
	/** Normal operation, no power save mode active */
	UBX_CFG_PM_OPERATEMODE_FULL = 0,
	/** PSM ON/OFF operation */
	UBX_CFG_PM_OPERATEMODE_PSMOO = 1,
	/** PSM cyclic tracking operation */
	UBX_CFG_PM_OPERATEMODE_PSMCT = 2,
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_QZSS
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_qzss {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, QZSS, 0x05, USE_SLAS_DGNSS),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, QZSS, 0x06, USE_SLAS_TESTMODE),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, QZSS, 0x07, USE_SLAS_RAIM_),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_2BYTE, QZSS, 0x08, SLAS_MAX_BASELINE),
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_RATE
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_rate {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_2BYTE, RATE, 1, MEAS),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_2BYTE, RATE, 2, NAV),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, RATE, 3, TIMEREF),
};

enum ubx_cfg_key_rate_timeref {
	UBX_CFG_RATE_TIMEREF_UTC = 0,
	UBX_CFG_RATE_TIMEREF_GPS = 1,
	UBX_CFG_RATE_TIMEREF_GLO = 2,
	UBX_CFG_RATE_TIMEREF_BDS = 3,
	UBX_CFG_RATE_TIMEREF_GAL = 4,
	UBX_CFG_RATE_TIMEREF_NAVIC = 5,
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_RINV
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_rinv {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, RINV, 0x01, DUMP),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, RINV, 0x02, BINARY),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, RINV, 0x03, DATA_SIZE),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_8BYTE, RINV, 0x04, CHUNK0),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_8BYTE, RINV, 0x05, CHUNK1),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_8BYTE, RINV, 0x06, CHUNK2),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_8BYTE, RINV, 0x07, CHUNK3),
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_SBAS
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_sbas {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, SBAS, 0x02, USE_TESTMODE),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, SBAS, 0x03, USE_RANGING),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, SBAS, 0x04, USE_DIFFCORR),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, SBAS, 0x05, USE_INTEGRITY),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_8BYTE, SBAS, 0x06, PRNSCANMASK),
};

enum ubx_cfg_key_sbas_prnscanmask {
	UBX_CFG_SBAS_PRNSCANMASK_ALL = 0x00,
	UBX_CFG_SBAS_PRNSCANMASK_PRN120 = BIT64(0),
	UBX_CFG_SBAS_PRNSCANMASK_PRN121 = BIT64(1),
	UBX_CFG_SBAS_PRNSCANMASK_PRN122 = BIT64(2),
	UBX_CFG_SBAS_PRNSCANMASK_PRN123 = BIT64(3),
	UBX_CFG_SBAS_PRNSCANMASK_PRN124 = BIT64(4),
	UBX_CFG_SBAS_PRNSCANMASK_PRN125 = BIT64(5),
	UBX_CFG_SBAS_PRNSCANMASK_PRN126 = BIT64(6),
	UBX_CFG_SBAS_PRNSCANMASK_PRN127 = BIT64(7),
	UBX_CFG_SBAS_PRNSCANMASK_PRN128 = BIT64(8),
	UBX_CFG_SBAS_PRNSCANMASK_PRN129 = BIT64(9),
	UBX_CFG_SBAS_PRNSCANMASK_PRN130 = BIT64(10),
	UBX_CFG_SBAS_PRNSCANMASK_PRN131 = BIT64(11),
	UBX_CFG_SBAS_PRNSCANMASK_PRN132 = BIT64(12),
	UBX_CFG_SBAS_PRNSCANMASK_PRN133 = BIT64(13),
	UBX_CFG_SBAS_PRNSCANMASK_PRN134 = BIT64(14),
	UBX_CFG_SBAS_PRNSCANMASK_PRN135 = BIT64(15),
	UBX_CFG_SBAS_PRNSCANMASK_PRN136 = BIT64(16),
	UBX_CFG_SBAS_PRNSCANMASK_PRN137 = BIT64(17),
	UBX_CFG_SBAS_PRNSCANMASK_PRN138 = BIT64(18),
	UBX_CFG_SBAS_PRNSCANMASK_PRN139 = BIT64(19),
	UBX_CFG_SBAS_PRNSCANMASK_PRN140 = BIT64(20),
	UBX_CFG_SBAS_PRNSCANMASK_PRN141 = BIT64(21),
	UBX_CFG_SBAS_PRNSCANMASK_PRN142 = BIT64(22),
	UBX_CFG_SBAS_PRNSCANMASK_PRN143 = BIT64(23),
	UBX_CFG_SBAS_PRNSCANMASK_PRN144 = BIT64(24),
	UBX_CFG_SBAS_PRNSCANMASK_PRN145 = BIT64(25),
	UBX_CFG_SBAS_PRNSCANMASK_PRN146 = BIT64(26),
	UBX_CFG_SBAS_PRNSCANMASK_PRN147 = BIT64(27),
	UBX_CFG_SBAS_PRNSCANMASK_PRN148 = BIT64(28),
	UBX_CFG_SBAS_PRNSCANMASK_PRN149 = BIT64(29),
	UBX_CFG_SBAS_PRNSCANMASK_PRN150 = BIT64(30),
	UBX_CFG_SBAS_PRNSCANMASK_PRN151 = BIT64(31),
	UBX_CFG_SBAS_PRNSCANMASK_PRN152 = BIT64(32),
	UBX_CFG_SBAS_PRNSCANMASK_PRN153 = BIT64(33),
	UBX_CFG_SBAS_PRNSCANMASK_PRN154 = BIT64(34),
	UBX_CFG_SBAS_PRNSCANMASK_PRN155 = BIT64(35),
	UBX_CFG_SBAS_PRNSCANMASK_PRN156 = BIT64(36),
	UBX_CFG_SBAS_PRNSCANMASK_PRN157 = BIT64(37),
	UBX_CFG_SBAS_PRNSCANMASK_PRN158 = BIT64(38),
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_SEC
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_sec {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, SEC, 0x09, CFG_LOCK),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_2BYTE, SEC, 0x0a, CFG_LOCK_UNLOCKGRP1),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_2BYTE, SEC, 0x0b, CFG_LOCK_UNLOCKGRP2),
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_SIGNAL
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_signal {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, SIGNAL, 0x1f, GPS_ENA),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, SIGNAL, 0x01, GPS_L1CA_ENA),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, SIGNAL, 0x20, SBAS_ENA),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, SIGNAL, 0x05, SBAS_L1CA_ENA),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, SIGNAL, 0x21, GALILEO_ENA),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, SIGNAL, 0x07, GALILEO_E1_ENA),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, SIGNAL, 0x22, BEIDOU_ENA),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, SIGNAL, 0x0d, BEIDOU_B1I_ENA),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, SIGNAL, 0x0f, BEIDOU_B1C_ENA),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, SIGNAL, 0x24, QZSS_ENA),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, SIGNAL, 0x12, QZSS_L1CA_ENA),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, SIGNAL, 0x14, QZSS_L1S_ENA),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, SIGNAL, 0x25, GLONASS_ENA),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, SIGNAL, 0x18, GLONASS_L1CA_ENA),
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_SPI
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_spi {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, SPI, 0x01, MAXFF),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, SPI, 0x02, CPOLARITY),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, SPI, 0x03, CPHASE),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, SPI, 0x05, EXTENDEDTIMEOUT),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, SPI, 0x06, ENABLED),
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_SPIINPROT
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_spiinprot {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, SPIINPROT, 0x01, UBX),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, SPIINPROT, 0x02, NMEA),
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_SPIOUTPROT
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_spioutprot {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, SPIOUTPROT, 0x01, UBX),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, SPIOUTPROT, 0x02, NMEA),
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_TP
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_tp {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, TP, 0x23, PULSE_DEF),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, TP, 0x30, PULSE_LENGTH_DEF),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_2BYTE, TP, 0x01, CABLEDELAY),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_4BYTE, TP, 0x02, PERIOD_TP1),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_4BYTE, TP, 0x03, PERIOD_LOCK_TP1),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_4BYTE, TP, 0x24, FREQ_TP1),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_4BYTE, TP, 0x25, FREQ_LOCK_TP1),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_4BYTE, TP, 0x04, LEN_TP1),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_4BYTE, TP, 0x05, LEN_LOCK_TP1),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_8BYTE, TP, 0x2a, DUTY_TP1),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_8BYTE, TP, 0x2b, DUTY_LOCK_TP1),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_4BYTE, TP, 0x06, USER_DELAY_TP1),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, TP, 0x07, TP1_ENA),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, TP, 0x08, SYNC_GNSS_TP1),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, TP, 0x09, USE_LOCKED_TP1),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, TP, 0x0a, ALIGN_TO_TOW_TP1),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, TP, 0x0b, POL_TP1),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, TP, 0x0c, TIMEGRID_TP1),
};

enum ubx_cfg_key_tp_pulse_def {
	UBX_CFG_TP_PULSE_DEF_PERIOD = 0,
	UBX_CFG_TP_PULSE_DEF_FREQ = 1,
};

enum ubx_cfg_key_tp_pulse_length_def {
	UBX_CFG_TP_PULSE_LENGTH_DEF_RATIO = 0,
	UBX_CFG_TP_PULSE_LENGTH_DEF_LENGTH = 1,
};

enum ubx_cfg_key_tp_pol_tp1 {
	UBX_CFG_TP_POL_TP1_FALLING_EDGE = 0,
	UBX_CFG_TP_POL_TP1_RISING_EDGE = 1,
};

enum ubx_cfg_key_tp_timegrid_tp1 {
	UBX_CFG_TP_TIMEGRID_TP1_UTC = 0,
	UBX_CFG_TP_TIMEGRID_TP1_GPS = 1,
	UBX_CFG_TP_TIMEGRID_TP1_GLO = 2,
	UBX_CFG_TP_TIMEGRID_TP1_BDS = 3,
	UBX_CFG_TP_TIMEGRID_TP1_GAL = 4,
	UBX_CFG_TP_TIMEGRID_TP1_NAVIC = 5,
	UBX_CFG_TP_TIMEGRID_TP1_LOCAL = 15,
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_TXREADY
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_txready {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, TXREADY, 1, ENABLED),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, TXREADY, 2, POLARITY),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, TXREADY, 3, PIN),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_2BYTE, TXREADY, 4, THRESHOLD),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, TXREADY, 5, INTERFACE),
};

enum ubx_cfg_key_rate_txready_polarity {
	UBX_CFG_TXREADY_POLARITY_ACTIVE_HIGH = false,
	UBX_CFG_TXREADY_POLARITY_ACTIVE_LOW = true,
};

enum ubx_cfg_key_rate_txready_interface {
	UBX_CFG_TXREADY_INTERFACE_I2C = 0,
	UBX_CFG_TXREADY_INTERFACE_SPI = 1,
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_UART1
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_uart1 {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_4BYTE, UART1, 1, BAUDRATE),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, UART1, 2, STOPBITS),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, UART1, 3, DATABITS),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BYTE, UART1, 4, PARITY),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, UART1, 5, ENABLED),
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_UART1INPROT
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_uart1inprot {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, UART1INPROT, 0x01, UBX),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, UART1INPROT, 0x02, NMEA),
};

/**
 * @}
 */

/**
 * @addtogroup UBX_CFG_KEY_UART1OUTPROT
 * @ingroup ubx_cfg
 * @{
 */

enum ubx_cfg_key_uart1outprot {
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, UART1OUTPROT, 0x01, UBX),
	_UBX_CFG_KEY_DEFINE(_UBX_CFG_KEY_SIZE_1BIT_, UART1OUTPROT, 0x02, NMEA),
};

/**
 * @}
 */

/**
 * @brief Helper to prepare the common CFG-VALSET message
 *
 * @param buf Buffer to prepare
 * @param layers Layers to set configuration values on
 */
static inline void ubx_msg_prepare_valset(struct net_buf_simple *buf, uint8_t layers)
{
	struct ubx_msg_cfg_valset_v0 *valset;

	ubx_msg_prepare(buf, UBX_MSG_CLASS_CFG, UBX_MSG_ID_CFG_VALSET);
	valset = net_buf_simple_add(buf, sizeof(*valset));
	valset->version = 0x00;
	valset->layers = layers;
}

/**
 * @brief Helper to prepare the common CFG-VALGET message
 *
 * @param buf Buffer to prepare
 * @param layer Layer to query configuration for
 * @param offset Start value offset for query
 */
static inline void ubx_msg_prepare_valget(struct net_buf_simple *buf, uint8_t layer, uint8_t offset)
{
	struct ubx_msg_cfg_valget_query *valget;

	ubx_msg_prepare(buf, UBX_MSG_CLASS_CFG, UBX_MSG_ID_CFG_VALGET);
	valget = net_buf_simple_add(buf, sizeof(*valget));
	valget->version = 0x00;
	valget->layer = layer;
	valget->position = offset;
}

/**
 * @brief Macro to append a configuration value to a buffer
 *
 * Implemented as a macro to allow the type of @a value to be preserved.
 * As @a key should always be a compile time constant, only the chosen branch should
 * ever be present in the output binary.
 *
 * @param buf Buffer to append value to
 * @param key UBX configuration value key
 * @param value Value
 */
#define UBX_CFG_VALUE_APPEND(buf, key, value)                                                      \
	net_buf_simple_add_le32(buf, key);                                                         \
	switch (key & _UBX_CFG_KEY_SIZE_MASK) {                                                    \
	case _UBX_CFG_KEY_SIZE_1BIT_:                                                              \
		net_buf_simple_add_u8(buf, value ? 0x01 : 0x00);                                   \
		break;                                                                             \
	case _UBX_CFG_KEY_SIZE_1BYTE:                                                              \
		net_buf_simple_add_u8(buf, value);                                                 \
		break;                                                                             \
	case _UBX_CFG_KEY_SIZE_2BYTE:                                                              \
		net_buf_simple_add_le16(buf, value);                                               \
		break;                                                                             \
	case _UBX_CFG_KEY_SIZE_4BYTE:                                                              \
		net_buf_simple_add_le32(buf, value);                                               \
		break;                                                                             \
	default:                                                                                   \
		net_buf_simple_add_le64(buf, value);                                               \
		break;                                                                             \
	}

/** Configuration value structure as returned by parser */
struct ubx_cfg_val {
	/** Configuration key ID */
	uint32_t key;
	/** Configuration key value */
	union {
		uint8_t l;
		uint8_t u1;
		uint8_t e1;
		uint8_t x1;
		int8_t i1;
		uint16_t u2;
		uint16_t e2;
		uint16_t x2;
		int16_t i2;
		uint32_t u4;
		uint32_t e4;
		uint32_t x4;
		int32_t i4;
		float r4;
		uint64_t u8;
		uint64_t e8;
		uint64_t x8;
		int64_t i8;
		double r8;
	} val;
};

/**
 * @brief Iteratively parse configuration values from a buffer
 *
 * Expected usage:
 * @code{.c}
 * struct ubx_cfg_val cfg_val;
 * uint8_t *val_ptr = &buffer;
 * uint16_t val_len = buffer_len;
 *
 * while (ubx_cfg_val_parse(&val_ptr, &val_len, &cfg_val) == 0) {
 *     Handle cfg_val here...
 * }
 * @endcode
 *
 * @param ptr Pointer to configuration buffer to parse
 * @param remaining
 * @param cfg
 * @return int
 */
static inline int ubx_cfg_val_parse(const uint8_t **ptr, size_t *remaining, struct ubx_cfg_val *cfg)
{
	if (*remaining <= 4) {
		return -ENOMEM;
	}
	cfg->key = sys_get_le32(*ptr);
	*ptr += 4;
	switch (cfg->key & _UBX_CFG_KEY_SIZE_MASK) {
	case _UBX_CFG_KEY_SIZE_1BIT_:
	case _UBX_CFG_KEY_SIZE_1BYTE:
		cfg->val.u1 = **ptr;
		*ptr += 1;
		*remaining -= 1;
		break;
	case _UBX_CFG_KEY_SIZE_2BYTE:
		cfg->val.u2 = sys_get_le16(*ptr);
		*ptr += 2;
		*remaining -= 2;
		break;
	case _UBX_CFG_KEY_SIZE_4BYTE:
		cfg->val.u4 = sys_get_le32(*ptr);
		*ptr += 4;
		*remaining -= 4;
		break;
	case _UBX_CFG_KEY_SIZE_8BYTE:
		cfg->val.u8 = sys_get_le64(*ptr);
		*ptr += 8;
		*remaining -= 8;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_GNSS_UBX_CFG_H_ */
