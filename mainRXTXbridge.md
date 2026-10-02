# Hardware-in-the-Loop Serial Bridge: TXRXproto

## 1. Overview & System Role

The **`TXRXproto`** project (located in `C:\Users\egape\TXRXproto`) serves as the dedicated **Hardware-in-the-Loop (HIL) verification fixture and downstream client** for the Zigbee mesh network. 

Physically wired to the ESP32-C6 node's hardware UART serial bridge, `TXRXproto` provides immediate visual and serial feedback confirming that packets sent from the remote coordinator successfully traverse the wireless Zigbee mesh, get dispatched across the node's serial bridge, and arrive uncorrupted at the end-actuator.

```text
┌────────────────────────────────────────────────────────┐
│               PuTTY / Host PC Terminal                 │
└───────────────────────────┬────────────────────────────┘
                            │ Serial Console ("Kitchen blinkx 3")
                            ▼
┌────────────────────────────────────────────────────────┐
│                Coordinator (ESP32-C6)                  │
└───────────────────────────┬────────────────────────────┘
                            │ Zigbee 3.0 Mesh (IEEE 802.15.4 / APS 0xFFC0)
                            ▼
┌────────────────────────────────────────────────────────┐
│                  PNPzigbee (ESP32-C6)                  │
│                serial_bridge (UART1)                   │
└───────────────────────────┬────────────────────────────┘
                            │ Hardware Serial (115200 Baud / 8-N-1)
                            ▼
┌────────────────────────────────────────────────────────┐
│                TXRXproto (SAMD21 / M0)                 │
│   • Built-in LED Heartbeat (4 blinks + pause)          │
│   • DotStar Blue 'blinkx' LED                          │
│   • Hardware 10-bit DAC Output (Pin A0)                │
│   • Bi-directional Serial Echo / Ping Engine           │
└────────────────────────────────────────────────────────┘
```

---

## 2. Hardware & Platform Specifications

| Parameter | Specification | Notes |
| :--- | :--- | :--- |
| **Microcontroller** | Microchip / Atmel ATSAMD21E18A | 32-bit ARM Cortex-M0+ @ 48 MHz |
| **Board Profile** | Adafruit Trinket M0 / ItsyBitsy M0 Express | `[env:adafruit_trinket_m0]` |
| **Framework** | Arduino via PlatformIO (`atmelsam`) | Embedded C++ |
| **Operating Voltage** | 3.3V Logic | Direct logic-level compatibility with ESP32-C6 |
| **Interface Ports** | Dual Serial (USB CDC + Hardware UART1) | Concurrent monitoring and bridge routing |
| **DAC Pin** | **`A0`** (PA02) | 10-bit true hardware DAC (0–3.3V) |

---

## 3. Physical Wiring Reference

Connect the ESP32-C6 node to the Arduino test fixture as follows:

| ESP32-C6 Pin | Signal Direction | If using **Trinket M0** | If using **ItsyBitsy M0** | Function |
| :--- | :---: | :--- | :--- | :--- |
| **GPIO 17** | &rarr; | **Pin 3** (RX) | **Pin 0** (RX) | Hardware UART TX from ESP32-C6 to Arduino |
| **GPIO 16** | &larr; | **Pin 4** (TX) | **Pin 1** (TX) | Hardware UART RX from Arduino to ESP32-C6 |
| **GND** | &harr; | **GND** | **GND** | Common reference ground |
| *Optional: ADC* | &larr; | **Pin A0** | **Pin A0** | True analog DAC output wired to ESP32-C6 ADC |

---

## 4. Complete Coordinator Command Reference

All commands are typed directly into the Coordinator serial console (PuTTY or Python `test_runner.py` @ 115200 baud).

### 4.1 Coordinator Network Management Commands
These commands manage the Zigbee mesh itself and do not require a device prefix:

