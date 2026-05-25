# Research Notes Index

## Active Research

| File | Topic | Status |
|---|---|---|
| `gs1900-8hp-poe-analysis.md` | Full PoE protocol analysis, hardware specs, decoded commands, bug list | Updated with load test data |
| `notes/` | Daily research logs | Ongoing |

## Planned Research

- [x] Stock firmware MCU communication reverse engineering (web UI cmd mapping done, UART protocol same as OpenWrt)
- [x] PoE load testing results (NR7101 ~5W load, stock vs OpenWrt comparison, disable/enable tested)
- [ ] Broadcom vs Realtek dialect feature matrix
- [ ] Power management mode investigation
- [x] Per-port power measurement accuracy study (stock mW vs OpenWrt W, within ~0.5W tolerance)

## Upstream Issues We're Investigating

These are issues from `Hurricos/realtek-poe` that we're researching in our workspace.
We do NOT comment on these issues. All findings stay in this fork until a human submits them.

- **#59**: "New realtek dialect is not autodetected" — Related to our dialect auto-detection investigation
- **#63**: "GS1900-10HP-B1 no response from PoE controller" — Related device, may share root cause
- **#64**: "Transfer repository to organization" — Community governance discussion
- **#68**: "Add PSE ID quirk for GS1900-48HP A1" — Same device family, similar PSE ID issue pattern

## Hardware Inventory

| Device | IP | Firmware | Access | Role |
|---|---|---|---|---|
| GS1900-8HP A1 #1 | 192.168.1.2 | OpenWrt (Linux 6.12.74, RTL8380) | SSH (root, no password) | Primary test device |
| GS1900-8HP A1 #2 | 192.168.1.1 | ZyXEL stock V2.90 | HTTP (admin/Zyxel2026!) | Stock comparison device |
| MacBook (en5 USB) | 192.168.1.100 | macOS | Local | Control workstation |
| NR7101 | 192.168.1.10 | OpenWrt (mips) | SSH (root, no password) | PoE test load (~5W, connected to port 8 on both switches) |
