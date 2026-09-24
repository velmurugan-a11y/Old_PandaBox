/*
 * led_mgr.c -- tick-driven LED state machine for PandaBox.
 *
 * State transitions:
 *
 *   (init) ─────────────────────────────────────────────> IDLE
 *   IDLE / BLE_ADV ──── LED_EVT_BLE_ADV ──────────────> BLE_ADV
 *   BLE_ADV ──────────── LED_EVT_BLE_CONNECT ──────────> CONN_SWEEP
 *   CONN_SWEEP ─────────── sweep done ─────────────────> CONNECTED
 *   CONNECTED ──────────── LED_EVT_BLE_DATA ───────────> DATA_BLINK
 *   DATA_BLINK ─────────── timer expires ──────────────> CONNECTED
 *   CONNECTED / any ────── LED_EVT_BLE_DISCONNECT ─────> BLE_ADV
 *   any ────────────────── LED_EVT_OTA_START ───────────> OTA
 *   OTA ────────────────── LED_EVT_OTA_DONE ────────────> CONNECTED
 *
 * PWR red (PB1) is NEVER turned off by this module -- it is latched ON
 * in led_boot_sequence() before led_mgr_init() is called and remains on
 * permanently.
 */
#include "led_mgr.h"
#include "gpio.h"
#include "board_config.h"
#include "FreeRTOS.h"
#include "task.h"

/* ------------------------------------------------------------------ */

typedef enum {
    S_IDLE,
    S_BLE_ADV,
    S_CONN_SWEEP,
    S_CONNECTED,
    S_DATA_BLINK,
    S_OTA,
} state_t;

#define NUM_LEDS 6u

static const struct {
    gpio_reg_t *port;
    uint8_t     pin;
} k_leds[NUM_LEDS] = {
    { LED_GPS_GREEN_PORT,   LED_GPS_GREEN_PIN   }, /* 0 */
    { LED_PWR_RED_PORT,     LED_PWR_RED_PIN     }, /* 1 -- always on, sweep writes 1 only */
    { LED_WIFI_ORANGE_PORT, LED_WIFI_ORANGE_PIN }, /* 2 */
    { LED_BT_BLUE_PORT,     LED_BT_BLUE_PIN     }, /* 3 */
    { LED_LCP1_PORT,        LED_LCP1_PIN        }, /* 4 */
    { LED_LCP2_PORT,        LED_LCP2_PIN        }, /* 5 */
};

#define PWR_IDX  1u   /* index of PWR LED -- never write 0 to this one */
#define BT_IDX   3u
#define LCP1_IDX 4u
#define LCP2_IDX 5u

#define SWEEP_ON_MS   80u
#define SWEEP_GAP_MS  30u
#define ADV_HALF_MS  500u
#define DATA_OFF_MS   80u

static state_t    s_state  = S_IDLE;
static TickType_t s_next   = 0;
static uint8_t    s_idx    = 0;    /* sweep index */
static uint8_t    s_phase  = 0;    /* 0 = LED on, 1 = gap */

/* ------------------------------------------------------------------ */

static void led_write(uint8_t idx, int on)
{
    if (idx == PWR_IDX && !on) return;   /* never extinguish PWR */
    gpio_write(k_leds[idx].port, k_leds[idx].pin, on);
}

static void all_off_except_pwr(void)
{
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        if (i != PWR_IDX) {
            gpio_write(k_leds[i].port, k_leds[i].pin, 0);
        }
    }
}

static void restore_adv_leds(void)
{
    /* PWR solid, LCP1 solid, BT off (blink will drive it), rest off */
    all_off_except_pwr();
    gpio_set_output_high(k_leds[LCP1_IDX].port, k_leds[LCP1_IDX].pin);
}

static void restore_connected_leds(void)
{
    /* PWR solid, BT solid, LCP1 solid, LCP2 solid, GPS/WiFi off */
    all_off_except_pwr();
    gpio_set_output_high(k_leds[BT_IDX].port,   k_leds[BT_IDX].pin);
    gpio_set_output_high(k_leds[LCP1_IDX].port, k_leds[LCP1_IDX].pin);
    gpio_set_output_high(k_leds[LCP2_IDX].port, k_leds[LCP2_IDX].pin);
}

/* ------------------------------------------------------------------ */

