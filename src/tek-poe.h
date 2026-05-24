/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef TEK_POE_H
#define TEK_POE_H

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <libubox/utils.h>

#define ULOG_DBG(fmt, ...) ulog(LOG_DEBUG, fmt, ## __VA_ARGS__)

#define GET_STR(a, b)	((a) < ARRAY_SIZE(b) ? (b)[a] : NULL)
#define MAX(a, b)	(((a) > (b)) ? (a) : (b))
#define MAX_PORT	48

/*
 * Order of commands doesn't matter. These are just an internal representation
 * that gets mapped to a wire command based on the dialect. Value of "0" is
 * reserve for "dialect does not implement command".
 *   MCU_ are global commands
 *   PORT_ are "port" commands
 */
enum poe_cmd {
	CMD_NONE = 0,
	MCU_SET_POWER_MGMT_MODE,
	MCU_SET_POWER_BUDGET,
	MCU_ENABLE_PORT_MAPPING,
	PORT_ENABLE,
	PORT_ENABLE_CLASSIFICATION,
	PORT_SET_DETECTION_TYPE,
	PORT_SET_PRIORITY,
	PORT_SET_POE_MODE,
	PORT_SET_DISCONNECT_TYPE,
	PORT_RESET,
	PORT_SET_POWER_LIMIT_TYPE,
	PORT_SET_POWER_LIMIT,
	PORT_SET_AUTO_POWERUP,

	MCU_CLEAR_COUNTERS,
	MCU_SET_DEVICE_POWER_MGMT,
	MCU_SET_HIGH_POWER_LIMIT,

	MCU_GET_SYSTEM_INFO,
	MCU_GET_POWER_STATS,
	MCU_GET_EXT_CONFIG,
	PORT_GET_CONFIG,
	PORT_GET_EXT_CONFIG,
	PORT_GET_STATUS,
	PORT_GET_SHORT_STATUS,
	PORT_GET_POWER_STATS,
	MCU_GET_PSE_POWER,
	PORT_GET_COUNTERS,
	MCU_GET_POWER_MGMT,

	/* LED commands (wire 0x41-0x49)
	 * Protocol: svanheule.net/switches/software/broadcom_poe_control_protocol
	 * Stock: board_poe_led_set (0x3920), board_poe_portLed_set (0x39e8),
	 *        board_poe_portLedCtrl_set (0x3cc4), board_poe_portLedEnable_set (0x3d6c)
	 * GS1900-8HP: 2 LEDs/port, bi-color anti-parallel, SPI shift register, LSB first
	 * Verified: grobian PR Hurricos/realtek-poe#48 on GS1900-8HP v1 */
	LED_GET_PORT_CONFIG,	/* 0x42 */
	LED_GET_SYSTEM_CONFIG,	/* 0x44 */
	LED_GET_PORT_MAP,	/* 0x49 */

	CMD_MAX
};

enum poe_cmd_flags {
	CMD_IS_4PORT = 2,
	CMD_IS_4PORT_RTL = 4,
	CMD_HAS_ALL_PORT = 8,
};

struct mcu;

struct port_state {
	const char *status;
	const char *poe_mode;
	float power_budget;
	float watt;
	float voltage;
	float current;
	float temperature;

	unsigned int has_config_info : 1;
	unsigned int has_detailed_state : 1;

	uint8_t power_limit_type;
	uint8_t priority;
	uint8_t primary_pse_output;
	uint8_t primary_power_limit;
	uint8_t mapping;

	uint8_t enabled;
	uint8_t auto_powerup;
	uint8_t detection_type;
	uint8_t classification_enable;
	uint8_t disconnect_type;
	uint8_t pair;

	uint8_t fault_type;
	uint8_t class_info;
	uint8_t pd_type;
	uint8_t mpss_mask;
	uint8_t power_mode;
	uint8_t chan_pwr;
	uint8_t pd_alt;

	uint16_t cnt_overload;
	uint16_t cnt_short;
	uint16_t cnt_denied;
	uint16_t cnt_mps_absent;
	uint16_t cnt_invalid_signature;
};

/* 0x42 reply: per-port LED configuration.
 * state_off/req/err/on use packed bitmask <[B][S]0000[mm]>:
 *   B=blink, S=PoE+ switch (2-LED only), mm=LED mask bits.
 * For anti-parallel bi-color LEDs (GS1900-8HP): 00 and 11 = off, 01/10 = on.
 */
struct port_led_config {
	uint8_t enable;		/* 0=MCU LED mgmt off, 1=on */
	uint8_t interface;	/* 0=SPI shift register, 1=GPIO parallel */
	uint8_t shift_order;	/* 0=LSB first, 1=MSB first */
	uint8_t led_count;	/* 1 or 2 LEDs per port */
	uint8_t state_off;	/* LED mask: disabled or searching */
	uint8_t state_req;	/* LED mask: requesting power (bit7=blink 2Hz) */
	uint8_t state_err;	/* LED mask: fault/other fault (bit7=blink 10Hz) */
	uint8_t state_on;	/* LED mask: delivering power (bit6=PoE+ vs PoE switch) */
	uint8_t blink_override;	/* 0=none, 1=requesting, 2=fault, 3=both */
};

