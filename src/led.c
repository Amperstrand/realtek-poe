/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * PoE LED control via RTL838x SoC LED engine registers.
 *
 * PoE status LEDs on RTL838x-based switches are driven by the SoC's built-in
 * LED engine, NOT by the BCM59111 MCU or RTL8231 GPIO. The stock Zyxel
 * firmware uses board_poe_portLed_set() → board_led_portSwCtrl_set() to
 * write to SoC registers. We do the same from userspace via debugfs.
 *
 * Board detection reads /sys/firmware/devicetree/base/compatible and matches
 * against a known-boards table to determine the PoE→SoC port mapping.
 * Unknown boards get LED control disabled (safe default).
 *
 * Register map (physical base 0xBB000000):
 *   0xA00C  led_sw_ctrl          Global software control enable
 *   0xA010  led0_sw_p_en_ctrl    Per-port enable for LED group 0 (PoE row)
 *   0xA014  led1_sw_p_en_ctrl    Per-port enable for LED group 1 (LINK-ACT row)
 *   0xA018  led2_sw_p_en_ctrl    Per-port enable for LED group 2
 *   0xA01C + (port<<2)           Per-port pattern register (9 bits, 3 per group)
 *
 * LED group 0, bits [2:0] control the PoE LED for each port:
 *   0 = OFF, 5 = ON (solid), 4 = FAST BLINK (~2 Hz), 7 = SLOW BLINK (~0.5 Hz)
 *
 * Verified with camera-based automated testing on GS1900-8HP A1 running
 * OpenWrt 25.12.1. See POE_PARITY.md for full RE and validation details.
 */

#include "tek-poe.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <libubox/ulog.h>

/* LED pattern values for group 0 bits [2:0] */
#define LED_PATTERN_OFF		0	/* 000: solid OFF */
#define LED_PATTERN_ON		5	/* 101: solid ON (most stable, std=2) */
#define LED_PATTERN_FAST_BLINK	4	/* 100: ~2 Hz blink */
#define LED_PATTERN_SLOW_BLINK	7	/* 111: ~0.5 Hz blink */

/* RTL838x LED debugfs paths */
#define LED_DEBUGFS_PATH	"/sys/kernel/debug/rtl838x/led"
#define LED_SW_CTRL		LED_DEBUGFS_PATH "/led_sw_ctrl"
#define LED0_SW_P_EN_CTRL	LED_DEBUGFS_PATH "/led0_sw_p_en_ctrl"

/* Maximum SoC port number (for buffer sizing) */
#define MAX_SOC_PORT		31

/*
 * Board-specific SoC LED port mapping.
 * offset: poe_port_id + offset = SoC LED port number
 * max_ports: number of PoE-capable ports on this board
 */
struct led_board_map {
	const char *compatible;	/* Substring to match in DT compatible string */
	int offset;
	int max_ports;
};

static const struct led_board_map led_board_maps[] = {
	/* Zyxel GS1900-8HP v1/v2: lan1..lan8 (port 1..8) → SoC ports 8..15 */
	{ "zyxel,gs1900-8hp",	.offset = 7, .max_ports = 8 },
	{ NULL, 0, 0 }	/* Sentinel — MUST be last */
};

/* Detected at init, used by poe_port_to_soc_port() */
static int led_port_offset = -1;
static int led_max_ports = 0;

/*
 * Detect board from device tree and populate led_port_offset/led_max_ports.
 * Returns 0 on success (known board), -1 on failure (unknown board).
 */
static int detect_led_board(void)
{
	char compatible[256];
	int fd, ret, i;
	const char *p;

	fd = open("/sys/firmware/devicetree/base/compatible", O_RDONLY);
	if (fd < 0) {
		ULOG_WARN("LED: cannot read device tree compatible: %s\n",
			  strerror(errno));
		return -1;
	}

	ret = read(fd, compatible, sizeof(compatible) - 1);
	close(fd);
	if (ret <= 0)
		return -1;

	compatible[ret] = '\0';

	/* Walk null-separated compatible strings from device tree */
	p = compatible;
	while (p < compatible + ret) {
		size_t plen = strlen(p);
		if (!plen) {
			p++;
			continue;
		}

		for (i = 0; led_board_maps[i].compatible; i++) {
			if (strstr(p, led_board_maps[i].compatible)) {
				led_port_offset = led_board_maps[i].offset;
				led_max_ports = led_board_maps[i].max_ports;
				ULOG_INFO("LED: detected board '%s' → offset=%d, "
					  "max_ports=%d\n", p,
					  led_port_offset, led_max_ports);
				return 0;
			}
		}

		p += plen + 1;
	}

	return -1;
}

/*
 * Map PoE port ID (1-based) to SoC LED port number using detected board.
 */
static int poe_port_to_soc_port(unsigned int poe_port_id)
{
	if (led_port_offset < 0)
		return -1;

	if (poe_port_id < 1 || poe_port_id > (unsigned int)led_max_ports)
		return -1;

	return (int)poe_port_id + led_port_offset;
}

/*
 * Map PoE status string to LED pattern value.
 * Matches Zyxel stock firmware behavior (led_state 0=OFF, 7=ON)
 * with blink patterns added for searching and fault states.
 */
