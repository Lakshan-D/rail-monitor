/*
 * Rail Monitor: power rail telemetry for Arduino Uno (ATmega328P)
 *
 *   - Timer1 interrupt sets a 100 Hz sample tick (no delay() in the main loop)
 *   - ADC reads the rail through a resistor divider, 8x oversampled
 *   - Optional INA219 over I2C gives current (set USE_INA219 to 1)
 *   - Debounced under/over-voltage and over-current flags
 *   - 14 byte framed packets with CRC-16 sent over UART at 10 Hz
 *   - Limits stored 3 times in EEPROM, 2-of-3 bitwise vote, periodic scrub
 *   - Watchdog resets the board if the main loop stops running
 *
 * The portable logic (CRC, framing, fault debounce, voting) lives in src/ and is
 * unit tested on a PC. This file is only the hardware glue.
 *
 * STATUS: sketch written to match the tested core. Compile and bench-test it on
 * your own board and record what you see in the README.
 */

#include <avr/io.h>
#include <avr/wdt.h>
#include <EEPROM.h>

#include "src/crc16.h"
#include "src/frame.h"
#include "src/fault.h"
#include "src/vote.h"

/* ------------------------------ user settings ------------------------------ */

#define USE_INA219        0        /* 1 = read current from an INA219 at 0x40  */

/* Divider from rail to A0. Two equal resistors (e.g. 10k + 10k) give ratio 2.00
 * which maps 0..10 V on the rail to 0..5 V at the pin. Never exceed 10 V. */
#define DIVIDER_X100      200u
/* Measure your 5 V pin with a multimeter and put the real number here (mV). */
#define VREF_MV           5000u

#define SAMPLE_HZ         100u
#define TELEM_EVERY       10u      /* send one packet per 10 samples = 10 Hz   */
#define SCRUB_MS          60000UL  /* re-vote the EEPROM copies every minute   */
#define CFG_FIXED_HOLD_MS 5000UL   /* keep CFG_FIXED flag visible for 5 s      */
#define BAUD              115200UL

/* EEPROM: three copies of the 7 byte config, 16 bytes apart. */
static const int CFG_ADDR[3] = {0, 16, 32};

/* ------------------------------- reset cause ------------------------------- */

/* Grab MCUSR before the watchdog can re-trigger. Standard avr-libc pattern.
 * NOTE: the Uno bootloader may clear MCUSR before the sketch runs, in which case
 * WDRF reads 0 here. The ground station also detects resets by the timestamp
 * jumping backwards, which works either way. */
uint8_t mcusr_mirror __attribute__((section(".noinit")));
void get_mcusr(void) __attribute__((naked)) __attribute__((section(".init3")));
void get_mcusr(void)
{
    mcusr_mirror = MCUSR;
    MCUSR = 0;
    wdt_disable();
}

/* --------------------------------- state ---------------------------------- */

static volatile uint8_t sample_due = 0;

static rm_cfg_t   cfg;
static rm_fault_t fault;
static uint8_t    seq = 0;
static uint8_t    sample_count = 0;
static uint8_t    boot_flags = 0;
static uint32_t   cfg_fixed_until_ms = 0;
static uint32_t   last_scrub_ms = 0;

ISR(TIMER1_COMPA_vect)
{
    sample_due = 1;
}

/* ------------------------------- EEPROM config ----------------------------- */

static void cfg_store(const rm_cfg_t *c)
{
    uint8_t raw[RM_CFG_BYTES];
    rm_cfg_to_bytes(c, raw);
    for (uint8_t copy = 0; copy < 3; copy++) {
        for (uint8_t i = 0; i < RM_CFG_BYTES; i++) {
            EEPROM.update(CFG_ADDR[copy] + i, raw[i]);   /* update() skips unchanged bytes */
        }
    }
}

/* Read three copies, vote, repair any copy that disagreed. */
static void cfg_load_and_scrub(void)
{
    uint8_t copy[3][RM_CFG_BYTES];
    uint8_t voted[RM_CFG_BYTES];

    for (uint8_t c = 0; c < 3; c++) {
        for (uint8_t i = 0; i < RM_CFG_BYTES; i++) {
            copy[c][i] = EEPROM.read(CFG_ADDR[c] + i);
        }
    }

    size_t disagree = rm_vote_buf(copy[0], copy[1], copy[2], voted, RM_CFG_BYTES);
    rm_cfg_from_bytes(&cfg, voted);

    if (!rm_cfg_valid(&cfg)) {
        /* blank EEPROM (first boot) or damage beyond what voting can fix */
        rm_cfg_default(&cfg);
        cfg_store(&cfg);
        return;
    }
    if (disagree > 0) {
        cfg_store(&cfg);                           /* scrub: rewrite all copies */
        cfg_fixed_until_ms = millis() + CFG_FIXED_HOLD_MS;
    }
}

