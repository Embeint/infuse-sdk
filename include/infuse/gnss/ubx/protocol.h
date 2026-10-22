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
 * @ingroup infuse_ubx
 * @{
 */

/** First byte of every UBX frame */
#define UBX_PREAMBLE_SYNC_CHAR_1 0xB5
/** Second byte of every UBX frame */
#define UBX_PREAMBLE_SYNC_CHAR_2 0x62

/**
 * @brief UBX Message Frame
 */
struct ubx_frame {
	/** @ref UBX_PREAMBLE_SYNC_CHAR_1 */
	uint8_t preamble_sync_char_1;
	/** @ref UBX_PREAMBLE_SYNC_CHAR_2 */
	uint8_t preamble_sync_char_2;
	/** Message class (@ref ubx_msg_class) */
	uint8_t message_class;
	/** Message ID within @ref ubx_frame::message_class */
	uint8_t message_id;
	/** Payload length, low byte */
	uint8_t payload_size_low;
	/** Payload length, high byte */
	uint8_t payload_size_high;
	/** Payload followed by the two byte checksum */
	uint8_t payload_and_checksum[];
};

/**
 * @brief UBX Message Classes
 */
enum ubx_msg_class {
	/** Acknowledgment and negative acknowledgment messages */
	UBX_MSG_CLASS_ACK = 0x05,
	/** Configuration input messages */
	UBX_MSG_CLASS_CFG = 0x06,
	/** Information messages */
	UBX_MSG_CLASS_INF = 0x04,
	/** Logging messages */
	UBX_MSG_CLASS_LOG = 0x21,
	/** Multiple GNSS assistance messages */
	UBX_MSG_CLASS_MGA = 0x13,
	/** Monitoring messages */
	UBX_MSG_CLASS_MON = 0x0a,
	/** Navigation solution messages */
	UBX_MSG_CLASS_NAV = 0x01,
	/** Receiver manager messages */
	UBX_MSG_CLASS_RXM = 0x02,
	/** Security feature messages */
	UBX_MSG_CLASS_SEC = 0x27,
	/** Timing messages */
	UBX_MSG_CLASS_TIM = 0x0d,
	/** Firmware update messages */
	UBX_MSG_CLASS_UPD = 0x09,
};

/**
 * @defgroup ubx_msg_class_ack UBX-ACK: Acknowledgment and negative acknowledgment messages
 * @ingroup ubx_message_protocol
 * @{
 */

/** @brief UBX-ACK message IDs */
enum ubx_msg_id_ack {
	/** Message not acknowledged */
	UBX_MSG_ID_ACK_NAK = 0x00,
	/** Message acknowledged */
	UBX_MSG_ID_ACK_ACK = 0x01,
};

/** @brief Response from @ref UBX_MSG_ID_ACK_ACK when an input message is accepted */
struct ubx_msg_id_ack_ack {
	/** Class of the accepted message */
	uint8_t message_class;
	/** ID of the accepted message */
	uint8_t message_id;
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_id_ack_ack) == 2);

/** @brief Response from @ref UBX_MSG_ID_ACK_NAK when an input message is rejected */
struct ubx_msg_id_ack_nak {
	/** Class of the rejected message */
	uint8_t message_class;
	/** ID of the rejected message */
	uint8_t message_id;
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_id_ack_nak) == 2);

/**
 * @}
 */

/**
 * @defgroup ubx_msg_class_cfg UBX-CFG: Configuration input messages
 * @ingroup ubx_message_protocol
 * @{
 */

/** @brief UBX-CFG message IDs */
enum ubx_msg_id_cfg {
	/** Port configuration */
	UBX_MSG_ID_CFG_PRT = 0x00,
	/** Message output rate configuration */
	UBX_MSG_ID_CFG_MSG = 0x01,
	/** Reset receiver and clear backup data */
	UBX_MSG_ID_CFG_RST = 0x04,
	/** Navigation and measurement rate configuration */
	UBX_MSG_ID_CFG_RATE = 0x08,
	/** Clear, save and load configurations */
	UBX_MSG_ID_CFG_CFG = 0x09,
	/** Navigation engine configuration */
	UBX_MSG_ID_CFG_NAV5 = 0x24,
	/** GNSS system configuration */
	UBX_MSG_ID_CFG_GNSS = 0x3E,
	/** Burn the DC-DC converter fuse (irreversible) */
	UBX_MSG_ID_CFG_DCDC_BURN = 0x41,
	/** Set configuration item values */
	UBX_MSG_ID_CFG_VALSET = 0x8a,
	/** Get configuration item values */
	UBX_MSG_ID_CFG_VALGET = 0x8b,
	/** Delete configuration item values */
	UBX_MSG_ID_CFG_VALDEL = 0x8c,
};

/** @brief Command or poll response for @ref UBX_MSG_ID_CFG_PRT on the SPI port */
struct ubx_msg_cfg_prt_spi {
	/** Port identifier (@ref ubx_msg_cfg_prt_ports) */
	uint8_t port_id;
	/** Reserved */
	uint8_t reserved1;
	/** TX ready pin configuration (@ref ubx_msg_cfg_prt_tx_ready) */
	uint16_t tx_ready;
	/** SPI mode (@ref ubx_msg_cfg_prt_spi_mode) */
	uint32_t mode;
	/** Reserved */
	uint8_t reserved2[4];
	/** Active input protocols (@ref ubx_msg_cfg_prt_proto_mask) */
	uint16_t in_proto_mask;
	/** Active output protocols (@ref ubx_msg_cfg_prt_proto_mask) */
	uint16_t out_proto_mask;
	/** Port flags (@ref ubx_msg_cfg_prt_spi_flags) */
	uint16_t flags;
	/** Reserved */
	uint8_t reserved3[2];
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_cfg_prt_spi) == 20);