void led_mgr_init(void)
{
    /* PWR was already latched ON by led_boot_sequence() -- don't touch it.
     * Turn on LCP1 (4G placeholder) and LCP2 as "system ready" indicators. */
    all_off_except_pwr();
    gpio_set_output_high(k_leds[LCP1_IDX].port, k_leds[LCP1_IDX].pin);
    gpio_set_output_high(k_leds[LCP2_IDX].port, k_leds[LCP2_IDX].pin);
    s_state = S_IDLE;
    s_next  = 0;
    s_idx   = 0;
    s_phase = 0;
}

void led_mgr_event(led_event_t evt)
{
    TickType_t now = xTaskGetTickCount();

    switch (evt) {

    case LED_EVT_BLE_ADV:
        s_state = S_BLE_ADV;
        restore_adv_leds();
        s_phase = 1;   /* phase 1 = BT off; tick will turn it on immediately */
        s_next  = now;
        break;

    case LED_EVT_BLE_CONNECT:
        if (s_state == S_CONN_SWEEP) break;   /* already sweeping */
        s_state = S_CONN_SWEEP;
        all_off_except_pwr();
        s_idx   = 0;
        s_phase = 0;   /* phase 0 = about to turn on LED[s_idx] */
        s_next  = now;
        break;

    case LED_EVT_BLE_DATA:
        if (s_state == S_CONNECTED) {
            s_state = S_DATA_BLINK;
            gpio_write(k_leds[BT_IDX].port, k_leds[BT_IDX].pin, 0);
            s_next  = now + pdMS_TO_TICKS(DATA_OFF_MS);
        } else if (s_state == S_DATA_BLINK) {
            /* Extend blink window on rapid successive data events */
            s_next = now + pdMS_TO_TICKS(DATA_OFF_MS);
        }
        break;

    case LED_EVT_BLE_DISCONNECT:
        s_state = S_BLE_ADV;
        restore_adv_leds();
        s_phase = 1;
        s_next  = now;
        break;

    case LED_EVT_OTA_START:
        s_state = S_OTA;
        all_off_except_pwr();
        s_idx   = 0;
        s_phase = 0;
        s_next  = now;
        break;

    case LED_EVT_OTA_DONE:
        s_state = S_CONNECTED;
        restore_connected_leds();
        break;
    }
}

void led_mgr_tick(void)
{
    TickType_t now = xTaskGetTickCount();

    if ((int32_t)(now - s_next) < 0) return;

    switch (s_state) {

    case S_IDLE:
        break;

    case S_BLE_ADV:
        if (s_phase == 0) {
            /* BT is on; time to turn it off */
            gpio_write(k_leds[BT_IDX].port, k_leds[BT_IDX].pin, 0);
            s_phase = 1;
            s_next  = now + pdMS_TO_TICKS(ADV_HALF_MS);
        } else {
            /* BT is off; time to turn it on */
            gpio_write(k_leds[BT_IDX].port, k_leds[BT_IDX].pin, 1);
            s_phase = 0;
            s_next  = now + pdMS_TO_TICKS(ADV_HALF_MS);
        }
        break;

    case S_CONN_SWEEP:
        if (s_phase == 0) {
            led_write(s_idx, 1);
            s_phase = 1;
            s_next  = now + pdMS_TO_TICKS(SWEEP_ON_MS);
        } else {
            led_write(s_idx, 0);   /* PWR guard inside led_write */
            s_idx++;
            s_phase = 0;
            s_next  = now + pdMS_TO_TICKS(SWEEP_GAP_MS);
            if (s_idx >= NUM_LEDS) {
                s_state = S_CONNECTED;
                restore_connected_leds();
            }
        }
        break;

    case S_CONNECTED:
        break;

    case S_DATA_BLINK:
        /* Blink window expired: restore BT on */
        gpio_write(k_leds[BT_IDX].port, k_leds[BT_IDX].pin, 1);
        s_state = S_CONNECTED;
        break;

    case S_OTA:
        if (s_phase == 0) {
            led_write(s_idx, 1);
            s_phase = 1;
            s_next  = now + pdMS_TO_TICKS(SWEEP_ON_MS);
        } else {
            led_write(s_idx, 0);
            s_idx++;
            if (s_idx >= NUM_LEDS) {
                s_idx = 0;   /* loop continuously during OTA */
            }
            s_phase = 0;
            s_next  = now + pdMS_TO_TICKS(SWEEP_GAP_MS);
        }
        break;
    }
}
