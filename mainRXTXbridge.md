# Hardware-in-the-Loop Serial Bridge: TXRXproto

## 1. Overview & System Role

The **`TXRXproto`** project (located in `C:\Users\egape\TXRXproto`) serves as the dedicated **Hardware-in-the-Loop (HIL) verification fixture and downstream client** for the ESP-NOW wireless network. 

Physically wired to the ESP32-C6 node's hardware UART serial bridge, `TXRXproto` provides immediate visual and serial feedback confirming that packets sent from the remote Coordinator successfully traverse the wireless ESP-NOW link, get dispatched across the node's serial bridge, and arrive uncorrupted at the downstream actuator.

```text
┌────────────────────────────────────────────────────────┐
│               PuTTY / Host PC Terminal                 │
└───────────────────────────┬────────────────────────────┘
                            │ Serial Console ("Kitchen blinkx 3") @ 115200 Baud
                            ▼
┌────────────────────────────────────────────────────────┐
│                Coordinator (ESP32-C6)                  │
└───────────────────────────┬────────────────────────────┘
                            │ ESP-NOW Wireless Transport (Wi-Fi 2.4 GHz / Ch 1)
                            │ 138-byte packed action frames / Sub-millisecond latency
                            ▼
┌────────────────────────────────────────────────────────┐
│                  PNPzigbee (ESP32-C6)                  │
│            serial_bridge driver (UART1)                │
└───────────────────────────┬────────────────────────────┘
                            │ Hardware Serial (115200 Baud / 8-N-1)
                            │ GPIO 17 (TX) / GPIO 16 (RX)
                            ▼
┌────────────────────────────────────────────────────────┐
│                TXRXproto (SAMD21 / M0)                 │
│   • Built-in LED Heartbeat (4 blinks + pause)          │
│   • DotStar Blue 'blinkx' LED (APA102)                 │
│   • Hardware 10-bit DAC Output (Pin A0)                │
│   • Bi-directional Serial Echo / Ping Engine           │
└────────────────────────────────────────────────────────┘
```

---

## 2. ESP-NOW Wireless Protocol Architecture

The wireless transport layer operates entirely on **Espressif ESP-NOW**, completely replacing legacy Zigbee 3.0 / Zboss libraries. This transition eliminates massive stack overhead, removes joining delays, and drastically improves packet speed and reliability.

### 2.1 Why ESP-NOW Replaced Zigbee
- **Zero Stack Overhead:** Zigbee required hundreds of kilobytes of closed/proprietary Zboss libraries, software cryptographic workarounds, and complex multi-layer APS/ZDO packet wrappers. ESP-NOW communicates directly at the IEEE 802.11 MAC layer.
- **Ultra-Low Latency:** Packets transmit in **< 1 ms** without routing lookups, route rediscovery delays, or Zigbee beaconing.
- **Hardware Cryptography:** Eliminating Zboss allowed re-enabling native ESP32-C6 hardware cryptographic acceleration (AES, SHA, MPI).
- **No Wi-Fi Infrastructure Required:** ESP-NOW transmits standard vendor-specific action frames directly peer-to-peer. No Wi-Fi access point, router, DHCP handshake, or IP configuration is needed.

### 2.2 Channel & Radio Configuration
- **Frequency Band:** 2.4 GHz Wi-Fi.
- **Operating Channel:** Fixed to **Channel 1** (`ESPNOW_WIFI_CHANNEL = 1`, `WIFI_SECOND_CHAN_NONE`).
- **Wi-Fi Mode:** Station Mode (`WIFI_MODE_STA`) with power-save disabled (`WIFI_PS_NONE`) for maximum responsiveness.
- **Protocol Flags:** 802.11b/g/n enabled.

### 2.3 Frame Structure (`espnow_frame_t`)
All wireless messages share a unified, 138-byte packed structure (`espnow_frame_t`) guaranteeing binary interoperability between the Coordinator and all remote nodes:

```c
#define MESH_TEXT_LEN 18
#define MESH_LINE_LEN 80

typedef struct __attribute__((packed))
{
    char source[MESH_TEXT_LEN];   // Origin node name (e.g. "coordinator", "Kitchen")
    char target[MESH_TEXT_LEN];   // Target destination node name (e.g. "Kitchen")
    char cmd[MESH_TEXT_LEN];      // Command verb (e.g. "blink", "SetupSerialBridge")
    int16_t value;                // Primary numeric parameter
    int16_t value2;               // Secondary numeric parameter
    char text[MESH_LINE_LEN];     // Raw text line or return reply string
} espnow_frame_t;
```