/** @brief Port identifiers for @ref ubx_msg_cfg_prt_spi::port_id */
enum ubx_msg_cfg_prt_ports {
	/** DDC (I2C compatible) port */
	UBX_MSG_CFG_PRT_PORT_ID_I2C = 0,
	/** USB port */
	UBX_MSG_CFG_PRT_PORT_ID_USB = 3,
	/** SPI port */
	UBX_MSG_CFG_PRT_PORT_ID_SPI = 4,
};

/**
 * @brief Protocol masks for @ref ubx_msg_cfg_prt_spi::in_proto_mask and
 *        @ref ubx_msg_cfg_prt_spi::out_proto_mask
 */
enum ubx_msg_cfg_prt_proto_mask {
	/** UBX protocol */
	UBX_MSG_CFG_PRT_PROTO_MASK_UBX = BIT(0),
	/** NMEA protocol */
	UBX_MSG_CFG_PRT_PROTO_MASK_NMEA = BIT(1),
	/** RTCM2 protocol */
	UBX_MSG_CFG_PRT_PROTO_MASK_RTCM2 = BIT(2),
	/** RTCM3 protocol */
	UBX_MSG_CFG_PRT_PROTO_MASK_RTCM3 = BIT(5),
};

/** @brief TX ready configuration for @ref ubx_msg_cfg_prt_spi::tx_ready */
enum ubx_msg_cfg_prt_tx_ready {
	/** Enable the TX ready pin */
	UBX_MSG_CFG_PRT_TX_READY_EN = BIT(0),
	/** TX ready pin is active high */
	UBX_MSG_CFG_PRT_TX_READY_POL_ACTIVE_HIGH = 0,
	/** TX ready pin is active low */
	UBX_MSG_CFG_PRT_TX_READY_POL_ACTIVE_LOW = BIT(1),
};

/**
 * @brief Construct the PIO and threshold portion of @ref ubx_msg_cfg_prt_spi::tx_ready
 *
 * @param pio PIO to use as the TX ready pin
 * @param threshold Number of pending TX bytes before the pin is asserted
 */
#define UBX_MSG_CFG_PRT_TX_READY_CFG(pio, threshold) (pio << 2) | ((threshold / 8) << 7)

/** @brief SPI mode for @ref ubx_msg_cfg_prt_spi::mode */
enum ubx_msg_cfg_prt_spi_mode {
	/** SPI mode 0 (CPOL = 0, CPHA = 0) */
	UBX_MSG_CFG_PRT_SPI_MODE_0 = (0 << 1),
	/** SPI mode 1 (CPOL = 0, CPHA = 1) */
	UBX_MSG_CFG_PRT_SPI_MODE_1 = (1 << 1),
	/** SPI mode 2 (CPOL = 1, CPHA = 0) */
	UBX_MSG_CFG_PRT_SPI_MODE_2 = (2 << 1),
	/** SPI mode 3 (CPOL = 1, CPHA = 1) */
	UBX_MSG_CFG_PRT_SPI_MODE_3 = (3 << 1),
};

/** @brief Flags for @ref ubx_msg_cfg_prt_spi::flags */
enum ubx_msg_cfg_prt_spi_flags {
	/** Only time out the port when at least 4 kB of TX memory is allocated */
	UBX_MSG_CFG_PRT_SPI_EXTENDED_TIMEOUT = BIT(1),
};

/** @brief Command for @ref UBX_MSG_ID_CFG_MSG that sets the output rate on the current port */
struct ubx_msg_cfg_msg {
	/** Class of the message to configure */
	uint8_t msg_class;
	/** ID of the message to configure */
	uint8_t msg_id;
	/** Output rate on the current port, in navigation solutions (0 disables) */
	uint8_t rate;
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_cfg_msg) == 3);

/** @brief Command or poll response for @ref UBX_MSG_ID_CFG_RATE */
struct ubx_msg_cfg_rate {
	/** Nominal time between GNSS measurements (ms) */
	uint16_t meas_rate;
	/** Number of measurements per navigation solution (cycles) */
	uint16_t nav_rate;
	/** Time system that measurements are aligned to (@ref ubx_msg_cfg_rate_time_ref) */
	uint16_t time_ref;
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_cfg_rate) == 6);

/** @brief Time reference for @ref ubx_msg_cfg_rate::time_ref */
enum ubx_msg_cfg_rate_time_ref {
	/** UTC time */
	UBX_MSG_CFG_RATE_TIME_REF_UTC = 0,
	/** GPS time */
	UBX_MSG_CFG_RATE_TIME_REF_GPS = 1,
	/** GLONASS time */
	UBX_MSG_CFG_RATE_TIME_REF_GLONASS = 2,
	/** BeiDou time */
	UBX_MSG_CFG_RATE_TIME_REF_BEIDOU = 3,
	/** Galileo time */
	UBX_MSG_CFG_RATE_TIME_REF_GALILEO = 4,
	/** NavIC time */
	UBX_MSG_CFG_RATE_TIME_REF_NAVIC = 5,
};

/** @brief Command for @ref UBX_MSG_ID_CFG_RST (not acknowledged) */
struct ubx_msg_cfg_rst {
	/** Battery backed RAM sections to clear (@ref ubx_msg_cfg_rst_bbr_mask) */
	uint16_t nav_bbr_mask;
	/** Reset type (@ref ubx_msg_cfg_rst_mode) */
	uint8_t reset_mode;
	/** Reserved */
	uint8_t reserved1;
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_cfg_rst) == 4);

