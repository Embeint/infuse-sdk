/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * UBX protocol message definitions.
 */

#ifndef INFUSE_GNSS_UBX_PROTOCOL_H_
#define INFUSE_GNSS_UBX_PROTOCOL_H_

#include <zephyr/net_buf.h>
#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup ubx_message_protocol UBX Message Protocol
 * @{
 */

#define UBX_PREAMBLE_SYNC_CHAR_1 0xB5
#define UBX_PREAMBLE_SYNC_CHAR_2 0x62

/**
 * @brief UBX Message Frame
 */
struct ubx_frame {
	uint8_t preamble_sync_char_1;
	uint8_t preamble_sync_char_2;
	uint8_t message_class;
	uint8_t message_id;
	uint8_t payload_size_low;
	uint8_t payload_size_high;
	uint8_t payload_and_checksum[];
};

/**
 * @brief UBX Message Classes
 */
enum ubx_msg_class {
	UBX_MSG_CLASS_ACK = 0x05,
	UBX_MSG_CLASS_CFG = 0x06,
	UBX_MSG_CLASS_INF = 0x04,
	UBX_MSG_CLASS_LOG = 0x21,
	UBX_MSG_CLASS_MGA = 0x13,
	UBX_MSG_CLASS_MON = 0x0a,
	UBX_MSG_CLASS_NAV = 0x01,
	UBX_MSG_CLASS_RXM = 0x02,
	UBX_MSG_CLASS_SEC = 0x27,
	UBX_MSG_CLASS_TIM = 0x0d,
	UBX_MSG_CLASS_UPD = 0x09,
};

/**
 * @addtogroup UBX_MSG_CLASS_ACK
 * @ingroup ubx_message_protocol
 * @{
 */

enum ubx_msg_id_ack {
	UBX_MSG_ID_ACK_NAK = 0x00,
	UBX_MSG_ID_ACK_ACK = 0x01,
};

/** @ref UBX_MSG_ID_ACK_ACK */
struct ubx_msg_id_ack_ack {
	uint8_t message_class;
	uint8_t message_id;
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_id_ack_ack) == 2);

/** @ref UBX_MSG_ID_ACK_NAK */
struct ubx_msg_id_ack_nak {
	uint8_t message_class;
	uint8_t message_id;
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_id_ack_nak) == 2);

/**
 * @}
 */

/**
 * @addtogroup UBX_MSG_CLASS_CFG
 * @ingroup ubx_message_protocol
 * @{
 */

enum ubx_msg_id_cfg {
	UBX_MSG_ID_CFG_PRT = 0x00,
	UBX_MSG_ID_CFG_MSG = 0x01,
	UBX_MSG_ID_CFG_RST = 0x04,
	UBX_MSG_ID_CFG_RATE = 0x08,
	UBX_MSG_ID_CFG_CFG = 0x09,
	UBX_MSG_ID_CFG_NAV5 = 0x24,
	UBX_MSG_ID_CFG_GNSS = 0x3E,
	UBX_MSG_ID_CFG_DCDC_BURN = 0x41,
	UBX_MSG_ID_CFG_VALSET = 0x8a,
	UBX_MSG_ID_CFG_VALGET = 0x8b,
	UBX_MSG_ID_CFG_VALDEL = 0x8c,
};

/** @ref UBX_MSG_ID_CFG_PRT (SPI) */
struct ubx_msg_cfg_prt_spi {
	uint8_t port_id;
	uint8_t reserved1;
	uint16_t tx_ready;
	uint32_t mode;
	uint8_t reserved2[4];
	uint16_t in_proto_mask;
	uint16_t out_proto_mask;
	uint16_t flags;
	uint8_t reserved3[2];
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_cfg_prt_spi) == 20);

enum ubx_msg_cfg_prt_ports {
	UBX_MSG_CFG_PRT_PORT_ID_I2C = 0,
	UBX_MSG_CFG_PRT_PORT_ID_USB = 3,
	UBX_MSG_CFG_PRT_PORT_ID_SPI = 4,
};

enum ubx_msg_cfg_prt_proto_mask {
	UBX_MSG_CFG_PRT_PROTO_MASK_UBX = BIT(0),
	UBX_MSG_CFG_PRT_PROTO_MASK_NMEA = BIT(1),
	UBX_MSG_CFG_PRT_PROTO_MASK_RTCM2 = BIT(2),
	UBX_MSG_CFG_PRT_PROTO_MASK_RTCM3 = BIT(5),
};

enum ubx_msg_cfg_prt_tx_ready {
	UBX_MSG_CFG_PRT_TX_READY_EN = BIT(0),
	UBX_MSG_CFG_PRT_TX_READY_POL_ACTIVE_HIGH = 0,
	UBX_MSG_CFG_PRT_TX_READY_POL_ACTIVE_LOW = BIT(1),
};

#define UBX_MSG_CFG_PRT_TX_READY_CFG(pio, threshold) (pio << 2) | ((threshold / 8) << 7)

