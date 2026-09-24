/*
 * led_mgr.h -- LED state machine for PandaBox.
 *
 * LED mapping (active HIGH, all confirmed from schematic):
 *   PB0 = GPS green
 *   PB1 = PWR red      -- always ON after boot, never touched by state machine
 *   PC4 = WiFi orange  -- future; stays off until WiFi driver exists
 *   PC5 = BT blue      -- BLE advertising: blinks 500ms; connected: solid ON
 *   PC6 = LCP1 / 4G    -- solid ON once system is up (4G LED placeholder)
 *   PC7 = LCP2         -- solid ON once system is up (secondary indicator)
 *
 * BLE connect / OTA events trigger a series sweep of all 6 LEDs, then
 * settle into steady state.
 */
#ifndef LED_MGR_H
#define LED_MGR_H

typedef enum {
    LED_EVT_BLE_ADV,        /* BLE advertising started (BT blinks 500ms on/off) */
    LED_EVT_BLE_CONNECT,    /* BLE link established -- sweep all 6, then BT solid */
    LED_EVT_BLE_DATA,       /* data RX or TX -- brief 80ms BT blink */
    LED_EVT_BLE_DISCONNECT, /* link dropped -- back to advertising blink */
    LED_EVT_OTA_START,      /* OTA in progress -- all 6 sweep continuously */
    LED_EVT_OTA_DONE,       /* OTA complete -- restore connected state */
} led_event_t;

/* Call once after led_boot_sequence() completes in main(). */
void led_mgr_init(void);

/* Post an event from any task context (non-blocking). */
void led_mgr_event(led_event_t evt);

/* Drive the state machine; call every 20 ms from vTaskA. */
void led_mgr_tick(void);

#endif /* LED_MGR_H */