/** @brief Backup data to clear for @ref ubx_msg_cfg_rst::nav_bbr_mask */
enum ubx_msg_cfg_rst_bbr_mask {
	/** Hot start, keep all backup data */
	UBX_MSG_CFG_RST_BBR_HOT_START = 0x0000,
	/** Warm start, clear ephemeris data */
	UBX_MSG_CFG_RST_BBR_WARM_START = 0x0001,
	/** Cold start, clear all backup data */
	UBX_MSG_CFG_RST_BBR_COLD_START = 0xFFFF,
};

/** @brief Reset type for @ref ubx_msg_cfg_rst::reset_mode */
enum ubx_msg_cfg_rst_mode {
	/** Immediate hardware (watchdog) reset */
	UBX_MSG_CFG_RST_MODE_HARDWARE = 0x0000,
};

/** @brief Command for @ref UBX_MSG_ID_CFG_CFG on M8 receivers, with storage device selection */
struct ubx_msg_cfg_cfg_m8 {
	/** Configuration sections to clear (@ref ubx_msg_cfg_cfg_mask) */
	uint32_t clear_mask;
	/** Configuration sections to save (@ref ubx_msg_cfg_cfg_mask) */
	uint32_t save_mask;
	/** Configuration sections to load (@ref ubx_msg_cfg_cfg_mask) */
	uint32_t load_mask;
	/** Storage devices to apply the command to (@ref ubx_msg_cfg_cfg_device) */
	uint8_t device_mask;
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_cfg_cfg_m8) == 13);

/** @brief Command for @ref UBX_MSG_ID_CFG_CFG on M10 receivers */
struct ubx_msg_cfg_cfg_m10 {
	/** Configuration sections to clear (@ref ubx_msg_cfg_cfg_mask) */
	uint32_t clear_mask;
	/** Configuration sections to save (@ref ubx_msg_cfg_cfg_mask) */
	uint32_t save_mask;
	/** Configuration sections to load (@ref ubx_msg_cfg_cfg_mask) */
	uint32_t load_mask;
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_cfg_cfg_m10) == 12);

/** @brief Configuration sections for the @ref UBX_MSG_ID_CFG_CFG clear, save and load masks */
enum ubx_msg_cfg_cfg_mask {
	/** Communications port settings */
	UBX_MSG_CFG_CFG_MASK_IO_PORT = BIT(0),
	/** Message configuration */
	UBX_MSG_CFG_CFG_MASK_MSG_CONF = BIT(1),
	/** Information message configuration */
	UBX_MSG_CFG_CFG_MASK_INF_MSG = BIT(2),
	/** Navigation configuration */
	UBX_MSG_CFG_CFG_MASK_NAV_CONF = BIT(3),
	/** Receiver manager configuration */
	UBX_MSG_CFG_CFG_MASK_RXM_CONF = BIT(4),
	/** Sensor interface configuration */
	UBX_MSG_CFG_CFG_MASK_SEN_CONF = BIT(8),
	/** Remote inventory configuration */
	UBX_MSG_CFG_CFG_MASK_RINV_CONF = BIT(9),
	/** Antenna configuration */
	UBX_MSG_CFG_CFG_MASK_ANT_CONF = BIT(10),
	/** Logging configuration */
	UBX_MSG_CFG_CFG_MASK_LOG_CONF = BIT(11),
	/** FTS configuration (FTS products only) */
	UBX_MSG_CFG_CFG_MASK_FTS_CONF = BIT(12),
};

/** @brief Storage devices for @ref ubx_msg_cfg_cfg_m8::device_mask */
enum ubx_msg_cfg_cfg_device {
	/** Battery backed RAM */
	UBX_MSG_CFG_CFG_DEVICE_BBR = BIT(0),
	/** Flash */
	UBX_MSG_CFG_CFG_DEVICE_FLASH = BIT(1),
	/** EEPROM */
	UBX_MSG_CFG_CFG_DEVICE_EEPROM = BIT(2),
	/** SPI flash */
	UBX_MSG_CFG_CFG_DEVICE_SPI_FLASH = BIT(4),
};

/** @brief Command or poll response for @ref UBX_MSG_ID_CFG_NAV5 */
struct ubx_msg_cfg_nav5 {
	/** Parameters to apply, others are left unchanged */
	uint16_t mask;
	/** Dynamic platform model */
	uint8_t dyn_model;
	/** Position fixing mode */
	uint8_t fix_mode;
	/** Fixed altitude (mean sea level) for 2D fix mode (0.01 m) */
	int32_t fixed_alt;
	/** Fixed altitude variance for 2D mode (0.0001 m^2) */
	uint32_t fixed_alt_var;
	/** Minimum satellite elevation to be used in navigation (deg) */
	int8_t min_elev;
	/** Reserved */
	uint8_t dr_limit;
	/** Position DOP mask (0.1) */
	uint16_t p_dop;
	/** Time DOP mask (0.1) */
	uint16_t t_dop;
	/** Position accuracy mask (m) */
	uint16_t p_acc;
	/** Time accuracy mask (m) */
	uint16_t t_acc;
	/** Static hold speed threshold (cm/s) */
	uint8_t static_hold_threshold;
	/** DGNSS timeout (s) */
	uint8_t d_gnss_timeout;
	/** Number of satellites required above @ref ubx_msg_cfg_nav5::cno_threshold to attempt a
	 * fix
	 */
	uint8_t cno_threshold_num_svs;
	/** C/N0 threshold for deciding whether to attempt a fix (dBHz) */
	uint8_t cno_threshold;
	/** Reserved */
	uint8_t reserved1[2];
	/** Distance moved before static hold is exited (m) */
	uint16_t static_hold_max_dist;
	/** UTC standard to use */
	uint8_t utc_standard;
	/** Reserved */
	uint8_t reserved2[5];
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_cfg_nav5) == 36);

