# LoRaWAN Single Channel Gateway for ChirpStack

Single-channel LoRaWAN gateway based on **Heltec WiFi LoRa 32 V4** (ESP32-S3 + SX1262) using Radiolib and tested with **ChirpStack** via Semtech UDP protocol.

## 🎯 Features

- ✅ **Hardware**: Heltec WiFi LoRa 32 V4 (ESP32-S3 + SX1262)
- ✅ **RadioLib**: Full support for SX1262
- ✅ **Lorawan server**: Standard Semtech UDP protocol
- ✅ **OLED Display**: Real-time statistics
- ✅ **WiFi**: Automatic connection with retry
- ✅ **NTP**: Automatic time synchronization
- ✅ **Statistics**: Advanced RX/TX packet counters
- ✅ **Downlink**: Class A support for devices with reception in RX1 and RX2 windows
- ✅ **Downlink**: Class C support for devices with continuous reception
- ✅ **OTA Updates**: Firmware updates via WiFi
- ✅ **Multi-frequency**: Optional support for hopping on multiple frequencies
- ✅ **Debug**: Detailed statistics on interrupts, CRC, timeout

## 🎯 Todo

- [ ] **Lorawan server**: Connection with LoRaWAN server via MQTT

## 📋 Hardware Requirements

- Heltec WiFi LoRa 32 V4
- LoRa 868MHz antenna (or 433MHz according to configuration)
- USB-C cable for programming

## 📁 Project Structure

```
ESP32-1ch-Gateway/
├── src/
│   ├── main.cpp          # Main gateway code
│   ├── TypeDef.h         # Semtech UDP packet definitions
│   └── common.h          # Utility functions (Base64, Gateway ID)
├── include/
│   └── config.h          # Configuration file
├── variants/
│   └── heltec_v4/        # Pin definitions for Heltec V4
├── boards/
│   └── heltec_v4.json    # PlatformIO board definition
├── test_node/            # LoRaWAN test node project with radiolib
└── platformio.ini        # PlatformIO configuration
```

## 🔧 Configuration

### 1. Copy `include/config.h.example` to `include/config.h`

#### Basic Configuration

```cpp
// WiFi
#define WIFI_SSID "YourWiFi"
#define WIFI_PASSWORD "YourPassword"

// ChirpStack Server
#define SERVER_HOST "192.168.1.100"  // IP of your ChirpStack server
#define SERVER_PORT 1700

// LoRa
#define LORA_FREQUENCY 868.1  // MHz (change according to your region)
#define LORA_SPREADING_FACTOR 7  // 7-12
#define LORA_BANDWIDTH 125.0  // kHz (125, 250, 500)
#define LORA_OUTPUT_POWER 22  // dBm (-9 to 22)
```

#### Downlink Configuration (Class C)

```cpp
// Enable automatic response to nodes
#define AUTO_DOWNLINK_ENABLED true

// RX window timing (in milliseconds)
#define RX1_DELAY 1000   // RX1 window delay
#define RX2_DELAY 2000   // RX2 window delay
```

#### OTA Configuration

```cpp
#define OTA_HOSTNAME "esp32-gateway"
#define OTA_PASSWORD "admin"
```

### 2. Install dependencies and compile

```bash
cd ESP32-1ch-Gateway

# Install PlatformIO dependencies (if not already installed)
pio pkg install

# Compile the project
platformio run

# Upload to device
platformio run --target upload
```

### 3. Monitor serial

```bash
platformio device monitor
```

The gateway will print:
- Initialization information
- WiFi status and ChirpStack connection
- Received and forwarded LoRa packets
- Statistics every 2 minutes
- Errors and debug messages

## 📊 ChirpStack Configuration

### 1. Add the Gateway

1. Login to ChirpStack Web UI
2. Go to **Gateways** → **Add Gateway**
3. **Gateway ID**: Use the ID printed in serial (format: `AABBCCFFFFDDEEFF`)
   - The Gateway ID is automatically generated from the MAC address
   - Format: `MAC[0:2]:FF:FF:MAC[3:5]` (e.g., `AA:BB:CC:DD:EE:FF` → `AABBCCFFFFDDEEFF`)
4. **Gateway name**: `heltec-gateway-01` (or a name of your choice)
5. **Network server**: Select your network server
6. **Gateway profile**: Select an appropriate profile
   - **Single-channel gateway**: Use a profile with a single frequency
   - **Multi-channel gateway**: If `MULTI_SF_ENABLED` is enabled, use multi-channel profile
7. Save

