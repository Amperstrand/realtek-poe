# GS1900-8HP PoE Protocol Analysis

## Hardware

| Component | Details |
|---|---|
| Switch SoC | Realtek RTL8380M rev C |
| PoE PSE Controller | Broadcom BCM59121B0KMLG |
| Management MCU | ST Micro STM32F100C8 (firmware v17.1) |
| Device ID | 0xe111 (57617 decimal) |
| Power Budget | 70W configurable (63W usable with 10% guard band) |
| Ports | 8x PoE+ (802.3at), all with class-based power limiting |
| UART Interface | `/dev/ttyS1` at 19200 baud (8N1) |
| PoE Enable GPIO | `/sys/firmware/devicetree/base/switchcore@1b000000/mdio-aux/expander@0/poe_enable` |

## UART Protocol

### Frame Format

All frames are **12 bytes**. Format:

```
Byte  0:    Command ID (wire dialect-specific)
Byte  1:    Sequence number (incrementing, wraps at 255)
Bytes 2-10: Payload (unused bytes set to 0xFF)
Byte  11:   Checksum = sum(bytes[0..10]) & 0xFF
```

### Checksum Algorithm

Simple 8-bit additive checksum:
```c
cmd[11] = 0;
for (i = 0; i < 11; i++)
    cmd[11] += cmd[i];
```

Verified against all captured frames — all checksums match.

### Error Responses

When the MCU rejects a command, it replies with:
```
Byte 0: 0xF0 | error_code
Bytes 1-10: 0xFF (no data)
Byte 11: checksum
```

Known error codes:
| Code | Meaning |
|---|---|
| 0x0D | request-incomplete |
| 0x0E | request-bad-checksum |
| 0x0F | not-ready |

### Timing

- MCU takes **~20 seconds** after power-on before accepting commands
- During this window, all commands get `request-bad-checksum` or no response
- After initialization, commands succeed with ~10-50ms round-trip latency
- Daemon polls every 1 second (state_timeout_cb)

## Broadcom Dialect Commands (GS1900-8HP)

The GS1900-8HP uses the **Broadcom dialect** (default in realtek-poe). This was verified:
- Board compatible: `zyxel,gs1900-8hp-a1`
- No `force_dialect` in UCI config → defaults to Broadcom
- All observed wire command IDs match Broadcom mapping

### Command Reference

| Wire ID | Enum | Function | Flags |
|---|---|---|---|
| 0x00 | PORT_ENABLE | Enable/disable port | 1 |
| 0x02 | MCU_ENABLE_PORT_MAPPING | Enable port mapping | 1 |
| 0x10 | PORT_SET_DETECTION_TYPE | Set detection method | CMD_HAS_ALL_PORT |
| 0x11 | PORT_ENABLE_CLASSIFICATION | Enable IEEE classification | CMD_IS_4PORT |
| 0x13 | PORT_SET_DISCONNECT_TYPE | Set disconnect method | CMD_HAS_ALL_PORT |
| 0x15 | PORT_SET_POWER_LIMIT_TYPE | Set power limit mode | CMD_IS_4PORT |
| 0x16 | PORT_SET_POWER_LIMIT | Set per-port power limit | 1 |
| 0x17 | MCU_SET_POWER_MGMT_MODE | Set power management mode | 1 |
| 0x18 | MCU_SET_POWER_BUDGET | Set global budget + guard | 1 |
| 0x1A | PORT_SET_PRIORITY | Set port priority (0-3) | CMD_IS_4PORT |
| 0x1C | PORT_SET_POE_MODE | Set PoE mode per 4-port group | CMD_IS_4PORT |
| **0x20** | **MCU_GET_SYSTEM_INFO** | Get MCU info | 1 |
| **0x21** | **PORT_GET_STATUS** | Get port status | 1 |
| **0x23** | **MCU_GET_POWER_STATS** | Get global power stats | 1 |
| **0x25** | **PORT_GET_CONFIG** | Get port configuration | 1 |
| **0x26** | **PORT_GET_EXT_CONFIG** | Get extended port config | 1 |
| **0x28** | **PORT_GET_SHORT_STATUS** | Get 4-port group status | CMD_IS_4PORT |
| **0x2B** | **MCU_GET_EXT_CONFIG** | Get extended system config | 1 |
| **0x30** | **PORT_GET_POWER_STATS** | Get per-port power stats | 1 |

### CMD_IS_4PORT Flag