/** @brief Command or poll response for @ref UBX_MSG_ID_CFG_GNSS */
struct ubx_msg_cfg_gnss {
	/** Message version (0x00 for this version) */
	uint8_t msg_ver;
	/** Number of tracking channels available in hardware (read only) */
	uint8_t num_trk_ch_hw;
	/** Number of tracking channels to use */
	uint8_t num_trk_ch_use;
	/** Number of entries in @ref ubx_msg_cfg_gnss::configs */
	uint8_t num_cfg_blocks;
	/** Configuration for a single GNSS system */
	struct ubx_msg_cfg_gnss_cfg {
		/** GNSS identifier */
		uint8_t gnss_id;
		/** Number of reserved (minimum) tracking channels */
		uint8_t res_trk_chan;
		/** Maximum number of tracking channels */
		uint8_t max_trk_chan;
		/** Reserved */
		uint8_t reserved1;
		/** System configuration flags (@ref ubx_msg_cfg_gnss_flags) */
		uint32_t flags;
	} __packed configs[]; /**< One configuration block per GNSS system */
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_cfg_gnss) == 4);

/** @brief Flags for @ref ubx_msg_cfg_gnss::ubx_msg_cfg_gnss_cfg::flags */
enum ubx_msg_cfg_gnss_flags {
	/** GNSS system is enabled */
	UBX_MSG_CFG_GNSS_FLAGS_ENABLED = BIT(0),
};

/** @brief Command for @ref UBX_MSG_ID_CFG_VALSET (version 0, no transaction support) */
struct ubx_msg_cfg_valset_v0 {
	/** Message version (0x00 for this version) */
	uint8_t version;
	/** Configuration layers to update (@ref ubx_msg_cfg_valset_layers) */
	uint8_t layers;
	/** Reserved */
	uint8_t reserved[2];
	/** Configuration key and value pairs */
	uint8_t cfg_data[];
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_cfg_valset_v0) == 4);

/** @brief Command for @ref UBX_MSG_ID_CFG_VALSET (version 1, with transaction support) */
struct ubx_msg_cfg_valset_v1 {
	/** Message version (0x01 for this version) */
	uint8_t version;
	/** Configuration layers to update (@ref ubx_msg_cfg_valset_layers) */
	uint8_t layers;
	/** Transaction action (@ref ubx_msg_cfg_valset_transaction) */
	uint8_t transaction;
	/** Reserved */
	uint8_t reserved;
	/** Configuration key and value pairs */
	uint8_t cfg_data[];
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_cfg_valset_v1) == 4);

/** @brief Configuration layers to write with @ref UBX_MSG_ID_CFG_VALSET */
enum ubx_msg_cfg_valset_layers {
	/** RAM layer */
	UBX_MSG_CFG_VALSET_LAYERS_RAM = BIT(0),
	/** Battery backed RAM layer */
	UBX_MSG_CFG_VALSET_LAYERS_BBR = BIT(1),
	/** Flash layer */
	UBX_MSG_CFG_VALSET_LAYERS_FLASH = BIT(2),
};

/** @brief Transaction state for @ref ubx_msg_cfg_valset_v1::transaction */
enum ubx_msg_cfg_valset_transaction {
	/** No transaction, apply the values immediately */
	UBX_MSG_CFG_VALSET_TRANSACTION_NONE = 0,
	/** Start a new transaction, discarding any pending one */
	UBX_MSG_CFG_VALSET_TRANSACTION_START = 1,
	/** Add the values to the current transaction */
	UBX_MSG_CFG_VALSET_TRANSACTION_ONGOING = 2,
	/** Add the values and apply the transaction */
	UBX_MSG_CFG_VALSET_TRANSACTION_APPLY = 3,
};

/** @brief Poll request for @ref UBX_MSG_ID_CFG_VALGET */
struct ubx_msg_cfg_valget_query {
	/** Message version (0x00 for this version) */
	uint8_t version;
	/** Configuration layer to read (@ref ubx_msg_cfg_valget_layer) */
	uint8_t layer;
	/** Number of values to skip before returning results */
	uint16_t position;
	/** Configuration keys to read */
	uint32_t cfg_keys[];
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_cfg_valget_query) == 4);

/** @brief Response from @ref UBX_MSG_ID_CFG_VALGET */
struct ubx_msg_cfg_valget_response {
	/** Message version (0x01 for this version) */
	uint8_t version;
	/** Configuration layer the values were read from (@ref ubx_msg_cfg_valget_layer) */
	uint8_t layer;
	/** Number of values skipped before these results */
	uint16_t position;
	/** Configuration key and value pairs */
	uint8_t cfg_data[];
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_cfg_valget_response) == 4);

/** @brief Configuration layer to read with @ref UBX_MSG_ID_CFG_VALGET */
enum ubx_msg_cfg_valget_layer {
	/** RAM layer */
	UBX_MSG_CFG_VALGET_LAYER_RAM = 0,
	/** Battery backed RAM layer */
	UBX_MSG_CFG_VALGET_LAYER_BBR = 1,
	/** Flash layer */
	UBX_MSG_CFG_VALGET_LAYER_FLASH = 2,
	/** Default values */
	UBX_MSG_CFG_VALGET_LAYER_DEFAULT = 7,
};

/**
 * @}
 */

/**
 * @defgroup ubx_msg_class_mon UBX-MON: Monitoring messages
 * @ingroup ubx_message_protocol
 * @{
 */