enum ubx_msg_cfg_prt_spi_mode {
	/* CPOL = 0, CPHA = 0 */
	UBX_MSG_CFG_PRT_SPI_MODE_0 = (0 << 1),
	/* CPOL = 0, CPHA = 1 */
	UBX_MSG_CFG_PRT_SPI_MODE_1 = (1 << 1),
	/* CPOL = 1, CPHA = 0 */
	UBX_MSG_CFG_PRT_SPI_MODE_2 = (2 << 1),
	/* CPOL = 1, CPHA = 1 */
	UBX_MSG_CFG_PRT_SPI_MODE_3 = (3 << 1),
};

enum ubx_msg_cfg_prt_spi_flags {
	UBX_MSG_CFG_PRT_SPI_EXTENDED_TIMEOUT = BIT(1),
};

/** @ref UBX_MSG_ID_CFG_MSG */
struct ubx_msg_cfg_msg {
	uint8_t msg_class;
	uint8_t msg_id;
	uint8_t rate;
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_cfg_msg) == 3);

/** @ref UBX_MSG_ID_CFG_RATE */
struct ubx_msg_cfg_rate {
	uint16_t meas_rate;
	uint16_t nav_rate;
	uint16_t time_ref;
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_cfg_rate) == 6);

enum ubx_msg_cfg_rate_time_ref {
	UBX_MSG_CFG_RATE_TIME_REF_UTC = 0,
	UBX_MSG_CFG_RATE_TIME_REF_GPS = 1,
	UBX_MSG_CFG_RATE_TIME_REF_GLONASS = 2,
	UBX_MSG_CFG_RATE_TIME_REF_BEIDOU = 3,
	UBX_MSG_CFG_RATE_TIME_REF_GALILEO = 4,
	UBX_MSG_CFG_RATE_TIME_REF_NAVIC = 5,
};

/** @ref UBX_MSG_ID_CFG_RST */
struct ubx_msg_cfg_rst {
	uint16_t nav_bbr_mask;
	uint8_t reset_mode;
	uint8_t reserved1;
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_cfg_rst) == 4);

enum ubx_msg_cfg_rst_bbr_mask {
	UBX_MSG_CFG_RST_BBR_HOT_START = 0x0000,
	UBX_MSG_CFG_RST_BBR_WARM_START = 0x0001,
	UBX_MSG_CFG_RST_BBR_COLD_START = 0xFFFF,
};

enum ubx_msg_cfg_rst_mode {
	UBX_MSG_CFG_RST_MODE_HARDWARE = 0x0000,
};

/** @ref UBX_MSG_ID_CFG_CFG */
struct ubx_msg_cfg_cfg_m8 {
	uint32_t clear_mask;
	uint32_t save_mask;
	uint32_t load_mask;
	uint8_t device_mask;
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_cfg_cfg_m8) == 13);

/** @ref UBX_MSG_ID_CFG_CFG */
struct ubx_msg_cfg_cfg_m10 {
	uint32_t clear_mask;
	uint32_t save_mask;
	uint32_t load_mask;
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_cfg_cfg_m10) == 12);

enum ubx_msg_cfg_cfg_mask {
	UBX_MSG_CFG_CFG_MASK_IO_PORT = BIT(0),
	UBX_MSG_CFG_CFG_MASK_MSG_CONF = BIT(1),
	UBX_MSG_CFG_CFG_MASK_INF_MSG = BIT(2),
	UBX_MSG_CFG_CFG_MASK_NAV_CONF = BIT(3),
	UBX_MSG_CFG_CFG_MASK_RXM_CONF = BIT(4),
	UBX_MSG_CFG_CFG_MASK_SEN_CONF = BIT(8),
	UBX_MSG_CFG_CFG_MASK_RINV_CONF = BIT(9),
	UBX_MSG_CFG_CFG_MASK_ANT_CONF = BIT(10),
	UBX_MSG_CFG_CFG_MASK_LOG_CONF = BIT(11),
	UBX_MSG_CFG_CFG_MASK_FTS_CONF = BIT(12),
};

enum ubx_msg_cfg_cfg_device {
	UBX_MSG_CFG_CFG_DEVICE_BBR = BIT(0),
	UBX_MSG_CFG_CFG_DEVICE_FLASH = BIT(1),
	UBX_MSG_CFG_CFG_DEVICE_EEPROM = BIT(2),
	UBX_MSG_CFG_CFG_DEVICE_SPI_FLASH = BIT(4),
};