### 2. Verify Connection

In the ChirpStack UI, check:
- **Gateways** → Your gateway → **Status**: Should be "online"
- **Live LoRaWAN frames**: You should see received packets
- **Gateway statistics**: You should see statistics every 5 minutes

### 3. Downlink Configuration

To enable downlink (messages from ChirpStack to nodes):

1. Make sure `AUTO_DOWNLINK_ENABLED` is `true` in `config.h`
2. Configure LoRaWAN keys (`LORAWAN_SNWKSINTKEY`, `LORAWAN_APPSKEY`) matching your nodes
3. Set `LORAWAN_DEVADDR` of the target node (or `0` to accept any node)
4. In ChirpStack, configure devices with the same keys
5. Send downlink messages from ChirpStack UI

**Note**: The gateway currently supports a single node at a time. For multi-node support, implement a `DevAddr → Keys` database.

## 🎨 OLED Display

The display shows real-time information:

```
LoRaWAN Gateway
WiFi: OK
868.1MHz SF7
RX: 123 FW: 120
12:34:56
```

**Legend:**
- **WiFi**: Connection status (OK/DISC)
- **MHz/SF**: Configured frequency and Spreading Factor
- **RX**: Successfully received packets (`stats.rx_ok`)
- **FW**: Packets forwarded to ChirpStack (`stats.rx_fw`)
- **Time**: Current time synchronized via NTP

**During OTA Update:**
The display shows firmware update progress.

**Disable Display:**
If you don't need the display, set in `config.h`:
```cpp
#define DISPLAY_ENABLED false
```

## 🌍 LoRa Frequencies

### Europe (EU868)
```cpp
#define LORA_FREQUENCY 868.1  // 868.3, 868.5
#define LORA_SPREADING_FACTOR 7
#define LORA_BANDWIDTH 125.0
```

**Available EU868 channels:**
- 868.1 MHz (Channel 0)
- 868.3 MHz (Channel 1)
- 868.5 MHz (Channel 2)
- 867.1 MHz, 867.3 MHz, 867.5 MHz, 867.7 MHz, 867.9 MHz (Channel 3-7)

### Italy (EU433)
```cpp
#define LORA_FREQUENCY 433.175
#define LORA_SPREADING_FACTOR 7
#define LORA_BANDWIDTH 125.0
```

### USA (US915)
```cpp
#define LORA_FREQUENCY 915.0
#define LORA_SPREADING_FACTOR 7
#define LORA_BANDWIDTH 125.0
```

**Note**: For single-channel gateway, use a single frequency. For multi-frequency hopping, enable `MULTI_SF_ENABLED` and configure the `EU868_FREQS[]` array.

## 🔄 OTA Updates

The gateway supports Over-The-Air firmware updates via WiFi.

### OTA Configuration

1. Make sure WiFi is configured correctly
2. Configure hostname and password in `config.h`:
   ```cpp
   #define OTA_HOSTNAME "esp32-gateway"
   #define OTA_PASSWORD "admin"
   ```

### Upload firmware via OTA

**Method 1: PlatformIO**
```bash
# Configure upload_port in platformio.ini with gateway IP
# Then:
platformio run --target upload
```

**Method 2: Arduino IDE**
1. Install ArduinoOTA library
2. Go to **Sketch → Upload Using Network**
3. Select device `esp32-gateway.local` or gateway IP

**Method 3: Web Browser**
1. Open `http://esp32-gateway.local` or `http://<GATEWAY_IP>`
2. Upload the `.bin` firmware file

### OTA Monitoring

During update:
- The display shows progress
- Serial monitor shows status
- Radio is stopped during update

## 🐛 Debug and Troubleshooting

### Gateway doesn't connect to ChirpStack

1. **Check server IP and port** in `config.h`
2. **Check firewall**: UDP port 1700 must be open
3. **Verify Gateway ID**: Must exactly match the one in ChirpStack
4. **Check serial logs**: Look for `[UDP]` and `[PULL]` messages
5. **Test connection**: Try pinging the ChirpStack server from the network

### No packets received

1. **Check LoRa frequency**: Must match the node's frequency
2. **Check Spreading Factor**: Must be compatible (SF7-SF12)
3. **Check antenna**: Must be properly connected
4. **Check distance**: <1km for single-channel gateway
5. **Check power**: Check `LORA_OUTPUT_POWER` (recommended 14-22 dBm)
6. **Check serial statistics**: Look for `[STATS]` to see CRC/timeout errors

### Display doesn't work

