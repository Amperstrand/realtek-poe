# board_poe.ko (Zyxel GS1900-8HP V2.90 GPL install/) — RE catalog, first pass
Date: 2026-10-21. File: 32-bit MSB MIPS REL, NOT STRIPPED — 240 named funcs.

## Headline: the vendor driver has a GPIO HARD RESET for the PoE MCU
- poe_reset.c: board_poe_reset_init(const sys_poe_reset_conf_t{pGpio,
  period_time, restart_time}) + board_poe_reset_set().
- Strings: "PoE Reset period time = %u ms, restart time = %u ms",
  "PoE Reset initialization on GPIO dev %u pin %u", "PoE has been reset",
  "Retry counts: %u / %u" (x many = retry ladder per dialect).
- THIS is the missing recovery for the MCU-crash class (crashed MCU survives
  soft reboots, mains-only today). Fork parity item #1: implement GPIO MCU
  reset. Pin for GS1900-8HP = runtime-injected from board config (not in
  GPL headers; next sources: stock box's config backup via web UI, or
  disasm of the stock userspace board-init).

## Dialect layer (mirrors our fork)
- bcm59111_{uart_init,uart_xmit,uart_ex[change],smi_init,message_debug}
- bcm59121_{uart_init,uart_xmit,uart_ex[change],smi_init,message_debug}
- rtl8238b_{uart_init,uart_xmit,uart_ex[change],xmit_timeout}
- poe_{bcm59011,bcm59111,bcm59121}_init + poe_chip_init dispatch via
  poe_mapper (poe_mapper_init) — same vendor-ops pattern as the GPL headers.

## Other notable symbols
- board_poe_{init,intr_init,led_init,reset_init,reset_set,smi_init,
  dis_init,btn_init}, poe_init_status_check, poe_ctrl_init/_exit,
  ski_poe_cmd_mux (the ioctl mux), poe_threshold_set,
  poe_portAdminEnable_set, drv_uart_baudrate_set (runtime baud).

## Next legs
1. GPIO pin discovery: stock-box config backup (web UI export) or GPL
   kernel vendors/ board files; then a bench script toggling it via sysfs
   to recover a crashed MCU without mains (test on the next crash!).
2. Full disasm (objdump -d, symbols make it tractable) for retry/timing
   constants per dialect vs the fork's aabf65f guesses.