/** @ref UBX_MSG_ID_CFG_NAV5 */
struct ubx_msg_cfg_nav5 {
	uint16_t mask;
	uint8_t dyn_model;
	uint8_t fix_mode;
	int32_t fixed_alt;
	uint32_t fixed_alt_var;
	int8_t min_elev;
	uint8_t dr_limit;
	uint16_t p_dop;
	uint16_t t_dop;
	uint16_t p_acc;
	uint16_t t_acc;
	uint8_t static_hold_threshold;
	uint8_t d_gnss_timeout;
	uint8_t cno_threshold_num_svs;
	uint8_t cno_threshold;
	uint8_t reserved1[2];
	uint16_t static_hold_max_dist;
	uint8_t utc_standard;
	uint8_t reserved2[5];
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_cfg_nav5) == 36);

/** @ref UBX_MSG_ID_CFG_GNSS */
struct ubx_msg_cfg_gnss {
	uint8_t msg_ver;
	uint8_t num_trk_ch_hw;
	uint8_t num_trk_ch_use;
	uint8_t num_cfg_blocks;
	struct ubx_msg_cfg_gnss_cfg {
		uint8_t gnss_id;
		uint8_t res_trk_chan;
		uint8_t max_trk_chan;
		uint8_t reserved1;
		uint32_t flags;
	} __packed configs[];
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_cfg_gnss) == 4);

enum ubx_msg_cfg_gnss_flags {
	UBX_MSG_CFG_GNSS_FLAGS_ENABLED = BIT(0),
};

/** @ref UBX_MSG_ID_CFG_VALSET */
struct ubx_msg_cfg_valset_v0 {
	uint8_t version;
	uint8_t layers;
	uint8_t reserved[2];
	uint8_t cfg_data[];
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_cfg_valset_v0) == 4);

/** @ref UBX_MSG_ID_CFG_VALSET */
struct ubx_msg_cfg_valset_v1 {
	uint8_t version;
	uint8_t layers;
	uint8_t transaction;
	uint8_t reserved;
	uint8_t cfg_data[];
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_cfg_valset_v1) == 4);

enum ubx_msg_cfg_valset_layers {
	UBX_MSG_CFG_VALSET_LAYERS_RAM = BIT(0),
	UBX_MSG_CFG_VALSET_LAYERS_BBR = BIT(1),
	UBX_MSG_CFG_VALSET_LAYERS_FLASH = BIT(2),
};

enum ubx_msg_cfg_valset_transaction {
	UBX_MSG_CFG_VALSET_TRANSACTION_NONE = 0,
	UBX_MSG_CFG_VALSET_TRANSACTION_START = 1,
	UBX_MSG_CFG_VALSET_TRANSACTION_ONGOING = 2,
	UBX_MSG_CFG_VALSET_TRANSACTION_APPLY = 3,
};

/** Query for @ref UBX_MSG_ID_CFG_VALGET */
struct ubx_msg_cfg_valget_query {
	uint8_t version;
	uint8_t layer;
	uint16_t position;
	uint32_t cfg_keys[];
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_cfg_valget_query) == 4);

/** Response to @ref UBX_MSG_ID_CFG_VALGET */
struct ubx_msg_cfg_valget_response {
	uint8_t version;
	uint8_t layer;
	uint16_t position;
	uint8_t cfg_data[];
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_cfg_valget_response) == 4);

enum ubx_msg_cfg_valget_layer {
	UBX_MSG_CFG_VALGET_LAYER_RAM = 0,
	UBX_MSG_CFG_VALGET_LAYER_BBR = 1,
	UBX_MSG_CFG_VALGET_LAYER_FLASH = 2,
	UBX_MSG_CFG_VALGET_LAYER_DEFAULT = 7,
};

/**
 * @}
 */

/**
 * @addtogroup UBX_MSG_CLASS_MON
 * @ingroup ubx_message_protocol
 * @{
 */

enum ubx_msg_id_mon {
	UBX_MSG_ID_MON_BATCH = 0x32,
	UBX_MSG_ID_MON_COMMS = 0x36,
	UBX_MSG_ID_MON_GNSS = 0x28,
	UBX_MSG_ID_MON_HW3 = 0x37,
	UBX_MSG_ID_MON_HW = 0x09,
	UBX_MSG_ID_MON_PATCH = 0x27,
	UBX_MSG_ID_MON_RF = 0x38,
	UBX_MSG_ID_MON_RXR = 0x21,
	UBX_MSG_ID_MON_SPAN = 0x31,
	UBX_MSG_ID_MON_VER = 0x04,
};

/** @ref UBX_MSG_ID_MON_HW3 */
struct ubx_msg_mon_hw3 {
	uint8_t version;
	uint8_t n_pins;
	uint8_t flags;
	char hw_version[10];
	uint8_t reserved0[9];
	struct {
		uint8_t reserved1;
		uint8_t pin_id;
		uint16_t pin_mask;
		uint8_t vp;
		uint8_t reserved2;
	} pins[];
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_mon_hw3) == 22);

enum ubx_msg_mon_hw3_flags {
	UBX_MSG_MON_HW3_FLAGS_RTC_CALIB = BIT(0),
	UBX_MSG_MON_HW3_FLAGS_SAFE_BOOT = BIT(1),
	UBX_MSG_MON_HW3_FLAGS_XTAL_ABSENT = BIT(2),
};