1. **Check I2C pins**: For Heltec V4 use hardware I2C pins
2. **Check power**: Display requires VEXT power supply
3. **Try disabling**: `#define DISPLAY_ENABLED false` to test

### Downlink doesn't work

1. **Check configuration**: `AUTO_DOWNLINK_ENABLED` must be `true`
2. **Check LoRaWAN keys**: Must exactly match the node
3. **Check DevAddr**: `LORAWAN_DEVADDR` must match or be `0`
4. **Check timing**: RX1_DELAY and RX2_DELAY must be correct
5. **Check logs**: Look for `[PULL]` and `[DOWNLINK]` messages in serial

### Serial Statistics

The gateway prints statistics every 2 minutes:
```
[STATS] ===== GATEWAY STATUS =====
[STATS] Uptime: 3600 s
[STATS] Total interrupts: 150
[STATS] OK packets: 120
[STATS] CRC errors: 5
[STATS] Timeout: 10
[STATS] Other errors: 15
[STATS] Radio listening: YES
[STATS] WiFi: OK
```

**Interpretation:**
- **Total interrupts**: Number of radio interrupts received
- **OK packets**: Packets received and validated correctly
- **CRC errors**: Packets with wrong CRC (noise/interference)
- **Timeout**: Timeout during reception
- **Other errors**: Other radio errors

## 📦 Dependencies

The project uses the following libraries (automatically managed by PlatformIO):

- **RadioLib** (^6.6.0): Full support for SX1262
- **ArduinoJson** (^6.21.0): JSON parsing for Semtech UDP protocol
- **U8g2** (^2.35.0): SSD1306 OLED display driver
- **ArduinoOTA**: Over-The-Air firmware updates

## 🧪 Test Node

The project includes a `test_node/` directory with a LoRaWAN test node to verify gateway operation.

For more information, see `test_node/README.md`.

## 📝 TODO / Roadmap

- [x] Downlink support (TX from ChirpStack) - **Implemented (Class C)**
- [x] OTA updates - **Implemented**
- [x] Advanced statistics - **Implemented**
- [ ] Complete multi-frequency hopping
- [ ] Adaptive Data Rate (ADR)
- [ ] Web UI for configuration
- [ ] Multi-node support for downlink (DevAddr → Keys database)
- [ ] Full LoRaWAN 1.1 support
- [ ] Class B support (beacon timing)

## 📚 Architecture

### Semtech UDP Protocol

The gateway implements the Semtech UDP protocol to communicate with ChirpStack:

- **PUSH_DATA** (0x00): Send received uplink packets
- **PUSH_ACK** (0x01): Confirm PUSH_DATA reception
- **PULL_DATA** (0x02): Periodic downlink request
- **PULL_RESP** (0x03): Response with downlink to transmit
- **PULL_ACK** (0x04): Confirm PULL_DATA reception
- **TX_ACK** (0x05): Confirm downlink transmission

### Packet Structure

UDP packets follow the Semtech format:
```
[Version:1][Token:2][Identifier:1][GatewayID:8][JSON:variable]
```

See `src/TypeDef.h` for complete packet definitions.

### Gateway ID

The Gateway ID is automatically generated from the MAC address:
- Format: `MAC[0:2]:FF:FF:MAC[3:5]`
- Example: `AA:BB:CC:DD:EE:FF` → `AABBCCFFFFDDEEFF`
- Implementation: `src/common.h::generateGatewayId()`

## 🔒 Security

**Important**: 
- LoRaWAN keys in `config.h` are **sensitive** and must not be committed
- Use `.gitignore` to exclude `config.h` or use environment variables
- For production, consider using NVS (Non-Volatile Storage) for keys

## 🤝 Contributing

Contributions welcome! 

1. Fork the project
2. Create a branch for your feature (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

## 📄 License

MIT License - See LICENSE file for details

## 🙏 Credits

- **RadioLib**: https://github.com/jgromes/RadioLib - Complete radio library for SX1262
- **Heltec**: https://heltec.org - Heltec WiFi LoRa 32 V4 hardware
- **ChirpStack**: https://www.chirpstack.io - LoRaWAN Network Server
- **PlatformIO**: https://platformio.org - IDE and build system

## 📞 Support

For issues or questions:
1. Check the [Debug and Troubleshooting](#-debug-and-troubleshooting) section
2. Check serial logs for error messages
3. Open an issue on GitHub with:
   - Problem description
   - Relevant serial logs
   - Hardware/software configuration

---

**Version**: 1.0.0  
**Last update**: 2024
