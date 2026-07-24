/*
  Teensy 4.1 -> Caravel external xclk + reset-triggered ScanDebug packet driver

  Behavior:
    - Generates continuous 1 MHz xclk on PIN_XCLK.
    - Clock period = 1000 ns.
    - Clock high time = 500 ns.
    - Clock low time = 500 ns.
    - Watches Caravel reset/resetb on PIN_CARAVEL_RESET.
    - When reset is pressed, waits until reset is released.
    - Then sends: dummy 0, followed by the real 16-bit scan word, LSB first.

  Connections:
    PIN_XCLK    -> Caravel xclk, J11 pin 19 or M.2/U8 P9
    PIN_SCAN_DL -> Caravel ScanInDL, GPIO22
    PIN_SCAN_DR -> Caravel ScanInDR, GPIO21
    PIN_SCAN_CC -> Caravel ScanInCC, GPIO35, held low
    PIN_TM      -> Caravel TM, GPIO36
*/

#include <Arduino.h>

constexpr uint8_t PIN_XCLK          = 9;
constexpr uint8_t PIN_SCAN_DL       = 2;
constexpr uint8_t PIN_SCAN_DR       = 3;
constexpr uint8_t PIN_SCAN_CC       = 4;
constexpr uint8_t PIN_TM            = 5;
constexpr uint8_t PIN_CARAVEL_RESET = 6;
constexpr uint8_t PIN_LED           = 13;

#define XCLK_HIGH()      (CORE_PIN9_PORTSET = CORE_PIN9_BITMASK)
#define XCLK_LOW()       (CORE_PIN9_PORTCLEAR = CORE_PIN9_BITMASK)
#define SCAN_DL_HIGH()   (CORE_PIN2_PORTSET = CORE_PIN2_BITMASK)
#define SCAN_DL_LOW()    (CORE_PIN2_PORTCLEAR = CORE_PIN2_BITMASK)
#define SCAN_DR_HIGH()   (CORE_PIN3_PORTSET = CORE_PIN3_BITMASK)
#define SCAN_DR_LOW()    (CORE_PIN3_PORTCLEAR = CORE_PIN3_BITMASK)
#define SCAN_CC_LOW()    (CORE_PIN4_PORTCLEAR = CORE_PIN4_BITMASK)
#define TM_HIGH()        (CORE_PIN5_PORTSET = CORE_PIN5_BITMASK)
#define TM_LOW()         (CORE_PIN5_PORTCLEAR = CORE_PIN5_BITMASK)
#define LED_HIGH()       (CORE_PIN13_PORTSET = CORE_PIN13_BITMASK)
#define LED_LOW()        (CORE_PIN13_PORTCLEAR = CORE_PIN13_BITMASK)

constexpr uint32_t XCLK_HZ = 1000000UL;
constexpr uint32_t HALF_CYCLES = F_CPU / (2UL * XCLK_HZ);

static_assert(F_CPU % (2UL * XCLK_HZ) == 0,
              "Use a Teensy CPU speed divisible by 2 MHz. 600 MHz is recommended.");

#if F_CPU != 600000000UL
#warning "Timing was reviewed for Teensy 4.1 at 600 MHz."
#endif

constexpr bool CARAVEL_RESET_ACTIVE_LOW = true;

constexpr bool    OP_SET = true; // For set = true, read & reset = false
constexpr uint8_t SL_SEL = 0;
constexpr uint8_t BL_SEL = 0;
constexpr uint8_t WL_SEL = 0;

constexpr uint16_t makeScanWord(bool op_set, uint8_t sl_sel, uint8_t bl_sel, uint8_t wl_sel) {
  return ((uint16_t)(op_set ? 1U : 0U) << 15) |
         ((uint16_t)(sl_sel & 0x1FU) << 10) |
         ((uint16_t)(bl_sel & 0x1FU) << 5) |
         ((uint16_t)(wl_sel & 0x1FU));
}

constexpr uint16_t SCAN_WORD = makeScanWord(OP_SET, SL_SEL, BL_SEL, WL_SEL);
constexpr bool HOLD_TM_HIGH_AFTER_SCAN = true;

