.. _epacket_bt_adv:

ePacket Bluetooth Advertising
#############################

Bluetooth Low Energy (BLE) advertising is the primary local communication mechanism
for Infuse-IoT devices. It provides a simple, low-power broadcast communications
mechanism that can be received by a diverse range of devices, without any setup
or configuration needed.

To limit congestion on the primary Bluetooth channels and to support data payloads
over 31 bytes, all application data is transmitted using the Bluetooth 5.0
extended advertising standard. While in theory this allows data packets of up to
255 bytes, due to limitations with the Apple iOS ecosystem, Infuse-IoT limits
the maximum packet size to 124 bytes.

Bluetooth Packet Format
***********************

Bluetooth advertising packets consist of multiple advertising data (`AD`_) structures.
Each Infuse-Iot packet always consist of three AD structures.

  1. Flags: Mandatory for connectable BLE devices
  2. UUID16 Incomplete: Contains the Infuse-IoT service UUID (``0xFC74``)
  3. Manufacturer Data: Contains the ePacket data (Company Code ``0x0DE4``)

The "UUID16 Incomplete" type is required to enable background scanning for Infuse-IoT
devices on the smartphone platforms.

Manufacturing Data Format
=========================

The first two bytes of the "Manufacturer Data" AD type contains the company code,
followed by an arbitrary binary data payload.

.. code-block:: c

   /* Packed binary structure for Infuse-IoT manufacturing data */
   struct {
      /* Embeint company code, 0x0DE4 (little endian) */
      uint16_t company_code;
      /* Arbitrary data payload, up to 103 bytes long */
      uint8_t payload[];
   } __packed;

The data payload then consists of a plaintext header, the encrypted data contents, and
the message authentication code.

.. code-block:: c

   /* Packed binary payload structure */
   struct {
      /* Plaint */
      uint16_t header[23];
      /* Arbitrary data payload, up to 64 bytes */
      uint8_t ciphertext[];
      /* Message authentication code (tag bytes) */
      uint8_t authentication[16];
   } __packed;

The format of encrpyted ciphertext depends on the packet type as described in the header.
The header itself has the following format:

.. code-block:: c

   struct epacket_v0_versioned_header_format {
      /* AEAD associated data */
      union {
         struct {
            /* Frame version */
            uint8_t version;
            /* Payload type */
            uint8_t type;
            /* Payload flags */
            uint16_t flags;
            /* Network or device key identifier */
            uint8_t key_identifier[3];
            /* Infuse device ID (upper 4 bytes) */
            uint32_t device_id_upper;
         } __packed;
         uint8_t raw[11];
      } associated_data;
      /* AEAD encryption nonce (IV) */
      union {
         struct {
            /* Infuse device ID (lower 4 bytes) */
            uint32_t device_id_lower;
            /* Local GPS time (seconds) */
            uint32_t gps_time;
            /* Packet sequence number */
            uint16_t sequence;
            /* Random entropy */
            uint16_t entropy;
         } __packed;
         uint8_t raw[12];
      } nonce;
   } __packed;

Of particular use, the 64 bit Infuse-IoT device ID can be constructed from the ``device_id_lower``
and ``device_id_upper`` fields.

.. _AD: https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/CSS_v11/out/en/supplement-to-the-bluetooth-core-specification/data-types-specification.html