/* -------------------------------- measurements ----------------------------- */

static uint16_t read_rail_mv(void)
{
    uint16_t sum = 0;
    for (uint8_t i = 0; i < 8; i++) {
        sum += (uint16_t)analogRead(A0);           /* 8 x 10 bit fits in 16 bit */
    }
    uint32_t adc = sum >> 3;
    uint32_t mv_at_pin = (adc * VREF_MV) / 1023UL;
    return (uint16_t)((mv_at_pin * DIVIDER_X100) / 100UL);
}

#if USE_INA219
#include <Wire.h>
#define INA_ADDR 0x40
/* Written from the INA219 datasheet for a 0.1 ohm shunt, 32 V range, current LSB
 * 0.1 mA, calibration 4096. UNTESTED on hardware: verify against a multimeter. */
static bool ina_ok = false;

static bool ina_write(uint8_t reg, uint16_t val)
{
    Wire.beginTransmission(INA_ADDR);
    Wire.write(reg);
    Wire.write((uint8_t)(val >> 8));
    Wire.write((uint8_t)val);
    return Wire.endTransmission() == 0;
}

static bool ina_read(uint8_t reg, uint16_t *val)
{
    Wire.beginTransmission(INA_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) return false;
    if (Wire.requestFrom((uint8_t)INA_ADDR, (uint8_t)2) != 2) return false;
    *val = (uint16_t)((Wire.read() << 8) | Wire.read());
    return true;
}

static void ina_begin(void)
{
    Wire.begin();
    ina_ok = ina_write(0x05, 4096);
}

static int16_t read_current_ma(void)
{
    uint16_t raw;
    if (!ina_ok || !ina_read(0x04, &raw)) {
        return 0;
    }
    return (int16_t)raw / 10;                      /* raw LSB = 0.1 mA */
}
#else
static int16_t read_current_ma(void) { return 0; }
#endif

/* --------------------------------- telemetry ------------------------------- */

static void send_telemetry(uint16_t mv, int16_t ma, uint8_t fault_flags)
{
    uint8_t flags = fault_flags | boot_flags;
    if (millis() < cfg_fixed_until_ms) {
        flags |= RM_FLAG_CFG_FIXED;
    }

    uint8_t payload[RM_TELEM_LEN];
    uint8_t out[RM_MAX_FRAME];

    rm_telem_pack(payload, millis(), mv, ma, flags);
    size_t n = rm_frame_encode(out, seq++, payload, RM_TELEM_LEN);
    Serial.write(out, n);
}

/* Single character commands from the ground station, for demos and tests. */
static void handle_command(void)
{
    if (!Serial.available()) {
        return;
    }
    char c = (char)Serial.read();

    if (c == 'x') {
        /* Simulate a bit flip (a stand-in for a single event upset) in config copy 2,
         * then re-vote straight away to show the correction. */
        EEPROM.write(CFG_ADDR[1], EEPROM.read(CFG_ADDR[1]) ^ 0x01);
        cfg_load_and_scrub();
    } else if (c == 'h') {
        /* Hang on purpose: the watchdog must reset the board in about 2 s. */
        while (1) { }
    }
}

/* ------------------------------------ main --------------------------------- */

void setup(void)
{
    if (mcusr_mirror & _BV(WDRF)) {
        boot_flags |= RM_FLAG_WDT_RESET;
    }

    Serial.begin(BAUD);
    cfg_load_and_scrub();
    rm_fault_init(&fault);

#if USE_INA219
    ina_begin();
#endif

    /* Timer1, CTC mode, prescaler 256: 16 MHz / 256 / 625 = 100 Hz */
    noInterrupts();
    TCCR1A = 0;
    TCCR1B = _BV(WGM12) | _BV(CS12);
    OCR1A  = (uint16_t)(F_CPU / 256UL / SAMPLE_HZ) - 1;
    TIMSK1 = _BV(OCIE1A);
    interrupts();

    wdt_enable(WDTO_2S);
    last_scrub_ms = millis();
}

void loop(void)
{
    wdt_reset();
    handle_command();

    if (sample_due) {
        sample_due = 0;

        uint16_t mv = read_rail_mv();
        int16_t  ma = read_current_ma();
        uint8_t  fault_flags = rm_fault_update(&fault, &cfg, mv, ma);

        if (++sample_count >= TELEM_EVERY) {
            sample_count = 0;
            send_telemetry(mv, ma, fault_flags);
        }
    }

    if ((millis() - last_scrub_ms) >= SCRUB_MS) {
        last_scrub_ms = millis();
        cfg_load_and_scrub();
    }
}