/* 0x49 reply: port-to-LED position mapping. 8 ports per reply.
 * GS1900-8HP: lan1=0, lan2=1, ... lan8=7 (sequential).
 */
struct port_led_map {
	uint8_t offset;
	uint8_t ports[8];
};

/* 0x44 reply: system-level PoE LED config (power budget indicator).
 * sys_ok/in_gb/out_of_gb/exceeds_ps: 0=off, 1=on, 2=blink slow, 3=blink fast.
 */
struct system_led_config {
	uint8_t sys_ok;
	uint8_t in_gb;
	uint8_t out_of_gb;
	uint8_t exceeds_ps;
	uint8_t out_of_gb_off_delay;
	uint8_t exceeds_ps_off_delay;
	uint8_t map_enable;
};

struct mcu_state {
	const char *sys_mode;
	const char *sys_mcu;
	const char *sys_status;
	float power_consumption;
	float reported_power_budget;
	float allocated_power;
	float uvlo_threshold;
	float ovlo_threshold;
	unsigned int num_detected_ports;

	unsigned int has_ext_cfg_info : 1;

	uint16_t device_id;
	uint8_t sys_version;
	uint8_t sys_ext_version;
	uint8_t port_map_en;

	uint8_t pre_alloc;
	uint8_t powerup_mode;
	uint8_t disconnect_type;
	uint8_t ddflag;
	uint8_t num_pse;

	uint8_t pse_id;
	uint8_t high_power;
	uint8_t gb_hysteresis;

	uint8_t pm_mode;
	float pm_power_limit[2];
	float pm_guard_band[2];

	struct port_state ports[MAX_PORT];

	struct port_led_config   port_led_config;
	struct port_led_map      led_maps[(MAX_PORT + 7) / 8];
	struct system_led_config sys_led_config;
};

struct port_config {
	char name[16];
	unsigned int valid : 1;
	unsigned int enable : 1;
	uint8_t priority;
	uint8_t power_up_mode;
	uint8_t power_budget;
	uint8_t power_limit_type;
	uint16_t power_limit_mw;
};

struct dialect_desc;

struct config {
	const struct dialect_desc *forced_dialect;

	float budget;
	float budget_guard;

	float threshold_high;
	float threshold_low;

	unsigned int forced_baudrate;
	unsigned int poll_interval_ms;
	unsigned int port_count;
	uint8_t pse_id_set_budget_mask;
	struct port_config ports[MAX_PORT];
};

struct dialect_ops {
	int (*init_async)(struct mcu *mcu, const struct config *cfg);
	int (*poll_async)(struct mcu *mcu, const struct config *cfg);
	int (*reset)(struct mcu *mcu);
	int (*handle_reply)(struct mcu_state *mcu, uint8_t *reply, size_t len);
};

struct dialect_map_entry {
	uint8_t wire_id;
	uint8_t flags;
};

struct dialect_map {
	const struct dialect_map_entry *entries;
	size_t len;
};

struct dialect_desc {
	const struct dialect_ops *ops;
	const struct dialect_map *map;
};

struct poe_dialect {
	const struct dialect_desc *desc;
	uint8_t reverve_map[0x100];
};

int mcu_queue_buf(struct mcu *mcu, uint8_t *cmd_buf, size_t len);

static inline uint16_t read16_be(uint8_t *raw)
{
	return (uint16_t)raw[0] << 8 | raw[1];
}

static inline void write16_be(uint8_t *raw, uint16_t value)
{
	raw[0] = value >> 8;
	raw[1] =  value & 0xff;
}

static inline int dialect_reverse_map(struct poe_dialect *dialect)
{
	const struct dialect_map *map = dialect->desc->map;
	unsigned int wire_id;
	size_t i;

	for (i = 0; i < map->len; i++) {
		if (!map->entries[i].flags)
			continue;

		wire_id = map->entries[i].wire_id;
		if (wire_id > 0x100)
			return -EINVAL;

		dialect->reverve_map[wire_id] = i;
	}
	return 0;
}

static inline int dialect_lookup_cmd(const struct poe_dialect *dialect,
				     enum poe_cmd cmd)
{
	const struct dialect_map *map = dialect->desc->map;

	if (cmd > map->len || !map->entries[cmd].flags)
		return -EINVAL;

	return map->entries[cmd].wire_id;
}


static inline enum poe_cmd dialect_rev_lookup(const struct poe_dialect *dialect,
					      uint8_t wire_id)
{
	return dialect->reverve_map[wire_id];
}

const char *port_short_status_to_str(uint8_t short_status);
extern const struct dialect_desc broadcom_dialect;
extern const struct dialect_desc realtek_dialect;

#endif /* TEK_POE_H */