| Command | Parameters | Description | Example Coordinator Output |
| :--- | :--- | :--- | :--- |
| **`GiveNetworkReport`** | *none* | Initiates a live RF survey. Pings all online nodes, queries local and remote LQI, and outputs a formatted status table. | `Kitchen,0xB43A...,0x86A1,1,END_DEVICE,51,48,OK,4,NO` |
| **`PingNetwork`** | *none* | Broadcasts a `PING` to all known devices in the network table to measure link latency and LQI. | `PING Kitchen SENT SHORT=0x86A1` |
| **`ResetNetwork`** | *none* | Re-opens the Zigbee 3.0 permit-joining window for **180 seconds** (`esp_zb_bdb_open_network(180)`) and restarts multi-pass active discovery for known IEEE addresses. | `RESET_NETWORK_BEGIN`<br>`RESET_NETWORK_END` |
| **`ResetCoordinator`** | *none* | Performs a software restart (`esp_restart()`) of the Coordinator board. | `RESET_COORDINATOR_RESTARTING` |

---

### 4.2 Node Direct Commands (ESP32-C6 Local)
Format: `<TargetName> <Command> [Parameters]` *(e.g. `Kitchen blink 5`)*

| Command | Parameters | Description | Node Action / Response |
| :--- | :--- | :--- | :--- |
| **`<TargetName> PING`** | *none* | Direct RF link test to a specific node. | Node measures signal quality and replies: `< <TargetName>: PONG LQI=<val>` |
| **`<TargetName> SetupSerialBridge`** | *none* | **Required prerequisite before downstream fixture testing.** Initializes UART1 (`GPIO 17 TX` / `GPIO 16 RX` @ 115200 baud) on the ESP32-C6. | ESP32-C6 installs UART driver and starts bridge task. Coordinator receives: `< <TargetName>: SETUP SERIAL OK` |
| **`<TargetName> blink [count] [ms]`** | `count` (1-20), `ms` (delay) | Blinks the ESP32-C6 **onboard status LED**. | ESP32-C6 toggles local onboard LED directly. |
| **`<TargetName> on`** | *none* | Standard Zigbee On/Off cluster ON. | Node responds: `< <TargetName>: GOT ON OK` |
| **`<TargetName> off`** | *none* | Standard Zigbee On/Off cluster OFF. | Node responds: `< <TargetName>: GOT OFF OK` |

---

### 4.3 Downstream Fixture Commands (Wired Arduino / `TXRXproto`)
These commands traverse: **PC &rarr; Coordinator &rarr; Zigbee Over-The-Air &rarr; ESP32-C6 &rarr; UART1 &rarr; Arduino**.  
*(Note: Always run `<TargetName> SetupSerialBridge` first so the UART bridge is active).*

| Command | Parameters | Downstream Action on Arduino | Expected Feedback / Confirmation |
| :--- | :--- | :--- | :--- |
| **`<TargetName> blinkx [count]`** | `count` (1–50, default 1) | Blinks the Arduino's onboard **DotStar addressable RGB LED** in **Blue** (`0x0000FF`) at 200ms cadence. | Arduino pulses Blue DotStar LED. Upstream terminal prints: `[Arduino] blinkx <count>`. |
| **`<TargetName> ping`** | *none* | **Full two-way loop verification.** Arduino receives `ping` on `Serial1` and immediately replies with `GotPing\r\n`. | ESP32-C6 forwards `GotPing` via Zigbee APS message back to Coordinator. Console displays: `< <TargetName>: GotPing`. |
| **`<TargetName> setDAC <value>`** | `value` (0–1023) | Sets the hardware DAC on Arduino pin **`A0`** ($0\text{V} = 0$, $3.3\text{V} = 1023$). | Arduino sets pin `A0` and replies `GotDAC <value>`. Console displays: `< <TargetName>: GotDAC <value>`. |

---

## 5. Visual Status Indicators on the Arduino

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
│   SAMD21 Core Liveness   │   Successful Zigbee         │
│   (Normal background)    │   'blinkx' Command Received │
└──────────────────────────┴─────────────────────────────┘
```

---

## 6. End-to-End Verification Procedure

Follow this checklist to verify your complete setup:

1. **Open Coordinator Terminal:** Connect to the Coordinator COM port at 115200 baud.
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