enum ubx_msg_mon_hw3_pin_mask {
	UBX_MSG_MON_HW3_PIN_MASK_PIO = BIT(0),
	UBX_MSG_MON_HW3_PIN_MASK_BANK_MASK = (0x07 << 1),
	UBX_MSG_MON_HW3_PIN_MASK_DIR_OUT = BIT(4),
	UBX_MSG_MON_HW3_PIN_MASK_VALUE = BIT(5),
	UBX_MSG_MON_HW3_PIN_MASK_VIRTUAL = BIT(6),
	UBX_MSG_MON_HW3_PIN_MASK_INT_EN = BIT(7),
	UBX_MSG_MON_HW3_PIN_MASK_PULL_UP = BIT(8),
	UBX_MSG_MON_HW3_PIN_MASK_PULL_DOWN = BIT(9),
};

/** @ref UBX_MSG_ID_MON_HW */
struct ubx_msg_mon_hw {
	uint32_t pin_sel;
	uint32_t pin_bank;
	uint32_t pin_dir;
	uint32_t pin_val;
	uint16_t noise_per_ms;
	uint16_t adc_cnt;
	uint8_t ant_state;
	uint8_t ant_power;
	uint8_t flags;
	uint8_t reserved1;
	uint32_t used_mask;
	uint8_t vp[17];
	uint8_t cw_supression;
	uint8_t reserved2[2];
	uint32_t pin_irq;
	uint32_t pull_high;
	uint32_t pull_low;
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_mon_hw) == 60);

/** @ref UBX_MSG_ID_MON_RXR */
struct ubx_msg_mon_rxr {
	uint8_t flags;
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_mon_rxr) == 1);

enum ubx_msg_mon_rxr_flags {
	UBX_MSG_MON_RXR_AWAKE = BIT(0),
};

/** @ref UBX_MSG_ID_MON_VER */
struct ubx_msg_mon_ver {
	/** Null-terminated software version string */
	char sw_version[30];
	/** Null-terminated hardware version string */
	char hw_version[10];
	/** Extended software information strings */
	struct {
		char ext_version[30];
	} extension[];
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_mon_ver) == 40);

/**
 * @}
 */

/**
 * @addtogroup UBX_MSG_CLASS_NAV
 * @ingroup ubx_message_protocol
 * @{
 */

enum ubx_msg_id_nav {
	UBX_MSG_ID_NAV_AOPSTATUS = 0x60,
	UBX_MSG_ID_NAV_CLOCK = 0x22,
	UBX_MSG_ID_NAV_COV = 0x36,
	UBX_MSG_ID_NAV_DOP = 0x04,
	UBX_MSG_ID_NAV_EOE = 0x61,
	UBX_MSG_ID_NAV_ODO = 0x09,
	UBX_MSG_ID_NAV_ORB = 0x34,
	UBX_MSG_ID_NAV_PL = 0x64,
	UBX_MSG_ID_NAV_POSECEF = 0x01,
	UBX_MSG_ID_NAV_POSLLH = 0x02,
	UBX_MSG_ID_NAV_PVT = 0x07,
	UBX_MSG_ID_NAV_RESETODO = 0x10,
	UBX_MSG_ID_NAV_SAT = 0x35,
	UBX_MSG_ID_NAV_SBAS = 0x32,
	UBX_MSG_ID_NAV_SIG = 0x43,
	UBX_MSG_ID_NAV_SLAS = 0x42,
	UBX_MSG_ID_NAV_STATUS = 0x03,
	UBX_MSG_ID_NAV_TIMEBDS = 0x24,
	UBX_MSG_ID_NAV_TIMEGAL = 0x25,
	UBX_MSG_ID_NAV_TIMEGLO = 0x23,
	UBX_MSG_ID_NAV_TIMEGPS = 0x20,
	UBX_MSG_ID_NAV_TIMELS = 0x26,
	UBX_MSG_ID_NAV_TIMEQZSS = 0x27,
	UBX_MSG_ID_NAV_TIMEUTC = 0x21,
	UBX_MSG_ID_NAV_VELECEF = 0x11,
	UBX_MSG_ID_NAV_VELNED = 0x10,
};

/** @ref UBX_MSG_ID_NAV_DOP */
struct ubx_msg_nav_dop {
	/** GPS time of week of the navigation epoch */
	uint32_t itow;
	/** Geometric DOP */
	uint16_t g_dop;
	/** Position DOP */
	uint16_t p_dop;
	/** Time DOP */
	uint16_t t_dop;
	/** Vertical DOP */
	uint16_t v_dop;
	/** Horizontal DOP */
	uint16_t h_dop;
	/** Northing DOP */
	uint16_t n_dop;
	/** Easting DOP */
	uint16_t e_dop;
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_nav_dop) == 18);

