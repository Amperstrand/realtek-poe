# realtek-poe AI Experimentation Workspace

This is a fork of [Hurricos/realtek-poe](https://github.com/Hurricos/realtek-poe) used exclusively as an AI research and experimentation workspace for reverse-engineering, documenting, and improving PoE (Power over Ethernet) support on OpenWrt-managed switches.

## Branch Structure

| Branch | Purpose | Rules |
|---|---|---|
| `main` | Pristine mirror of upstream `realtek-poe` branch. **Never modify directly.** Requires PR + human approval. | Branch protection enabled. |
| `ai-experiments` | Default branch. All AI-generated work goes here. | AI agents work freely on this branch. |
| `realtek-poe` | Original upstream branch (preserved for reference). | Do not modify. |

## AI Agent Rules

### UPSTREAM INTERACTION — FORBIDDEN

**NEVER interact with the upstream repository (`Hurricos/realtek-poe`) in any way.** This includes:

- Do NOT create issues on `Hurricos/realtek-poe`
- Do NOT comment on issues or PRs on `Hurricos/realtek-poe`
- Do NOT submit pull requests to `Hurricos/realtek-poe`
- Do NOT fork from or push to `Hurricos/realtek-poe`
- Do NOT mention this AI workspace in any upstream communication

**Only humans may interact with the upstream project.** All findings from this workspace that merit upstream contribution must be reviewed and submitted by a human maintainer.

### BRANCH RULES

- **All AI work happens on `ai-experiments`** (the default branch).
- **Never commit to `main`.** It is a pristine mirror of upstream.
- **Never merge `ai-experiments` into `main`.** These are separate concerns.
- If `main` needs updating (upstream changes), a human does `git fetch upstream && git checkout main && git merge upstream/realtek-poe`.
- Create feature branches from `ai-experiments` for focused work (e.g., `ai-experiments/per-port-power`).

### RESEARCH METHODOLOGY

This workspace focuses on:

1. **Protocol reverse-engineering** — Understanding the Broadcom and Realtek MCU UART protocols
2. **Firmware comparison** — Comparing ZyXEL stock firmware PoE behavior with OpenWrt's realtek-poe daemon
3. **Bug identification** — Finding and documenting bugs with hardware-verified evidence
4. **Documentation** — Writing detailed protocol docs, device quirks, and hardware notes
5. **Proof-of-concept fixes** — Implementing fixes that can later be polished for upstream submission

### HARDWARE CONTEXT

We have access to the following test hardware:

- **GS1900-8HP A1** running OpenWrt (192.168.1.2, SSH access) — primary test device
- **GS1900-8HP A1** running ZyXEL stock V2.90 (192.168.1.1, HTTP access, password `Zyxel2026!`) — stock comparison device
- Both devices share: Broadcom BCM59121B0KMLG PoE PSE controller, ST Micro STM32F100C8 management MCU (firmware v17.1), 70W budget, 8 PoE+ ports

### SAFETY RULES

- **Never flash firmware to the test devices without explicit human approval.**
- **Never disable PoE on all ports simultaneously** (could strand powered devices).
- **Never change the power budget to 0** or values that could damage equipment.
- When testing PoE commands, prefer `ubus call poe sendframe` for one-off experiments rather than modifying the daemon source.
- Always document what you did and what happened in `research/notes/`.

### COMMIT CONVENTIONS

- Prefix commits with `ai:` to clearly mark AI-generated work (e.g., `ai: document broadcom dialect checksum algorithm`)
- Include hardware verification evidence where possible (log output, ubus responses)
- Keep commits small and focused — one concept per commit

## Project Context

- **Upstream**: https://github.com/Hurricos/realtek-poe (branch: `realtek-poe`)
- **Maintainer**: Alexandru Gagniuc (`mrnuke` on GitHub)
- **License**: GPL-2.0-or-later
- **Parent project**: [conwrt](https://github.com/Amperstrand/conwrt) — OpenWrt firmware flasher and device manager
- **OpenWrt packaging**: Included in `openwrt/openwrt` under `package/firmware/realtek-poe/`

## Key Source Files

| File | Description |
|---|---|
| `src/main.c` | Main daemon: UART communication, ubus API, state management, command queue |
| `src/dialect_bcm.c` | Broadcom dialect: wire command ID mapping (used by GS1900-8HP) |
| `src/dialect_rtl.c` | Realtek dialect: different command set + custom ops (init, poll, reset) |
| `src/tek-poe.h` | Shared header: data structures, dialect API, helper macros |
| `files/etc/config/poe` | Default UCI config: budget, port enable/priority |
| `files/etc/init.d/poe` | procd init script |

## Known Issues Under Investigation

See `research/` directory for detailed findings.

### Upstream Issue #32 — 802.3bt and Paired Port Support

**Status**: Blocked — no BCM59121 hardware to test on.

**Problem**: 802.3bt (4-pair) devices don't power up on BCM59121-based switches (e.g., Netgear GS110TUP). Standard 802.3af/at (2-pair) works fine. The issue requires mapping two PSE controller outputs to a single port for 4-pair delivery.

**What we verified on our BCM59111 (2-pair only hardware)**:
- Command 0x19 (Set port power pair) works — MCU accepts A-pair (00) and B-pair (01) with error=0, value persists on read-back via 0x25 GET CONFIG reply[8]
- Our device: `port_map_en=0`, `system_status` bit 3=0 (pair mapping not enabled), single PSE controller — no pairing possible
- Full UART test log in conwrt `docs/POE_PARITY.md` under "CPU Utilization Investigation"

**Solution path for BCM59121 hardware** (protocol from svanheule.net):

| Step | Command | What | Status |
|------|---------|------|--------|
| Read current mapping | 0x26 GET EXT CONFIG | Returns `primary_pse_output` and `secondary_pse_output` per port | ✅ Already decoded in our code |
| Read pair mapping status | 0x20 GET SYSTEM INFO | `system_status` bit 3 = "Output pairing enabled" | ✅ Already decoded |
| Set power-up mode to bt | 0x1c SET PORT POE MODE | mode=05 (802.3bt) | ✅ Already in dialect (`PORT_SET_POE_MODE`) |
| A/B pair selection | 0x19 SET PORT POWER PAIR | pair=00 (A) or 01 (B) | ✅ Verified working, needs dialect entry |
| Map port → PSE output | 0x1d SET PORT MAPPING | `[port] [pse_output]` where pse_output = 8×APSE + N | ❌ Not in dialect, protocol fully documented |
| Full PSE output mapping | 0x0e SET PORT TO PSE OUTPUT | 7 parameters (b1-b7), values documented | ❌ Not in dialect, some params "likely" not confirmed |
| Enable pair mapping | Unknown | Which command sets system_status bit 3 | ❌ Unknown — likely achieved by 0x0e sequence |

**To pick this up**: Get UART capture from stock firmware on a BCM59121 device during init. The init sequence will show the exact 0x0e/0x1d commands and parameter values. Then implement in `dialect_bcm.c` and test on hardware.