/** @brief UBX-MON message IDs */
enum ubx_msg_id_mon {
	/** Data batching buffer status */
	UBX_MSG_ID_MON_BATCH = 0x32,
	/** Communication port information */
	UBX_MSG_ID_MON_COMMS = 0x36,
	/** Information about GNSS system selection */
	UBX_MSG_ID_MON_GNSS = 0x28,
	/** I/O pin status */
	UBX_MSG_ID_MON_HW3 = 0x37,
	/** Hardware status */
	UBX_MSG_ID_MON_HW = 0x09,
	/** Installed patches */
	UBX_MSG_ID_MON_PATCH = 0x27,
	/** RF information */
	UBX_MSG_ID_MON_RF = 0x38,
	/** Receiver status information */
	UBX_MSG_ID_MON_RXR = 0x21,
	/** Signal characteristics */
	UBX_MSG_ID_MON_SPAN = 0x31,
	/** Receiver and software version */
	UBX_MSG_ID_MON_VER = 0x04,
};

/** @brief Response from @ref UBX_MSG_ID_MON_HW3 (polled or periodic) */
struct ubx_msg_mon_hw3 {
	/** Message version (0x00 for this version) */
	uint8_t version;
	/** Number of entries in @ref ubx_msg_mon_hw3::pins */
	uint8_t n_pins;
	/** Receiver flags (@ref ubx_msg_mon_hw3_flags) */
	uint8_t flags;
	/** Null-terminated hardware version string */
	char hw_version[10];
	/** Reserved */
	uint8_t reserved0[9];
	/** Per pin state */
	struct {
		/** Reserved */
		uint8_t reserved1;
		/** Pin identifier, covering both external and internal pins */
		uint8_t pin_id;
		/** Pin state (@ref ubx_msg_mon_hw3_pin_mask) */
		uint16_t pin_mask;
		/** Virtual pin mapping */
		uint8_t vp;
		/** Reserved */
		uint8_t reserved2;
	} pins[];
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_mon_hw3) == 22);

/** @brief Flags for @ref ubx_msg_mon_hw3::flags */
enum ubx_msg_mon_hw3_flags {
	/** RTC is calibrated */
	UBX_MSG_MON_HW3_FLAGS_RTC_CALIB = BIT(0),
	/** Safe boot mode is active */
	UBX_MSG_MON_HW3_FLAGS_SAFE_BOOT = BIT(1),
	/** RTC crystal is absent */
	UBX_MSG_MON_HW3_FLAGS_XTAL_ABSENT = BIT(2),
};

/** @brief Pin state flags for @ref ubx_msg_mon_hw3::pins */
enum ubx_msg_mon_hw3_pin_mask {
	/** Pin is a PIO (otherwise peripheral) */
	UBX_MSG_MON_HW3_PIN_MASK_PIO = BIT(0),
	/** Bank the pin belongs to */
	UBX_MSG_MON_HW3_PIN_MASK_BANK_MASK = (0x07 << 1),
	/** Pin is an output */
	UBX_MSG_MON_HW3_PIN_MASK_DIR_OUT = BIT(4),
	/** Pin is high */
	UBX_MSG_MON_HW3_PIN_MASK_VALUE = BIT(5),
	/** Pin is used by the virtual pin manager */
	UBX_MSG_MON_HW3_PIN_MASK_VIRTUAL = BIT(6),
	/** Pin interrupt is enabled */
	UBX_MSG_MON_HW3_PIN_MASK_INT_EN = BIT(7),
	/** Pull-up resistor is enabled */
	UBX_MSG_MON_HW3_PIN_MASK_PULL_UP = BIT(8),
	/** Pull-down resistor is enabled */
	UBX_MSG_MON_HW3_PIN_MASK_PULL_DOWN = BIT(9),
};

/** @brief Response from @ref UBX_MSG_ID_MON_HW (polled or periodic) */
struct ubx_msg_mon_hw {
	/** Mask of pins set as PIO (otherwise peripheral) */
	uint32_t pin_sel;
	/** Mask of pins set as bank B (otherwise bank A) */
	uint32_t pin_bank;
	/** Mask of pins set as outputs (otherwise inputs) */
	uint32_t pin_dir;
	/** Mask of pins currently high */
	uint32_t pin_val;
	/** Noise level measured by the GNSS core */
	uint16_t noise_per_ms;
	/** AGC monitor (range 0 to 8191) */
	uint16_t agc_cnt;
	/** Antenna supervisor state (0 = init, 1 = unknown, 2 = OK, 3 = short, 4 = open) */
	uint8_t ant_state;
	/** Antenna power state (0 = off, 1 = on, 2 = unknown) */
	uint8_t ant_power;
	/** Hardware status flags */
	uint8_t flags;
	/** Reserved */
	uint8_t reserved1;
	/** Mask of pins used by the virtual pin manager */
	uint32_t used_mask;
	/** Virtual pin mapping for each physical pin */
	uint8_t vp[17];
	/** CW jamming indicator (0 = none, 255 = strong) */
	uint8_t cw_supression;
	/** Reserved */
	uint8_t reserved2[2];
	/** Mask of pins with the PIO interrupt enabled */
	uint32_t pin_irq;
	/** Mask of pins with the pull-up resistor enabled */
	uint32_t pull_high;
	/** Mask of pins with the pull-down resistor enabled */
	uint32_t pull_low;
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_mon_hw) == 60);

/** @brief Response from @ref UBX_MSG_ID_MON_RXR, output when the receiver changes power state */
struct ubx_msg_mon_rxr {
	/** Receiver status flags (@ref ubx_msg_mon_rxr_flags) */
	uint8_t flags;
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_mon_rxr) == 1);

/** @brief Flags for @ref ubx_msg_mon_rxr::flags */
enum ubx_msg_mon_rxr_flags {
	/** Receiver is awake (not in backup mode) */
	UBX_MSG_MON_RXR_AWAKE = BIT(0),
};