static int poe_status_to_led_pattern(const char *status)
{
	if (!status || !status[0])
		return LED_PATTERN_OFF;

	if (!strcmp(status, "Delivering power"))
		return LED_PATTERN_ON;

	if (!strcmp(status, "Searching"))
		return LED_PATTERN_FAST_BLINK;

	/*
	 * "Requesting power" is the transient BCM state between detection/
	 * classification and FET power-on. On a healthy port it's sub-second
	 * and invisible. On unhealthy ports (see RTL8238B Issue #50) it can
	 * stick — same fast-blink as Searching gives operators a visible
	 * "trying to bring port up" signal instead of LED-off ambiguity.
	 */
	if (!strcmp(status, "Requesting power"))
		return LED_PATTERN_FAST_BLINK;

	if (!strcmp(status, "Fault") || !strcmp(status, "Other fault"))
		return LED_PATTERN_SLOW_BLINK;

	/* "Disabled" or any unknown state */
	return LED_PATTERN_OFF;
}

/*
 * Write a hex value to a debugfs LED register file.
 * Returns 0 on success, -errno on failure.
 */
static int led_write_reg(const char *path, unsigned int value)
{
	int fd, len;
	char buf[16];

	fd = open(path, O_WRONLY);
	if (fd < 0) {
		ULOG_WARN("LED: cannot open %s: %s\n", path, strerror(errno));
		return -errno;
	}

	len = snprintf(buf, sizeof(buf), "0x%08x", value);
	if (write(fd, buf, len) != len) {
		ULOG_WARN("LED: write to %s failed: %s\n", path, strerror(errno));
		close(fd);
		return -errno;
	}

	close(fd);
	return 0;
}

/*
 * Read a hex value from a debugfs LED register file.
 * Returns the value on success, -1 on failure.
 */
static int led_read_reg(const char *path)
{
	int fd;
	char buf[16];
	ssize_t n;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;

	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);

	if (n <= 0)
		return -1;

	buf[n] = '\0';
	/* Skip "0x" prefix if present, strip whitespace */
	return (int)strtoul(buf, NULL, 0);
}

/*
 * Build the debugfs path for a per-port LED control register.
 * Path format: /sys/kernel/debug/rtl838x/led/led_sw_p_ctrl.XX
 */
static int led_p_ctrl_path(int soc_port, char *buf, size_t buflen)
{
	return snprintf(buf, buflen, "%s/led_sw_p_ctrl.%02d",
			LED_DEBUGFS_PATH, soc_port);
}

/*
 * One-time initialization of the SoC LED engine for PoE LED control.
 * Detects the board, enables global software control, and sets up
 * per-port enable bits for all PoE ports.
 *
 * Returns 0 on success, negative on failure.
 */
int poe_led_init(unsigned int port_count)
{
	unsigned int i;
	unsigned int en_mask = 0;
	int ret;

	/* Detect board and load port mapping */
	if (detect_led_board() < 0) {
		ULOG_INFO("LED: unknown board, PoE LED control disabled "
			  "(add board to led_board_maps[] to enable)\n");
		return -ENODEV;
	}

	/* Check if the debugfs interface exists */
	ret = access(LED_SW_CTRL, W_OK);
	if (ret < 0) {
		ULOG_INFO("LED: debugfs interface not available, "
			  "PoE LED control disabled\n");
		return -ENODEV;
	}

	/* Build the enable mask: set bit for each PoE port */
	for (i = 0; i < port_count && i < MAX_PORT; i++) {
		int soc_port = poe_port_to_soc_port(i + 1);
		if (soc_port >= 0 && soc_port <= MAX_SOC_PORT)
			en_mask |= (1u << soc_port);
	}

	if (!en_mask) {
		ULOG_WARN("LED: no valid SoC port mappings\n");
		return -EINVAL;
	}

	/* Enable per-port software control for all PoE ports */
	ret = led_write_reg(LED0_SW_P_EN_CTRL, en_mask);
	if (ret < 0)
		return ret;

	/* Enable global software control */
	ret = led_write_reg(LED_SW_CTRL, 0x00000001);
	if (ret < 0)
		return ret;

	ULOG_INFO("LED: SoC LED engine initialized for %u ports "
		  "(en_mask=0x%04x)\n", port_count, en_mask);

	return 0;
}

/*
 * Update a single port's PoE LED based on its current status.
 * Called from the status change detection loop whenever a port's
 * PoE state transitions.
 *
 * @poe_port_id: 1-based PoE port ID (1=lan1, 2=lan2, ..., 8=lan8)
 * @status: current PoE status string (e.g. "Delivering power")
 */
void poe_led_update(unsigned int poe_port_id, const char *status)
{
	int soc_port, pattern, old_val, new_val;
	char path[128];

	soc_port = poe_port_to_soc_port(poe_port_id);
	if (soc_port < 0)
		return;

	pattern = poe_status_to_led_pattern(status);

	/* Build path to per-port control register */
	led_p_ctrl_path(soc_port, path, sizeof(path));

	/* Read-modify-write: only modify bits [2:0] (LED group 0) */
	old_val = led_read_reg(path);
	if (old_val < 0)
		old_val = 0;

	new_val = (old_val & ~0x7) | pattern;

	/* Skip write if value unchanged */
	if (new_val == old_val)
		return;

	if (led_write_reg(path, (unsigned int)new_val) < 0)
		return;

	ULOG_DBG("LED: port %u (soc %d) status='%s' pattern=%d val=0x%08x\n",
		 poe_port_id, soc_port, status ? status : "(null)",
		 pattern, new_val);
}

/*
 * Shutdown: release LED control back to hardware.
 * All PoE LEDs return to hardware-driven state.
 */
void poe_led_shutdown(void)
{
	if (led_port_offset < 0)
		return;

	/* Disable global software control — hardware takes over */
	led_write_reg(LED_SW_CTRL, 0x00000000);
	/* Clear per-port enable mask */
	led_write_reg(LED0_SW_P_EN_CTRL, 0x00000000);
}