static uint32_t next_edge_cycle;
static bool reset_seen_asserted = false;

FASTRUN static inline void enableCycleCounter() {
  ARM_DEMCR |= ARM_DEMCR_TRCENA;
  ARM_DWT_CTRL |= ARM_DWT_CTRL_CYCCNTENA;
  ARM_DWT_CYCCNT = 0;
}

FASTRUN static inline void waitUntilCycle(uint32_t target) {
  while ((int32_t)(ARM_DWT_CYCCNT - target) < 0) {
  }
}

FASTRUN static inline void xclkRise() {
  waitUntilCycle(next_edge_cycle);
  XCLK_HIGH();
  next_edge_cycle += HALF_CYCLES;
}

FASTRUN static inline void xclkFall() {
  waitUntilCycle(next_edge_cycle);
  XCLK_LOW();
  next_edge_cycle += HALF_CYCLES;
}

FASTRUN static inline void xclkCycle() {
  xclkRise();
  xclkFall();
}

FASTRUN static void runIdleCycles(uint32_t cycles) {
  for (uint32_t i = 0; i < cycles; ++i) {
    xclkCycle();
  }
}

FASTRUN static inline bool caravelResetAsserted() {
  const bool pin_high = digitalReadFast(PIN_CARAVEL_RESET);
  return CARAVEL_RESET_ACTIVE_LOW ? !pin_high : pin_high;
}

FASTRUN static void waitForCaravelResetRelease() {
  while (caravelResetAsserted()) {
    TM_LOW();
    SCAN_DR_HIGH();
    SCAN_DL_LOW();
    xclkCycle();
  }
}

FASTRUN static inline void setScanDlBit(uint8_t value) {
  if (value) {
    SCAN_DL_HIGH();
  } else {
    SCAN_DL_LOW();
  }
}

FASTRUN static void shiftScanWord(uint16_t scan_word) {
  SCAN_DL_LOW();
  SCAN_DR_HIGH();

  TM_HIGH();
  xclkCycle();      // TM high first, DR still high, no scan shift

  SCAN_DL_LOW();    // first dummy/setup bit = 0
  SCAN_DR_LOW();    // active-low scan enable
  xclkCycle();      // first qualified clock only arms RTL

  for (uint8_t bit = 0; bit < 16; ++bit) {
    setScanDlBit((scan_word >> bit) & 1U);
    xclkCycle();
  }

  SCAN_DL_LOW();
  xclkCycle();      // final capture clock

  SCAN_DR_HIGH();
  SCAN_DL_LOW();
  runIdleCycles(4);

  if (!HOLD_TM_HIGH_AFTER_SCAN) {
    TM_LOW();
  }

  LED_HIGH();
}

FASTRUN static void handleResetTriggerIfNeeded() {
  if (!caravelResetAsserted()) {
    reset_seen_asserted = false;
    return;
  }

  if (reset_seen_asserted) {
    return;
  }

  reset_seen_asserted = true;
  LED_LOW();
  TM_LOW();
  SCAN_DR_HIGH();
  SCAN_DL_LOW();

  waitForCaravelResetRelease();
  shiftScanWord(SCAN_WORD);
}

void setup() {
  pinMode(PIN_XCLK, OUTPUT);
  pinMode(PIN_SCAN_DL, OUTPUT);
  pinMode(PIN_SCAN_DR, OUTPUT);
  pinMode(PIN_SCAN_CC, OUTPUT);
  pinMode(PIN_TM, OUTPUT);
  pinMode(PIN_CARAVEL_RESET, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);

  XCLK_LOW();
  SCAN_DL_LOW();
  SCAN_DR_HIGH();
  SCAN_CC_LOW();
  TM_LOW();
  LED_LOW();

  delay(10);
  noInterrupts();
  enableCycleCounter();
  next_edge_cycle = ARM_DWT_CYCCNT + HALF_CYCLES;
}

void loop() {
  xclkCycle();
  handleResetTriggerIfNeeded();
}