/** @brief Response from @ref UBX_MSG_ID_MON_VER */
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
 * @defgroup ubx_msg_class_nav UBX-NAV: Navigation solution messages
 * @ingroup ubx_message_protocol
 * @{
 */

/** @brief UBX-NAV message IDs */
enum ubx_msg_id_nav {
	/** AssistNow Autonomous status */
	UBX_MSG_ID_NAV_AOPSTATUS = 0x60,
	/** Clock solution */
	UBX_MSG_ID_NAV_CLOCK = 0x22,
	/** Covariance matrices */
	UBX_MSG_ID_NAV_COV = 0x36,
	/** Dilution of precision */
	UBX_MSG_ID_NAV_DOP = 0x04,
	/** End of epoch */
	UBX_MSG_ID_NAV_EOE = 0x61,
	/** Odometer solution */
	UBX_MSG_ID_NAV_ODO = 0x09,
	/** GNSS orbit database information */
	UBX_MSG_ID_NAV_ORB = 0x34,
	/** Protection level information */
	UBX_MSG_ID_NAV_PL = 0x64,
	/** Position solution in ECEF */
	UBX_MSG_ID_NAV_POSECEF = 0x01,
	/** Geodetic position solution */
	UBX_MSG_ID_NAV_POSLLH = 0x02,
	/** Navigation position, velocity and time solution */
	UBX_MSG_ID_NAV_PVT = 0x07,
	/** Reset odometer */
	UBX_MSG_ID_NAV_RESETODO = 0x10,
	/** Satellite information */
	UBX_MSG_ID_NAV_SAT = 0x35,
	/** SBAS status data */
	UBX_MSG_ID_NAV_SBAS = 0x32,
	/** Signal information */
	UBX_MSG_ID_NAV_SIG = 0x43,
	/** QZSS L1S SLAS status data */
	UBX_MSG_ID_NAV_SLAS = 0x42,
	/** Receiver navigation status */
	UBX_MSG_ID_NAV_STATUS = 0x03,
	/** BeiDou time solution */
	UBX_MSG_ID_NAV_TIMEBDS = 0x24,
	/** Galileo time solution */
	UBX_MSG_ID_NAV_TIMEGAL = 0x25,
	/** GLONASS time solution */
	UBX_MSG_ID_NAV_TIMEGLO = 0x23,
	/** GPS time solution */
	UBX_MSG_ID_NAV_TIMEGPS = 0x20,
	/** Leap second event information */
	UBX_MSG_ID_NAV_TIMELS = 0x26,
	/** QZSS time solution */
	UBX_MSG_ID_NAV_TIMEQZSS = 0x27,
	/** UTC time solution */
	UBX_MSG_ID_NAV_TIMEUTC = 0x21,
	/** Velocity solution in ECEF */
	UBX_MSG_ID_NAV_VELECEF = 0x11,
	/** Velocity solution in NED frame */
	UBX_MSG_ID_NAV_VELNED = 0x10,
};

/** @brief Response from @ref UBX_MSG_ID_NAV_DOP (polled or periodic) */
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

/** @brief Response from @ref UBX_MSG_ID_NAV_PVT (polled or periodic) */
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
	/** Reserved */
	uint8_t reserved0[4];
	/** 2D Heading of vehicle, this is only valid when headVehValid is set (1e-5 deg) */
	int32_t head_veh;
	/** Magnetic declination. Only supported in ADR 4.10 and later (1e-2 deg) */
	int16_t mag_dec;
	/** Magnetic declination accuracy. Only supported in ADR 4.10 and later (1e-2 deg) */
	uint16_t mag_acc;
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_nav_pvt) == 92);

/** @brief Validity flags for @ref ubx_msg_nav_pvt::valid */
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

/** @brief Fix types for @ref ubx_msg_nav_pvt::fix_type */
enum ubx_msg_nav_pvt_fix_type {
	/** No fix */
	UBX_MSG_NAV_PVT_FIX_TYPE_NO_FIX = 0,
	/** Dead reckoning only */
	UBX_MSG_NAV_PVT_FIX_TYPE_DEAD_RECKONING = 1,
	/** 2D fix */
	UBX_MSG_NAV_PVT_FIX_TYPE_2D = 2,
	/** 3D fix */
	UBX_MSG_NAV_PVT_FIX_TYPE_3D = 3,
	/** GNSS and dead reckoning combined */
	UBX_MSG_NAV_PVT_FIX_TYPE_GNSS_DEAD_RECKONING = 4,
	/** Time only fix */
	UBX_MSG_NAV_PVT_FIX_TYPE_TIME_ONLY = 5,
};

/** @brief Fix status flags for @ref ubx_msg_nav_pvt::flags */
enum ubx_msg_nav_pvt_flags {
	/** Valid fix (i.e within DOP & accuracy masks) */
	UBX_MSG_NAV_PVT_FLAGS_GNSS_FIX_OK = BIT(0),
	/** Differential corrections were applied */
	UBX_MSG_NAV_PVT_FLAGS_DIFF_SOLN = BIT(1),
	/** Power save mode state */
	UBX_MSG_NAV_PVT_FLAGS_PSM_MASK = (0x7 << 2),
	/** Power save mode is not active */
	UBX_MSG_NAV_PVT_FLAGS_PSM_DISABLED = (0 << 2),
	/** Power save mode enabled, before acquisition starts */
	UBX_MSG_NAV_PVT_FLAGS_PSM_ENABLED = (1 << 2),
	/** Power save mode acquisition */
	UBX_MSG_NAV_PVT_FLAGS_PSM_ACQUISITION = (2 << 2),
	/** Power save mode tracking */
	UBX_MSG_NAV_PVT_FLAGS_PSM_TRACKING = (3 << 2),
	/** Power save mode power optimised tracking */
	UBX_MSG_NAV_PVT_FLAGS_PSM_POWER_OPTIMIZED_TRACKING = (4 << 2),
	/** Power save mode inactive */
	UBX_MSG_NAV_PVT_FLAGS_PSM_INACTIVE = (5 << 2),
	/** Heading of vehicle is valid, only set if the receiver is in sensor fusion mode */
	UBX_MSG_NAV_PVT_FLAGS_HEAD_VEH_VALID = BIT(5),
	/** Carrier phase range solution status */
	UBX_MSG_NAV_PVT_FLAGS_CARR_SOLN_MASK = (0x3 << 6),
	/** No carrier phase range solution */
	UBX_MSG_NAV_PVT_FLAGS_CARR_SOLN_NO_SOLN = (0 << 6),
	/** Carrier phase range solution with floating ambiguities */
	UBX_MSG_NAV_PVT_FLAGS_CARR_SOLN_FLOATING_ABIGUITIES = (1 << 6),
	/** Carrier phase range solution with fixed ambiguities */
	UBX_MSG_NAV_PVT_FLAGS_CARR_SOLN_FIXED_ABIGUITIES = (2 << 6),
};