/** @ref UBX_MSG_ID_NAV_PVT */
struct ubx_msg_nav_pvt {
	/** GPS time of week of the navigation epoch */
	uint32_t itow;
	/** Year (UTC) */
	uint16_t year;
	/** Month, range 1..12 (UTC */
	uint8_t month;
	/** Day of month, range 1..31 (UTC) */
	uint8_t day;
	/** Hour of day, range 0..23 (UTC) */
	uint8_t hour;
	/** Minute of hour, range 0..59 (UTC) */
	uint8_t min;
	/** Seconds of minute, range 0..60 (UTC) */
	uint8_t sec;
	/** Validity flags */
	uint8_t valid;
	/** Time accuracy estimate (UTC) */
	uint32_t t_acc;
	/** Fraction of second, range -1e9 .. 1e9 (UTC) */
	int32_t nano;
	/** GNSSfix Type */
	uint8_t fix_type;
	/** Fix status flags */
	uint8_t flags;
	/** Additional flags */
	uint8_t flags2;
	/** Number of satellites used in Nav Solution */
	uint8_t num_sv;
	/** Longitude (1e-7) */
	int32_t lon;
	/** Latitude (1e-7) */
	int32_t lat;
	/** Height above ellipsoid (mm) */
	int32_t height;
	/** Height above mean sea level (mm) */
	int32_t height_msl;
	/** Horizontal accuracy estimate (mm) */
	uint32_t h_acc;
	/** Vertical accuracy estimate (mm) */
	uint32_t v_acc;
	/** NED north velocity (mm/s) */
	int32_t vel_n;
	/** NED east velocity (mm/s) */
	int32_t vel_e;
	/** NED down velocity (mm/s) */
	int32_t vel_d;
	/** 2D Ground Speed  (mm/s) */
	int32_t g_speed;
	/** 2D Heading of motion (1e-5 deg) */
	int32_t head_mot;
	/** Speed accuracy estimate (mm/s) */
	uint32_t s_acc;
	/** Heading accuracy estimate (both motion and vehicle) (1e-5 deg) */
	uint32_t head_acc;
	/** Position DOP (0.01) */
	uint16_t p_dop;
	/** Additional flags */
	uint16_t flags3;
	uint8_t reserved0[4];
	/** 2D Heading of vehicle, this is only valid when headVehValid is set (1e-5 deg) */
	int32_t head_veh;
	/** Magnetic declination. Only supported in ADR 4.10 and later (1e-2 deg) */
	int16_t mag_dec;
	/** Magnetic declination accuracy. Only supported in ADR 4.10 and later (1e-2 deg) */
	uint16_t mag_acc;
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_nav_pvt) == 92);

enum ubx_msg_nav_pvt_valid {
	/** Valid UTC Date */
	UBX_MSG_NAV_PVT_VALID_DATE = BIT(0),
	/** Valid UTC time of day */
	UBX_MSG_NAV_PVT_VALID_TIME = BIT(1),
	/** UTC time of day has been fully resolved (no seconds uncertainty). Cannot be used to
	 * check if time is completely solved
	 */
	UBX_MSG_NAV_PVT_VALID_FULLY_RESOLVED = BIT(2),
	/** Valid magnetic declination */
	UBX_MSG_NAV_PVT_VALID_MAG = BIT(3),
};

enum ubx_msg_nav_pvt_fix_type {
	UBX_MSG_NAV_PVT_FIX_TYPE_NO_FIX = 0,
	UBX_MSG_NAV_PVT_FIX_TYPE_DEAD_RECKONING = 1,
	UBX_MSG_NAV_PVT_FIX_TYPE_2D = 2,
	UBX_MSG_NAV_PVT_FIX_TYPE_3D = 3,
	UBX_MSG_NAV_PVT_FIX_TYPE_GNSS_DEAD_RECKONING = 4,
	UBX_MSG_NAV_PVT_FIX_TYPE_TIME_ONLY = 5,
};

enum ubx_msg_nav_pvt_flags {
	/** Valid fix (i.e within DOP & accuracy masks) */
	UBX_MSG_NAV_PVT_FLAGS_GNSS_FIX_OK = BIT(0),
	/** Differential corrections were applied */
	UBX_MSG_NAV_PVT_FLAGS_DIFF_SOLN = BIT(1),
	/** Power save mode state */
	UBX_MSG_NAV_PVT_FLAGS_PSM_MASK = (0x7 << 2),
	UBX_MSG_NAV_PVT_FLAGS_PSM_DISABLED = (0 << 2),
	UBX_MSG_NAV_PVT_FLAGS_PSM_ENABLED = (1 << 2),
	UBX_MSG_NAV_PVT_FLAGS_PSM_ACQUISITION = (2 << 2),
	UBX_MSG_NAV_PVT_FLAGS_PSM_TRACKING = (3 << 2),
	UBX_MSG_NAV_PVT_FLAGS_PSM_POWER_OPTIMIZED_TRACKING = (4 << 2),
	UBX_MSG_NAV_PVT_FLAGS_PSM_INACTIVE = (5 << 2),
	/** Heading of vehicle is valid, only set if the receiver is in sensor fusion mode */
	UBX_MSG_NAV_PVT_FLAGS_HEAD_VEH_VALID = BIT(5),
	/** Carrier phase range solution status */
	UBX_MSG_NAV_PVT_FLAGS_CARR_SOLN_MASK = (0x3 << 6),
	UBX_MSG_NAV_PVT_FLAGS_CARR_SOLN_NO_SOLN = (0 << 6),
	UBX_MSG_NAV_PVT_FLAGS_CARR_SOLN_FLOATING_ABIGUITIES = (1 << 6),
	UBX_MSG_NAV_PVT_FLAGS_CARR_SOLN_FIXED_ABIGUITIES = (2 << 6),
};

