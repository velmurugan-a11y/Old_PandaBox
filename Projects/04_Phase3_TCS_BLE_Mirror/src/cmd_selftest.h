#ifndef CMD_SELFTEST_H
#define CMD_SELFTEST_H

/* Runs a fixed battery of non-meter Lx commands through cmd_process_line()
 * and logs input/output over RTT + debug UART. See cmd_selftest.c. */
void cmd_selftest_run(void);

#endif