/** @brief Additional flags for @ref ubx_msg_nav_pvt::flags2 */
enum ubx_msg_nav_pvt_flags2 {
	/** Information about UTC Date and Time of Day validity confirmation is available */
	UBX_MSG_NAV_PVT_FLAGS2_CONFIRMED_AVAI = BIT(5),
	/** UTC Date validity could be confirmed */
	UBX_MSG_NAV_PVT_FLAGS2_CONFIRMED_DATE = BIT(6),
	/** UTC Time of Day could be confirmed */
	UBX_MSG_NAV_PVT_FLAGS2_CONFIRMED_TIME = BIT(7),
};

/** @brief Additional flags for @ref ubx_msg_nav_pvt::flags3 */
enum ubx_msg_nav_pvt_flags3 {
	/** Invalid lon, lat, height and hMS */
	UBX_MSG_NAV_PVT_FLAGS3_INVALID_LLH = BIT(0),
	/** Age of the most recently received differential correction (seconds) */
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_MASK = (0xF << 1),
	/** Not available */
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_NA = (0 << 1),
	/** 0 to 1 seconds */
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_0_1 = (1 << 1),
	/** 1 to 2 seconds */
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_1_2 = (2 << 1),
	/** 2 to 5 seconds */
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_2_5 = (3 << 1),
	/** 5 to 10 seconds */
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_5_10 = (4 << 1),
	/** 10 to 15 seconds */
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_10_15 = (5 << 1),
	/** 15 to 20 seconds */
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_15_20 = (6 << 1),
	/** 20 to 30 seconds */
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_20_30 = (7 << 1),
	/** 30 to 45 seconds */
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_30_45 = (8 << 1),
	/** 45 to 60 seconds */
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_45_60 = (9 << 1),
	/** 60 to 90 seconds */
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_60_90 = (10 << 1),
	/** 90 to 120 seconds */
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_90_120 = (11 << 1),
	/** At least 120 seconds */
	UBX_MSG_NAV_PVT_FLAGS3_CORRECTION_AGE_120_N = (12 << 1),
	/** Output time has been validated against an external trusted time source */
	UBX_MSG_NAV_PVT_FLAGS3_AUTH_TIME = BIT(13),
};

/** @brief Response from @ref UBX_MSG_ID_NAV_SAT (polled or periodic) */
struct ubx_msg_nav_sat {
	/** GPS time of week of the navigation epoch */
	uint32_t itow;
	/** Message version (0x01 for this version) */
	uint8_t version;
	/** Number of satellites */
	uint8_t num_svs;
	/** Reserved */
	uint8_t reserved0[2];
	/** Information for a single satellite */
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
	} __packed svs[]; /**< One entry per satellite */
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_nav_sat) == 8);