enum ubx_msg_nav_pvt_flags2 {
	/** Information about UTC Date and Time of Day validity confirmation is available */
	UBX_MSG_NAV_PVT_FLAGS2_CONFIRMED_AVAI = BIT(5),
	/** UTC Date validity could be confirmed */
	UBX_MSG_NAV_PVT_FLAGS2_CONFIRMED_DATE = BIT(6),
	/** UTC Time of Day could be confirmed */
	UBX_MSG_NAV_PVT_FLAGS2_CONFIRMED_TIME = BIT(7),
};

enum ubx_msg_nav_pvt_flags3 {
	/** Invalid lon, lat, height and hMS */
	UBX_MSG_NAV_PVT_FLAGS3_INVALID_LLH = BIT(0),
	/** Age of the most recently received differential correction (seconds) */
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_MASK = (0xF << 1),
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_NA = (0 << 1),
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_0_1 = (1 << 1),
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_1_2 = (2 << 1),
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_2_5 = (3 << 1),
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_5_10 = (4 << 1),
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_10_15 = (5 << 1),
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_15_20 = (6 << 1),
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_20_30 = (7 << 1),
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_30_45 = (8 << 1),
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_45_60 = (9 << 1),
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_60_90 = (10 << 1),
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_90_120 = (11 << 1),
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_120_N = (12 << 1),
	/** Output time has been validated against an external trusted time source */
	UBX_MSG_NAV_PVT_FLAGS3_AUTH_TIME = BIT(13),
};

/** @ref UBX_MSG_ID_NAV_SAT */
struct ubx_msg_nav_sat {
	/** GPS time of week of the navigation epoch */
	uint32_t itow;
	/** Message version (0x01 for this version) */
	uint8_t version;
	/** Number of satellites */
	uint8_t num_svs;
	uint8_t reserved0[2];
	struct ubx_msg_nav_sat_sv {
		/** GNSS identifier */
		uint8_t gnss_id;
		/** Satellite identifier */
		uint8_t sv_id;
		/** Carrier to noise ratio (signal strength) (dBHz) */
		uint8_t cno;
		/** Elevation (range: +/-90), unknown if out of range (deg) */
		int8_t elev;
		/** Azimuth (range 0-360), unknown if elevation is out of range (deg) */
		int16_t azim;
		/** Pseudorange residual */
		int16_t pr_res;
		/** Bitmask */
		uint32_t flags;
	} __packed svs[];
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_nav_sat) == 8);