Commands with `CMD_IS_4PORT` flag pack 4 port IDs into bytes [2,3,4,5] and 4 values into bytes [6,7,8,9]. The MCU processes all 4 ports in a single command.

## Reply Structures (Decoded from Hardware)

### MCU_GET_SYSTEM_INFO (0x20) Reply

```
Byte 0:  0x20 (command echo)
Byte 1:  sequence number
Byte 2:  mode (0=Semi-auto I2C, 1=Semi-auto UART, 2=Auto I2C, 3=Auto UART)
Byte 3:  num_detected_ports (8 for GS1900-8HP)
Byte 4:  port_map_en (0=disabled)
Bytes 5-6: device_id (big-endian) = 0xE111
Byte 7:  sys_version (17 → "v17.x")
Byte 8:  mcu_type (0=STM32F100, 1=Nuvoton M05xx, 2=STF030C8, 3=Nuvoton M058SAN, 4=Nuvoton NUC122)
Byte 9:  sys_status (bitfield: Global Disable pin, system reset, config dirty/saved)
Byte 10: sys_ext_version (1 → "v17.1")
Byte 11: checksum
```

Our device: mode=0 (Auto UART... wait, mode=0 is Semi-auto I2C). Actually our captured reply was byte[2]=0x00 which maps to "Semi-auto I2C". Need to verify this mapping.

### MCU_GET_POWER_STATS (0x23) Reply

```
Byte 0:    0x23 (command echo)
Byte 1:    sequence number
Bytes 2-3: power_consumption (big-endian, ×0.1 W) = 0x0000 → 0.0W
Bytes 4-5: reported_power_budget (big-endian, ×0.1 W) = 0x0276 → 63.0W
Bytes 6-7: guard_band? (0x0002 → 0.2W)
Bytes 8-10: unused (0xFF)
Byte 11:  checksum
```

### PORT_GET_STATUS (0x21) Reply

```
Byte 0:  0x21 (command echo)
Byte 1:  sequence number
Byte 2:  port index (0-based)
Byte 3:  class_info (1=class0, 2=class1, ..., maps to IEEE 802.3af/at classes)
Byte 4:  pd_type (0x01=normal, 0x06=different — port 7 anomaly)
Byte 5:  mpss_mask
Bytes 6-7: unknown
Bytes 8-10: unused (0xFF)
Byte 11: checksum
```

### PORT_GET_CONFIG (0x25) Reply

```
Byte 0:  0x25 (command echo)
Byte 1:  sequence number
Byte 2:  port index
Byte 3:  enabled (1=yes, 0=no)
Byte 4:  auto_powerup (0=no)
Byte 5:  detection_type (3=?)
Byte 6:  classification_enable (1=yes)
Byte 7:  disconnect_type (2=?)
Byte 8:  pair (0=?)
Bytes 9-10: unused (0xFF)
Byte 11: checksum
```

### PORT_GET_SHORT_STATUS (0x28) Reply (4-port)

```
Byte 0:  0x28 (command echo)
Byte 1:  sequence number
Byte 2:  port index of first port in group
Byte 3:  status port+0 (0x11 = "Searching")
Byte 4:  port index of second port
Byte 5:  status port+1
Byte 6:  port index of third port
Byte 7:  status port+2
Byte 8:  port index of fourth port
Byte 9:  status port+3
Byte 10: 0xFF (unused)
Byte 11: checksum
```

Status values observed: 0x11 = "Searching" (no PD connected), 0x61 = seen on port 7

### PORT_GET_POWER_STATS (0x30) Reply

```
Byte 0:    0x30 (command echo)
Byte 1:    sequence number
Byte 2:    port index
Bytes 3-4: unknown field A (big-endian) — 0x0344 (836) with NR7101 load, 0x0000 idle
Bytes 5-6: unknown field B (big-endian) — 0x005D (93) with NR7101 load, 0x0000 idle
Bytes 7-8: port max capability (big-endian, ×0.1 W) — 0x00BC (18.8W), CONSTANT for all ports
Bytes 9-10: consumed power (big-endian, ×0.1 W) — 0x0032 (5.0W) with NR7101 load ✓
Byte 11:  checksum
```

**CORRECTION (2026-05-22)**: The previous version incorrectly mapped bytes 3-4 as "consumed_power" and bytes 5-6 as "allocated_power". The correct mapping has consumed power at bytes 9-10, confirmed by matching ubus output (5.0W) and stock web UI (4700-5200mW). The fields at bytes 3-6 are still under investigation.