/** @brief Flags for @ref ubx_msg_nav_sat::ubx_msg_nav_sat_sv::flags */
enum ubx_msg_nav_sat_sv_flags {
	/** Signal quality indicator */
	UBX_MSG_NAV_SAT_FLAGS_QUALITY_IND_MASK = 0x7,
	/** No signal */
	UBX_MSG_NAV_SAT_FLAGS_QUALITY_IND_NO_SIGNAL = 0,
	/** Searching signal */
	UBX_MSG_NAV_SAT_FLAGS_QUALITY_IND_SEARCHING = 1,
	/** Signal acquired */
	UBX_MSG_NAV_SAT_FLAGS_QUALITY_IND_ACQUIRED = 2,
	/** Signal detected but unusable */
	UBX_MSG_NAV_SAT_FLAGS_QUALITY_IND_UNUSABLE = 3,
	/** Code locked and time synchronised */
	UBX_MSG_NAV_SAT_FLAGS_QUALITY_IND_CODE_LOCKED = 4,
	/** Code and carrier locked and time synchronised */
	UBX_MSG_NAV_SAT_FLAGS_QUALITY_IND_CODE_CARRIER_LOCKED1 = 5,
	/** Code and carrier locked and time synchronised */
	UBX_MSG_NAV_SAT_FLAGS_QUALITY_IND_CODE_CARRIER_LOCKED2 = 6,
	/** Code and carrier locked and time synchronised */
	UBX_MSG_NAV_SAT_FLAGS_QUALITY_IND_CODE_CARRIER_LOCKED3 = 7,
	/** Signal in the subset specified in Signal Identifiers is currently being used for
	 * navigation
	 */
	UBX_MSG_NAV_SAT_FLAGS_SV_USED = BIT(3),
	/** Signal health flag */
	UBX_MSG_NAV_SAT_FLAGS_HEALTH_MASK = (0x3 << 4),
	/** Health unknown */
	UBX_MSG_NAV_SAT_FLAGS_HEALTH_UNKNOWN = (0 << 4),
	/** Healthy */
	UBX_MSG_NAV_SAT_FLAGS_HEALTH_HEALTHY = (1 << 4),
	/** Unhealthy */
	UBX_MSG_NAV_SAT_FLAGS_HEALTH_UNHEALTHY = (2 << 4),
	/** Differential correction data is available for this SV */
	UBX_MSG_NAV_SAT_FLAGS_DIFF_CORR = BIT(6),
	/** Carrier smoothed pseudorange used */
	UBX_MSG_NAV_SAT_FLAGS_SMOOTHED = BIT(7),
	/** Orbit source */
	UBX_MSG_NAV_SAT_FLAGS_ORBIT_SOURCE_MASK = (0x7 << 8),
	/** No orbit information available */
	UBX_MSG_NAV_SAT_FLAGS_ORBIT_SOURCE_NONE = (0 << 8),
	/** Ephemeris used */
	UBX_MSG_NAV_SAT_FLAGS_ORBIT_SOURCE_EPHEMERIS = (1 << 8),
	/** Almanac used */
	UBX_MSG_NAV_SAT_FLAGS_ORBIT_SOURCE_ALMANAC = (2 << 8),
	/** AssistNow Offline orbit used */
	UBX_MSG_NAV_SAT_FLAGS_ORBIT_SOURCE_ASSIST_NOW_OFFLINE = (3 << 8),
	/** AssistNow Autonomous orbit used */
	UBX_MSG_NAV_SAT_FLAGS_ORBIT_SOURCE_ASSIST_NOW_AUTONOMOUS = (4 << 8),
	/** Other orbit information used */
	UBX_MSG_NAV_SAT_FLAGS_ORBIT_SOURCE_OTHER1 = (5 << 8),
	/** Other orbit information used */
	UBX_MSG_NAV_SAT_FLAGS_ORBIT_SOURCE_OTHER2 = (6 << 8),
	/** Other orbit information used */
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

/** @brief Response from @ref UBX_MSG_ID_NAV_TIMEGPS (polled or periodic) */
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

/** @brief Validity flags for @ref ubx_msg_nav_timegps::valid */
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
 * @defgroup ubx_msg_class_rxm UBX-RXM: Receiver manager messages
 * @ingroup ubx_message_protocol
 * @{
 */

/** @brief UBX-RXM message IDs */
enum ubx_msg_id_rxm {
	/** Satellite measurements for cloud positioning (20 byte format) */
	UBX_MSG_ID_RXM_MEAS20 = 0x84,
	/** Satellite measurements for cloud positioning (50 byte format) */
	UBX_MSG_ID_RXM_MEAS50 = 0x86,
	/** Satellite measurements for cloud positioning (C12 format) */
	UBX_MSG_ID_RXM_MEASC12 = 0x82,
	/** Satellite measurements for cloud positioning (D12 format) */
	UBX_MSG_ID_RXM_MEASD12 = 0x80,
	/** Satellite measurements for RRLP */
	UBX_MSG_ID_RXM_MEASX = 0x14,
	/** Power management request */
	UBX_MSG_ID_RXM_PMREQ = 0x41,
	/** Galileo SAR return link message report */
	UBX_MSG_ID_RXM_RLM = 0x59,
	/** Broadcast navigation data subframe */
	UBX_MSG_ID_RXM_SFRBX = 0x13,
};

/** @brief Command for @ref UBX_MSG_ID_RXM_PMREQ (version 0) */
struct ubx_msg_rxm_pmreq {
	/** Message version (0x00 for this version) */
	uint8_t version;
	/** Reserved */
	uint8_t reserved0[3];
	/** Duration of the requested task, 0 for infinite (ms) */
	uint32_t duration_ms;
	/** Task flags (@ref ubx_msg_rxm_pmreq_flags) */
	uint32_t flags;
	/** Sources that wake the receiver (@ref ubx_msg_rxm_pmreq_wakeup) */
	uint32_t wakeup_sources;
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_rxm_pmreq) == 16);

/** @brief Flags for @ref ubx_msg_rxm_pmreq::flags */
enum ubx_msg_rxm_pmreq_flags {
	/** Enter backup mode for the requested duration */
	UBX_MSG_RXM_PMREQ_FLAGS_BACKUP = BIT(1),
	/** Force backup mode even while USB is connected */
	UBX_MSG_RXM_PMREQ_FLAGS_FORCE = BIT(2),
};

/** @brief Wakeup sources for @ref ubx_msg_rxm_pmreq::wakeup_sources */
enum ubx_msg_rxm_pmreq_wakeup {
	/** Wake on a UART RX pin edge */
	UBX_MSG_RXM_PMREQ_WAKEUP_UARTRX = BIT(3),
	/** Wake on an EXTINT0 pin edge */
	UBX_MSG_RXM_PMREQ_WAKEUP_EXTINT0 = BIT(5),
	/** Wake on an EXTINT1 pin edge */
	UBX_MSG_RXM_PMREQ_WAKEUP_EXTINT1 = BIT(6),
	/** Wake on an SPI CS pin edge */
	UBX_MSG_RXM_PMREQ_WAKEUP_SPICS = BIT(7),
};

/**
 * @}
 */

/**
 * @defgroup ubx_msg_class_sec UBX-SEC: Security feature messages
 * @ingroup ubx_message_protocol
 * @{
 */

/** @brief UBX-SEC message IDs */
enum ubx_msg_id_sec {
	/** Unique chip ID */
	UBX_MSG_ID_SEC_UNIQID = 0x03,
};

/** @brief Response from @ref UBX_MSG_ID_SEC_UNIQID */
struct ubx_msg_sec_uniqid {
	/** Message version */
	uint8_t version;
	/** Reserved */
	uint8_t reserved0[3];
	/** Unique chip ID */
	uint8_t uniqueId[6];
} __packed;
BUILD_ASSERT(sizeof(struct ubx_msg_sec_uniqid) == 10);

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