enum ubx_msg_nav_sat_sv_flags {
	/** Signal quality indicator */
	UBX_MSG_NAV_SAT_FLAGS_QUALITY_IND_MASK = 0x7,
	UBX_MSG_NAV_SAT_FLAGS_QUALITY_IND_NO_SIGNAL = 0,
	UBX_MSG_NAV_SAT_FLAGS_QUALITY_IND_SEARCHING = 1,
	UBX_MSG_NAV_SAT_FLAGS_QUALITY_IND_ACQUIRED = 2,
	UBX_MSG_NAV_SAT_FLAGS_QUALITY_IND_UNUSABLE = 3,
	UBX_MSG_NAV_SAT_FLAGS_QUALITY_IND_CODE_LOCKED = 4,
	UBX_MSG_NAV_SAT_FLAGS_QUALITY_IND_CODE_CARRIER_LOCKED1 = 5,
	UBX_MSG_NAV_SAT_FLAGS_QUALITY_IND_CODE_CARRIER_LOCKED2 = 6,
	UBX_MSG_NAV_SAT_FLAGS_QUALITY_IND_CODE_CARRIER_LOCKED3 = 7,
	/** Signal in the subset specified in Signal Identifiers is currently being used for
	 * navigation
	 */
	UBX_MSG_NAV_SAT_FLAGS_SV_USED = BIT(3),
	/** Signal health flag */
	UBX_MSG_NAV_SAT_FLAGS_HEALTH_MASK = (0x3 << 4),
	UBX_MSG_NAV_SAT_FLAGS_HEALTH_UNKNOWN = (0 << 4),
	UBX_MSG_NAV_SAT_FLAGS_HEALTH_HEALTHY = (1 << 4),
	UBX_MSG_NAV_SAT_FLAGS_HEALTH_UNHEALTHY = (2 << 4),
	/** Differential correction data is available for this SV */
	UBX_MSG_NAV_SAT_FLAGS_DIFF_CORR = BIT(6),
	/** Carrier smoothed pseudorange used */
	UBX_MSG_NAV_SAT_FLAGS_SMOOTHED = BIT(7),
	/** Orbit source */
	UBX_MSG_NAV_SAT_FLAGS_ORBIT_SOURCE_MASK = (0x7 << 8),
	UBX_MSG_NAV_SAT_FLAGS_ORBIT_SOURCE_NONE = (0 << 8),
	UBX_MSG_NAV_SAT_FLAGS_ORBIT_SOURCE_EPHEMERIS = (1 << 8),
	UBX_MSG_NAV_SAT_FLAGS_ORBIT_SOURCE_ALMANAC = (2 << 8),
	UBX_MSG_NAV_SAT_FLAGS_ORBIT_SOURCE_ASSIST_NOW_OFFLINE = (3 << 8),
	UBX_MSG_NAV_SAT_FLAGS_ORBIT_SOURCE_ASSIST_NOW_AUTONOMOUS = (4 << 8),
	UBX_MSG_NAV_SAT_FLAGS_ORBIT_SOURCE_OTHER1 = (5 << 8),
	UBX_MSG_NAV_SAT_FLAGS_ORBIT_SOURCE_OTHER2 = (6 << 8),
	UBX_MSG_NAV_SAT_FLAGS_ORBIT_SOURCE_OTHER3 = (7 << 8),
	/** Ephemeris is available for this SV */
	UBX_MSG_NAV_SAT_FLAGS_EPH_AVAIL = BIT(11),
	/** Almanac is available for this SV */
	UBX_MSG_NAV_SAT_FLAGS_ALM_AVAIL = BIT(12),
	/** AssistNow Offline data is available for this SV */
	UBX_MSG_NAV_SAT_FLAGS_ANO_AVAIL = BIT(13),
	/** AssistNow Autonomous data is available for this SV */
	UBX_MSG_NAV_SAT_FLAGS_AOP_AVAIL = BIT(14),
	/** SBAS corrections have been used for this SV */
	UBX_MSG_NAV_SAT_FLAGS_SBAS_CORR_USED = BIT(16),
	/** RTCM corrections have been used for this SV */
	UBX_MSG_NAV_SAT_FLAGS_RTCM_CORR_USED = BIT(17),
	/** QZSS SLAS corrections have been used for this SV */
	UBX_MSG_NAV_SAT_FLAGS_SLAS_CORR_USED = BIT(18),
	/** SPARTN corrections have been used for this SV */
	UBX_MSG_NAV_SAT_FLAGS_SPARTN_CORR_USED = BIT(19),
	/** Pseudorange corrections have been used for this SV */
	UBX_MSG_NAV_SAT_FLAGS_PR_CORR_USED = BIT(20),
	/** Carrier range corrections have been used for this SV */
	UBX_MSG_NAV_SAT_FLAGS_CR_CORR_USED = BIT(21),
	/** Range rate (Doppler) corrections have been used for this SV */
	UBX_MSG_NAV_SAT_FLAGS_DO_CORR_USED = BIT(22),
	/** CLAS corrections have been used for this SV */
	UBX_MSG_NAV_SAT_FLAGS_CLAS_CORR_USED = BIT(23),
};

/** @ref UBX_MSG_ID_NAV_TIMEGPS */
struct ubx_msg_nav_timegps {
	/** GPS time of week of the navigation epoch. */
	uint32_t itow;
	/** Fractional part of iTOW (range: +/-500000) */
	int32_t ftow;
	/** GPS week number of the navigation epoch */
	uint16_t week;
	/** GPS leap seconds (GPS-UTC) */
	int8_t leap_s;
	/** Validity Flags */
	uint8_t valid;
	/** Time Accuracy Estimate */
	uint32_t t_acc;
} __packed;

enum ubx_msg_nav_timegps_valid {
	/** Valid GPS time of week (iTOW & fTOW) */
	UBX_MSG_NAV_TIMEGPS_VALID_TOW_VALID = BIT(0),
	/** Valid GPS week number */
	UBX_MSG_NAV_TIMEGPS_VALID_WEEK_VALID = BIT(1),
	/** Valid GPS leap seconds */
	UBX_MSG_NAV_TIMEGPS_VALID_LEAP_S_VALID = BIT(2),
};

/**
 * @}
 */

/**
 * @addtogroup UBX_MSG_CLASS_RXM
 * @ingroup ubx_message_protocol
 * @{
 */

