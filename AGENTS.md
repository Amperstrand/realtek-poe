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