Note: Ports 0,1,2,3,5,6 show max=0x00BC (18.8W). Ports 4,7 show 0x00BD (18.9W) — slight calibration difference.

## ZyXEL Stock V2.90 Comparison

### Stock Web UI PoE Pages

| Page | cmd | Content |
|---|---|---|
| Monitor → PoE | 776 | Total/Consuming/Allocated/Remaining power in watts |
| Config → PoE Global | 771 | PoE Mode: Classification vs Consumption toggle |
| Config → PoE Port | 773 | Per-port: State, Class, Priority, Power-Up mode, Consuming (mW), Max (mW), Time Range |
| Port Edit (submit) | 774 | Edit selected ports |

### Stock PoE Monitor (cmd=776) Values

| Field | Value |
|---|---|
| PoE Mode | Consumption |
| Total Power | 70.0W |
| Consuming Power | 0.0W |
| Allocated Power | 0.0W |
| Remaining Power | 70.0W |

### Stock Per-Port Config (cmd=773) Values

All 8 ports show: State=Enable, Class=class0, Priority=Low, Power-Up=802.3at, Consuming=0mW, Max=0mW, TimeRange=-

### Key Differences from OpenWrt

1. **Budget display**: Stock shows 70.0W total. OpenWrt configures 70W but MCU reports 63.0W (10% guard band). Stock either doesn't apply a guard band or calculates it differently.

2. **Per-port power measurement**: Stock shows per-port power in milliwatts. OpenWrt's `poe info` DOES expose per-port wattage (consumption field) when a port is delivering power. Verified with NR7101 load: shows 4.7-5.2W for lan8.

3. **Power allocation**: Stock shows "Allocated Power" and "Remaining Power" — these track how much power is reserved for connected devices. OpenWrt only shows consumed vs budget.

4. **PoE Mode**: Stock has Classification vs Consumption mode toggle. OpenWrt sets mode=2 at init (meaning unknown, likely "Consumption").

5. **Time scheduling**: Stock supports Time Range scheduling per port. OpenWrt has no equivalent.

6. **Per-port max power**: Stock shows 16200mW max for active port, 0mW for idle. OpenWrt's debug shows 15.4W per-port budget and 18.8W max (from UART bytes 7-8).

## Identified Bugs

### 1. Boot-Time Race Condition

**Severity**: Medium (cosmetic, self-recovering)
**Evidence**: All MCU commands rejected with `request-bad-checksum` for ~20 seconds after power-on.

The `realtek-poe` daemon starts via procd before the STM32F100 MCU is ready. The init sequence (`poe_initial_setup`) sends setup commands (set mode, set budget, enable ports) that all fail. After ~20 seconds, the MCU starts responding and the 1-second polling loop eventually succeeds. But the initial setup (power management mode, budget, port configuration) may not have been applied correctly.

**Fix proposal**: Add a startup delay or detect MCU readiness before sending init commands. The MCU responds with `0x0F` (not-ready) when it's still initializing — this could be used as a readiness check.

### 2. Per-Port Power Measurement Not Exposed

**Severity**: High (feature gap vs stock firmware)
**Evidence**: `PORT_GET_POWER_STATS` (0x30) returns per-port consumed/allocated/max power, but this data is only available through `poe debug`, not `poe info`.

The Broadcom dialect's polling uses `PORT_GET_SHORT_STATUS` (0x28) for status updates, which doesn't return power data. `PORT_GET_POWER_STATS` (0x30) is called during init but not during the regular polling loop. And even when called, the reply handler `poe_reply_port_power_stats()` populates `state->ports[i].watt` but the `ubus_poe_info_cb` only adds consumption to the output if `watt` is non-zero.

**Fix proposal**: 
1. Add per-port `PORT_GET_POWER_STATS` to the Broadcom dialect's polling loop
2. Always include per-port consumption in `poe info` output (even when 0)
3. Add per-port max_power and allocated_power to `poe info`

### 3. Budget Guard Band Discrepancy

**Severity**: Low (correct behavior, confusing display)
**Evidence**: Config says 70W, MCU reports 63W via `MCU_GET_POWER_STATS`.

The daemon applies `budget_guard = budget / 10 = 7.0W` and sends `budget - guard = 63.0W` to the MCU. The stock firmware shows 70W available. This might be because stock doesn't apply a guard band, or applies a different one.

**Investigation needed**: Compare actual budget management behavior under load. Does OpenWrt cut power at 63W or 70W? Does stock cut at 70W exactly?

### 4. Port 7 pd_type Anomaly