enum ubx_msg_id_rxm {
	UBX_MSG_ID_RXM_MEAS20 = 0x84,
	UBX_MSG_ID_RXM_MEAS50 = 0x86,
	UBX_MSG_ID_RXM_MEASC12 = 0x82,
	UBX_MSG_ID_RXM_MEASD12 = 0x80,
	UBX_MSG_ID_RXM_MEASX = 0x14,
	UBX_MSG_ID_RXM_PMREQ = 0x41,
	UBX_MSG_ID_RXM_RLM = 0x59,
	UBX_MSG_ID_RXM_SFRBX = 0x13,
};

/** @ref UBX_MSG_ID_RXM_PMREQ */
struct ubx_msg_rxm_pmreq {
	uint8_t version;
	uint8_t reserved0[3];
	uint32_t duration_ms;
	uint32_t flags;
	uint32_t wakeup_sources;
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_rxm_pmreq) == 16);

enum ubx_msg_rxm_pmreq_flags {
	UBX_MSG_RXM_PMREQ_FLAGS_BACKUP = BIT(1),
	UBX_MSG_RXM_PMREQ_FLAGS_FORCE = BIT(2),
};

enum ubx_msg_rxm_pmreq_wakeup {
	UBX_MSG_RXM_PMREQ_WAKEUP_UARTRX = BIT(3),
	UBX_MSG_RXM_PMREQ_WAKEUP_EXTINT0 = BIT(5),
	UBX_MSG_RXM_PMREQ_WAKEUP_EXTINT1 = BIT(6),
	UBX_MSG_RXM_PMREQ_WAKEUP_SPICS = BIT(7),
};

/**
 * @}
 */

/**
 * @brief Create a net_buf_simple large enough to hold a message
 *
 * @warning Only works for fixed length messages
 *
 * @param name Name of net_buf_simple
 * @param msg_type UBX message structure
 */
#define UBX_MSG_BUF_DEFINE(name, msg_type)                                                         \
	NET_BUF_SIMPLE_DEFINE(name,                                                                \
			      (sizeof(struct ubx_frame) + sizeof(msg_type) + sizeof(uint16_t)))

/**
 * @brief Prepare a net_buf_simple to be used as a UBX protocol message
 *
 * @param buf Buffer to prepare.
 * @param msg_class UBX protocol message class (@ref ubx_msg_class)
 * @param msg_id UBX protocol message ID (depends on @a msg_class)
 */
static inline void ubx_msg_prepare(struct net_buf_simple *buf, uint8_t msg_class, uint8_t msg_id)
{
	struct ubx_frame *frame;

	net_buf_simple_reset(buf);
	frame = net_buf_simple_add(buf, sizeof(*frame));

	/* Start of frame header */
	frame->preamble_sync_char_1 = UBX_PREAMBLE_SYNC_CHAR_1;
	frame->preamble_sync_char_2 = UBX_PREAMBLE_SYNC_CHAR_2;
	frame->message_class = msg_class;
	frame->message_id = msg_id;
}

/**
 * @brief Finalise a net_buf_simple to be used as a UBX protocol message
 *
 * Populates the header payload size and computes the message checksum.
 *
 * @param buf Buffer to finalise.
 */
static inline void ubx_msg_finalise(struct net_buf_simple *buf)
{
	const unsigned int crc_start_idx = offsetof(struct ubx_frame, message_class);
	struct ubx_frame *frame;
	uint16_t payload_size;
	uint8_t ckA = 0;
	uint8_t ckB = 0;

	frame = (void *)buf->data;
	__ASSERT_NO_MSG(frame->preamble_sync_char_1 == UBX_PREAMBLE_SYNC_CHAR_1);
	__ASSERT_NO_MSG(frame->preamble_sync_char_2 == UBX_PREAMBLE_SYNC_CHAR_2);
	payload_size = buf->len - sizeof(struct ubx_frame);
	/* Finish frame header */
	frame->payload_size_low = payload_size;
	frame->payload_size_high = payload_size >> 8;
	/* Calculate frame CRC */
	for (unsigned int i = crc_start_idx; i < buf->len; i++) {
		ckA += buf->data[i];
		ckB += ckA;
	}
	/* Append frame CRC */
	net_buf_simple_add_u8(buf, ckA);
	net_buf_simple_add_u8(buf, ckB);
}

/**
 * @brief Create a UBX message in a net_buf_simple
 *
 * @param buf Buffer to push message into
 * @param msg_class UBX protocol message class (@ref ubx_msg_class)
 * @param msg_id UBX protocol message ID (depends on @a msg_class)
 * @param msg UBX message payload
 * @param msg_len UBX payload length
 */
static inline void ubx_msg_simple(struct net_buf_simple *buf, uint8_t msg_class, uint8_t msg_id,
				  const void *msg, size_t msg_len)
{
	ubx_msg_prepare(buf, msg_class, msg_id);
	if (msg_len > 0) {
		net_buf_simple_add_mem(buf, msg, msg_len);
	}
	ubx_msg_finalise(buf);
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_GNSS_UBX_PROTOCOL_H_ */
