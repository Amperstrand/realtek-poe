# 2026-09-25 — daemon↔MCU wedge root cause + fix (GS1900-8HP #1, bench)

Incident class: recurring PoE wedge on the OpenWrt bench switch
(192.168.13.2), three occurrences in one morning plus one unexplained
switch watchdog reboot (12:26Z). Full timeline, rejection taxonomy and
hardware forensics: `~/conwrt-bench/docs/POE-WEDGE-ROOT-CAUSE.md` (the
authoritative writeup; this note covers the code-side findings).

## What was found (three bugs, one incident chain)

1. **Framing desync with no resync path** (`poe_stream_msg_cb`): fixed
   12-byte windowing over the UART stream; one stray/partial byte from a
   crashed MCU misaligns every later window forever. Frozen `poe info`,
   silently-dropped `manage` (bad windows freed the head command), and
   daemon restart as the only recovery (open + tcflush). Both "wedge
   variants" (daemon-side vs MCU-side) were this bug plus differing MCU
   recovery times.
2. **LED map reply OOB write** (`poe_reply_port_led_map`): unvalidated
   `reply[2]/8` index into a 6-entry array; BCM59111 v17.1 returns 0xFF
   for unimplemented LED commands → index 31 → deterministic 9×0xFF OOB
   write every boot. Latent (padding) since the LED work landed; became
   visible when the mcu_comm counters struct extension shifted the
   landing onto `config.ports[5].name` during this session's patching.
   Found with a per-frame canary: fires at reply #43 (the LED map reply
   in init), ok=42/bad=0, 6/6 boots; fixed 6/6 clean.
3. **Unexplained box reboot 12:26Z**: hang→watchdog-reset shape, no lane
   claims it, trigger unproven (candidates: 6 restarts in 22 min racing
   LED debugfs writes; MCU-crash cascade; 25.12.1 rtl838x platform bug —
   note 25.12.5 fixes "RTL838x: fix non-functional reboot"). The W2
   serial door (gs1900-serial-bench-arcs plan) is the instrument for the
   next occurrence.

## Commits

- `ai: fix OOB write in LED map reply handler` (434b6be)
- `ai: survive MCU garbage — UART framing resync, bounded retry,
  staleness surfacing` (80a7783) — includes `mcu_comm` in `poe info`
  (`age_s`, `stale`, rejection/resync counters) so a frozen snapshot is
  never served silently.

## Offline repro harness (QEMU + scripted MCU)

Artifacts on ai-legion-small: `/tmp/opencode/fakemcu.py` (speaks the
12-byte protocol on a pty with injection modes STRAY/FF/TRUNC/STORM +
realistic reply delays), `/tmp/opencode/harness.sh` (lifecycle),
`/tmp/opencode/blobtest.c` (blob-shape isolation), plus a MIPS rootfs
with musl/ubox/ubus/uci from the SDK and an unstripped `realtek-poe`
build. `qemu-mips -L <rootfs> <binary>` runs the daemon against the fake
MCU; `/dev/ttyS1` was temporarily symlinked to the fake pty during runs
(restored after). The STRAY injection reproduced the on-hardware desync
signature exactly and verified the resync recovery live. The QEMU stack
never reproduced the OOB because the fake MCU answers the LED map
command with a *valid* map (reply[2]=0), unlike v17.1 hardware — worth
fixing in fakemcu if the harness is reused.

## Deploy + verification (2026-09-25)

- Package `realtek-poe-5.apk` built in `openwrt/sdk:realtek-rtl838x-25.12.1`,
  sha256 4e677901306b3703555931fe91b85e52e63d793d6a8fa25b89c29ffc5b6f674d, deployed 14:22Z from bin/packages/mips_24kc/base/.
- `apk add --allow-untrusted --force-non-repository` under the bench
  flock + ONE daemon restart (no switch reboot; healthy PDs unaffected —
  consistent with all prior restarts).
- 6/6 clean daemon boots (dice-roll script `/tmp/opencode/diceroll.sh`,
  on-switch at /tmp/diceroll.sh); lan7 positive controls 2×PASS pre-patch
  and 2×PASS post-patch (reflects 14–32 s at poll_interval=30000);
  wedge-guard healthy; full `poe info` JSON parses with all 8 port keys
  clean.

## Open items

- Watch-stream blind spot: snap events carry `uptime_s: 0` /
  `boot_id: ""` always (cannot see switch reboots — why 12:26Z went
  unnoticed); 69 snap_unparsable are SSH failures, not corruption.
- Guard crash on ssh 255 mid-recovery (unhandled CalledProcessError) and
  on non-UTF-8 output (UnicodeDecodeError) — hardening noted in the
  conwrt-bench writeup; guard v2 should key on `mcu_comm.stale`.
- apk version labels stayed `1.3.1-r1` across releases (filename-only
  distinction) — same-version installs are ambiguous; consider bumping
  PKG_VERSION or checking binary md5 after deploy.
- Stray legacy binary at /usr/sbin/realtek-poe (not running, different
  md5) — inert cruft, candidate for removal at the next upgrade window.
