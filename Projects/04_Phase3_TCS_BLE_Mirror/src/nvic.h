#ifndef NVIC_H
#define NVIC_H

/* Forces NVIC priority grouping into a known state (group priority only,
 * no sub-priority split) instead of relying on whatever the silicon
 * happens to reset to. Call once, early in main(), right after
 * clock_init(). Needed before FreeRTOS bring-up (Phase C onward) so
 * PendSV/SysTick priorities behave predictably; harmless and a no-op
 * behaviorally for the current bare-metal-only build (Phase A/B). */
void nvic_set_priority_grouping(void);

#endif