### 2.4 Addressing & Peer Discovery
- **Unicast Addressing:** Packets are transmitted directly to the node's 6-byte IEEE 802.3 MAC address:
  - **Coordinator (ESP32-C6 DevKit):** `B4:3A:45:8A:C7:18`
  - **Kitchen Node (Seeed XIAO ESP32-C6):** `B4:3A:45:8A:C6:40`
- **Dynamic Peer Addition:** When the Coordinator dispatches to a target, it checks `esp_now_is_peer_exist()` and automatically registers the peer (`esp_now_add_peer`) on Channel 1 without user intervention.
- **Coordinator Auto-Learning:** Remote nodes automatically record the Coordinator's MAC address from the first frame received and direct all upstream responses back to that MAC.
- **Broadcast Fallback:** The broadcast address `FF:FF:FF:FF:FF:FF` is used for discovery sweeps (`PingNetwork`) or unlisted devices.

### 2.5 Link Quality Assessment (RSSI to LQI Mapping)
Every received packet extracts physical RSSI (dBm) from the hardware `rx_ctrl->rssi` header and calculates an equivalent Link Quality Indicator (0–255 LQI):

$$\text{LQI} = \text{clamp}\left(\frac{(\text{RSSI} + 100) \times 255}{70},\ 0,\ 255\right)$$

- When a node receives a `PING`, it measures local LQI and replies with:
  `PONG LQI=<remote_lqi>`
- The Coordinator records both local incoming LQI and the remote reported LQI, providing true bidirectional link quality in `GiveNetworkReport`.

### 2.6 Seeed Studio XIAO ESP32-C6 RF Switch Control
The Seeed Studio XIAO ESP32-C6 hardware includes an onboard RF multiplexer switch that requires explicit GPIO initialization before 2.4 GHz transmission can occur:
- **GPIO 3:** RF Switch Power Enable (Active-LOW). Must be driven `0` (LOW) to power the RF switch.
- **GPIO 14:** Antenna Path Select. Must be driven `0` (LOW) to select the onboard ceramic antenna (`1` selects external U.FL).

---

## 3. Hardware & Platform Specifications

| Parameter | Specification | Notes |
| :--- | :--- | :--- |
| **Microcontroller** | Microchip / Atmel ATSAMD21E18A | 32-bit ARM Cortex-M0+ @ 48 MHz |
| **Board Profile** | Adafruit Trinket M0 / ItsyBitsy M0 Express | `[env:adafruit_trinket_m0]` |
| **Framework** | Arduino via PlatformIO (`atmelsam`) | Embedded C++ |
| **Operating Voltage** | 3.3V Logic | Direct logic-level compatibility with ESP32-C6 |
| **Interface Ports** | Dual Serial (USB CDC + Hardware UART1) | Concurrent monitoring and bridge routing |
| **DAC Pin** | **`A0`** (PA02) | 10-bit true hardware DAC (0–3.3V) |

---

## 4. Physical Wiring Reference

Connect the ESP32-C6 node to the Arduino test fixture as follows:

| ESP32-C6 Pin | Signal Direction | If using **Trinket M0** | If using **ItsyBitsy M0** | Function |
| :--- | :---: | :--- | :--- | :--- |
| **GPIO 17** | &rarr; | **Pin 3** (RX) | **Pin 0** (RX) | Hardware UART TX from ESP32-C6 to Arduino |
| **GPIO 16** | &larr; | **Pin 4** (TX) | **Pin 1** (TX) | Hardware UART RX from Arduino to ESP32-C6 |
| **GND** | &harr; | **GND** | **GND** | Common reference ground |
| *Optional: ADC* | &larr; | **Pin A0** | **Pin A0** | True analog DAC output wired to ESP32-C6 ADC |

---

## 5. Complete Coordinator Command Reference

All commands are typed directly into the Coordinator serial console (PuTTY or Python `test_runner.py` @ 115200 baud).

### 5.1 Coordinator Network Management Commands
These commands manage the ESP-NOW network table and do not require a device prefix:

| Command | Parameters | Description | Example Coordinator Output |
| :--- | :--- | :--- | :--- |
| **`GiveNetworkReport`** | *none* | Initiates a live RF survey. Pings all devices in the table, awaits PONG with remote LQI, and outputs a formatted status table. | `Kitchen,0xB43A...,0x0001,1,END_DEVICE,153,174,OK,2,YES` |
| **`PingNetwork`** | *none* | Dispatches a `PING` frame to all nodes registered in the static table to assess connectivity. | `PING Kitchen SENT SHORT=0x0001` |
| **`ResetNetwork`** | *none* | Clears cached link health and forces an immediate discovery ping sweep across all nodes. | `RESET_NETWORK_BEGIN`<br>`RESET_NETWORK_END` |
| **`ResetCoordinator`** | *none* | Performs a software restart (`esp_restart()`) of the Coordinator board. | `RESET_COORDINATOR_RESTARTING` |

---

### 5.2 Node Direct Commands (ESP32-C6 Local)
Format: `<TargetName> <Command> [Parameters]` *(e.g. `Kitchen blink 3`)*

| Command | Parameters | Description | Node Action / Response |
| :--- | :--- | :--- | :--- |
| **`<TargetName> PING`** | *none* | Direct RF link test to a specific node. | Node measures signal quality and replies: `< <TargetName>: [PONG LQI=<val> REMOTE_LQI=<val>]` |
| **`<TargetName> SetupSerialBridge`** | *none* | **Required prerequisite before downstream fixture testing.** Initializes UART1 (`GPIO 17 TX` / `GPIO 16 RX` @ 115200 baud) on the ESP32-C6. | ESP32-C6 installs UART driver and starts bridge task. Coordinator receives: `< <TargetName>: SETUP SERIAL OK` |
| **`<TargetName> blink [count] [ms]`** | `count` (1–20), `ms` (delay) | Blinks the ESP32-C6 **onboard status LED**. | ESP32-C6 toggles local onboard LED directly. |
| **`<TargetName> on`** | *none* | Turns on local default light output. | Node responds: `< <TargetName>: GOT ON OK` |
| **`<TargetName> off`** | *none* | Turns off local default light output. | Node responds: `< <TargetName>: GOT OFF OK` |

---

### 5.3 Downstream Fixture Commands (Wired Arduino / `TXRXproto`)
These commands traverse: **PC &rarr; Coordinator &rarr; ESP-NOW Wireless &rarr; ESP32-C6 &rarr; UART1 &rarr; Arduino**.  
*(Note: Always run `<TargetName> SetupSerialBridge` first so the UART bridge is active).*

| Command | Parameters | Downstream Action on Arduino | Expected Feedback / Confirmation |
| :--- | :--- | :--- | :--- |
| **`<TargetName> blinkx [count]`** | `count` (1–50, default 1) | Blinks the Arduino's onboard **DotStar addressable RGB LED** in **Blue** (`0x0000FF`) at 200ms cadence. | Arduino pulses Blue DotStar LED. Upstream terminal prints: `[Arduino] blinkx <count>`. |
| **`<TargetName> ping`** | *none* | **Full two-way loop verification.** Arduino receives `ping` on `Serial1` and immediately replies with `GotPing\r\n`. | ESP32-C6 forwards `GotPing` via ESP-NOW frame back to Coordinator. Console displays: `< <TargetName>: GotPing`. |
| **`<TargetName> setDAC <value>`** | `value` (0–1023) | Sets the hardware DAC on Arduino pin **`A0`** ($0\text{V} = 0$, $3.3\text{V} = 1023$). | Arduino sets pin `A0` and replies `GotDAC <value>`. Console displays: `< <TargetName>: GotDAC <value>`. |

---

## 6. Visual Status Indicators on the Arduino

The Arduino fixture provides two completely independent visual feedback mechanisms:

```text
┌────────────────────────────────────────────────────────┐
│                Visual Actuation Modes                  │
├──────────────────────────┬─────────────────────────────┤
│   Red Built-In LED       │   DotStar RGB LED           │
│   (LED_BUILTIN / Pin 13) │   (Internal APA102)         │
├──────────────────────────┼─────────────────────────────┤
│   Pattern:               │   Pattern:                  │
│   1000ms ON, 1000ms OFF  │   Pulsed Blue (0x0000FF)    │
│   Three 100ms pulses     │   200ms ON / 200ms OFF      │
│   (4 blinks + pause)     │                             │
├──────────────────────────┼─────────────────────────────┤
│   Indicates:             │   Indicates:                │
│   SAMD21 Core Liveness   │   Successful ESP-NOW        │
│   (Normal background)    │   'blinkx' Command Received │
└──────────────────────────┴─────────────────────────────┘
```