**Severity**: Low (possibly hardware difference)
**Evidence**: Port 7 (lan8) reports `pd_type=6` while all other ports report `pd_type=1`.

This might indicate port 8 has different hardware characteristics (different PSE channel?), or it's a firmware quirk. The stock firmware doesn't expose this field so we can't compare directly.

## Open Questions

1. What does `power_mgmt_mode=2` mean? (Set during init: `poe_cmd_power_mgmt_mode(mcu, 2)`)
2. What does `detection_type=3` mean? (Set during port config)
3. What does `disconnect_type=2` mean?
4. What are the 4-port short status byte values? (0x11=Searching, others=?)
5. Does the stock firmware use the same UART protocol at the same baud rate?
6. What happens to power management when budget is exceeded?
7. How does Classification mode vs Consumption mode differ in practice?

## Load Testing Results (NR7101)

### Test Setup

- NR7101 (Zyxel 5G router, OpenWrt, MAC 4c:c5:3e:b6:1d:90) connected to port 8 on BOTH GS1900-8HP devices
- NR7101 configured at 192.168.1.10/24
- Both switches powered from same source

### Stock vs OpenWrt Power Comparison (Port 8, NR7101 load)

| Metric | Stock V2.90 (192.168.1.1) | OpenWrt (192.168.1.2) |
|---|---|---|
| Total Budget | 70.0W | 70.0W (config) / 63.0W (MCU reported) |
| Total Consumption | 4.7-5.2W | 4.9-5.3W |
| Port 8 Consumption | 4600-5200mW | 4.7-5.2W |
| Port 8 Max | 16200mW | 18.8W (UART) / 15.4W (debug) |
| Port 8 Class | class0 | class2 (UART) |
| Port 8 Power-Up Mode | 802.3at | (not exposed) |
| Remaining | 64.8-65.3W | 64.7-65.1W |

NOTE: The ~0.5W difference between stock and OpenWrt readings may be due to different firmware versions on the two GS1900-8HP devices (user confirmed they run slightly different firmware).

### Port Disable/Enable Test Results

**OpenWrt** (ubus call poe manage):
- Command: `ubus call poe manage '{"port":"lan8", "enable": false}'`
- Note: Returns "Method not found" or "Parsing message data failed" but STILL WORKS (daemon processes it)
- Result: Port → "Disabled", consumption → 0.0W, NR7101 loses power
- Re-enable: `ubus call poe manage '{"port":"lan8", "enable": true}'`
- Recovery: Port → "Delivering power", consumption → 5.1W after ~8s (PD renegotiation time)
- NR7101 recovers in ~5 seconds after re-enable

**Stock V2.90** (HTTP POST to cmd=775):
- Form requires XSSID token (CSRF protection), fetched from cmd=774 page
- POST fields: `XSSID=...&portlist=8&state=0|1&portPriority=3&portPowerMode=3&portLimitMode=0&portPowerLimit=0&poeTimeRange=20&cmd=775&sysSubmit=Apply`
- Disable (state=0): Port → "Disable", consumption → 0mW, total → 0.0W
- Re-enable (state=1): Port → "Enable", consumption → 4900mW after ~8s
- NR7101 stays reachable during test (it's connected to BOTH devices via different physical paths)

### UART Raw Data (Fresh Capture)

From `logread | grep "realtek-poe"` after daemon restart with NR7101 load on port 7 (lan8):
```
Port 7 (lan8 with load): RX <- 30 56 07 03 44 00 5d 00 bc 00 32 1f
Port 3 (idle):           RX <- 30 4a 03 00 00 00 00 00 bc 00 00 39
Port 4 (idle):           RX <- 30 4d 04 00 00 00 00 00 bc 00 00 3d
Port 5 (idle):           RX <- 30 50 05 00 00 00 00 00 bc 00 00 41
Port 6 (idle):           RX <- 30 53 06 00 00 00 00 00 bc 00 00 45
```

## Test Plan (Requires PoE Loads)

- [x] Connect 802.3af class 2 device (NR7101, ~5W) to both devices
- [x] Verify per-port power measurement accuracy (stock mW vs OpenWrt W)
- [ ] Connect 802.3at class 4 device (e.g., PoE access point, ~25W)
- [ ] Test budget overload: connect 8× 15W devices (>70W total)
- [ ] Verify priority-based power shedding
- [x] Test hot-plug: connect/disconnect while monitoring (port disable/enable)
- [ ] Compare Classification mode vs Consumption mode behavior
- [ ] Measure actual power draw with external meter for accuracy comparison