---

## 7. End-to-End Verification Procedure

Follow this checklist to verify your complete setup:

1. **Open Coordinator Terminal:** Connect to the Coordinator COM port at 115200 baud (or use `test_runner.py`).
2. **Verify RF Connectivity:**
   ```text
   GiveNetworkReport
   ```
   *Confirm `Kitchen` is listed as `ONLINE=1` with valid LQI.*
3. **Initialize the UART Bridge:**
   ```text
   Kitchen SetupSerialBridge
   ```
   *Wait for `< Kitchen: SETUP SERIAL OK` or `[ACK]`.*
4. **Test Two-Way Serial Round-Trip:**
   ```text
   Kitchen ping
   ```
   *Expected response:* `< Kitchen: GotPing`
5. **Test Visual Actuation:**
   ```text
   Kitchen blinkx 3
   ```
   *Verify that the Arduino's RGB DotStar LED flashes **Blue** 3 times.*
6. **Test Analog Hardware DAC:**
   ```text
   Kitchen setDAC 512
   ```
   *Expected response:* `< Kitchen: GotDAC 512` *(pin A0 outputs ~1.65V)*.
7. **Automated Regression Suite:**
   Run the test runner from `c:\Users\egape\ZigbeeTestRunner`:
   ```bash
   python test_runner.py --port COM11 --target Kitchen --all
   ```

---

## 8. To Be Done (Future Enhancements & Roadmap)

### 8.1 Channel Agility & Auto-Scanning

#### Context & Motivation
Once all peripheral drivers, actuators, and communication features are finalized, end-device nodes (Seeed Studio XIAO ESP32-C6) will be physically embedded inside objects, enclosures, and appliances. In these deployed environments:
- End-devices will not have accessible USB/UART debug ports or physical buttons for manual channel configuration.
- The Coordinator may be configured on different 2.4 GHz channels (Channels 1–11) to avoid interference from surrounding home/industrial Wi-Fi access points.
- Therefore, embedded end-devices must have autonomous channel agility to automatically discover, track, and lock onto the Coordinator's current operating frequency without human intervention.

#### Proposed Channel Agility Architecture
1. **Coordinator Periodic Beacon:**
   - The Coordinator transmits a lightweight broadcast heartbeat/beacon frame on its active channel at regular intervals (e.g., once every 1–2 seconds) or responds to active discovery probes.
2. **Autonomous Node Scanning State Machine:**
   - **Boot & Loss-of-Link Detection:** If an embedded node powers on or loses link connectivity with the Coordinator (e.g., no valid packet received for 10–15 seconds), it transitions into `SCANNING` mode.
   - **Multi-Channel Sweep:** The node sequentially steps its radio through Wi-Fi Channels 1 through 11 (or 13):
     ```c
     esp_wifi_set_channel(scan_channel, WIFI_SECOND_CHAN_NONE);
     ```
   - **Dwell & Probe:** The node dwells on each channel for a defined interval (e.g., 200–300 ms) listening for the Coordinator's beacon, or broadcasts a rapid `DISCOVERY_PROBE` frame to `FF:FF:FF:FF:FF:FF`.
   - **Channel Lock & Join:** Upon receiving a valid beacon or probe-response from the Coordinator, the node locks its radio to that channel, updates its internal peer table, and resumes normal operation.
3. **NVS Channel Caching:**
   - When a channel lock is achieved, the node persists the active channel number to Non-Volatile Storage (NVS flash).
   - On subsequent power cycles, the node first attempts to connect on the cached channel before initiating a full 11-channel sweep, minimizing boot-to-link time to under 50 ms.
4. **Coordinated Channel Migration:**
   - If the Coordinator is instructed to shift channels (e.g., via a serial command `SetChannel <new_ch>`), it broadcasts a `CHANNEL_SWITCH_ANNOUNCEMENT <new_ch>` frame with a short countdown.
   - Active nodes receive this announcement and migrate their radios synchronously with the Coordinator.
   - Any sleeping or disconnected node that misses the announcement will simply time out and re-acquire the Coordinator via autonomous channel scanning.

