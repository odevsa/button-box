#include <HID.h>
#include <FastLED.h>
#include <EEPROM.h>

// Uncomment to enable serial debug
// #define DEBUG

// ── Pin Configuration ──────────────────────────────────────────────

const uint8_t ROW_PINS[] = {2, 3};  // Full: {2, 3, 4, 5, 6};
const uint8_t COL_PINS[] = {7, 8};  // Full: {7, 8, 9, 10, 14};
#define LED_PIN A0

// ── Matrix Dimensions ──────────────────────────────────────────────

const uint8_t NUM_ROWS    = sizeof(ROW_PINS) / sizeof(ROW_PINS[0]);
const uint8_t NUM_COLS    = sizeof(COL_PINS) / sizeof(COL_PINS[0]);
const uint8_t NUM_BUTTONS = NUM_ROWS * NUM_COLS;

// ── Timing ─────────────────────────────────────────────────────────

const uint32_t HOLD_MS_EDIT  = 3000;
const uint32_t HOLD_MS_RESET = 6000;

// ── EEPROM Layout ──────────────────────────────────────────────────

const int     EEPROM_MAGIC_ADDR    = 0;
const uint8_t EEPROM_MAGIC_VALUE   = 0xA5;
const int     EEPROM_PREFS_ADDR    = 1;
const uint8_t EEPROM_BYTES_PER_BTN = 2;

// ── LED Colors ─────────────────────────────────────────────────────

struct LedColor {
  CRGB idle;
  CRGB pressed;
};

enum ColorIndex : uint8_t {
  COLOR_BLACK,
  COLOR_WHITE,
  COLOR_RED,
  COLOR_GREEN,
  COLOR_BLUE,
  COLOR_YELLOW,
  COLOR_MAGENTA,
  COLOR_CYAN,
  NUM_COLORS
};

const LedColor LED_COLORS[] = {
  { CRGB(  0,   0,   0), CRGB(100, 100, 100) },  // BLACK
  { CRGB( 50,  50,  50), CRGB(255, 255, 255) },  // WHITE
  { CRGB( 50,   0,   0), CRGB(255,   0,   0) },  // RED
  { CRGB(  0,  50,   0), CRGB(  0, 255,   0) },  // GREEN
  { CRGB(  0,   0,  50), CRGB(  0,   0, 255) },  // BLUE
  { CRGB( 50,  50,   0), CRGB(255, 255,   0) },  // YELLOW
  { CRGB( 50,   0,  50), CRGB(255,   0, 255) },  // MAGENTA
  { CRGB(  0,  50,  50), CRGB(  0, 255, 255) },  // CYAN
};

// ── Button Preferences ─────────────────────────────────────────────

struct ButtonPrefs {
  uint8_t colorIndex = 0;
  bool    isToggle   = false;
};

// ── Edit Mode ──────────────────────────────────────────────────────

enum EditStage : uint8_t {
  EDIT_NONE,
  EDIT_COLOR,
  EDIT_TOGGLE,
};

// ── HID Joystick ───────────────────────────────────────────────────

#define JOYSTICK_REPORT_ID 0x03

static const uint8_t HID_DESCRIPTOR[] PROGMEM = {
  0x05, 0x01,               // USAGE_PAGE (Generic Desktop)
  0x09, 0x04,               // USAGE (Joystick)
  0xA1, 0x01,               // COLLECTION (Application)
  0x85, JOYSTICK_REPORT_ID, //   REPORT_ID
  0x05, 0x09,               //   USAGE_PAGE (Button)
  0x19, 0x01,               //   USAGE_MINIMUM (Button 1)
  0x29, 0x19,               //   USAGE_MAXIMUM (Button 25)
  0x15, 0x00,               //   LOGICAL_MINIMUM (0)
  0x25, 0x01,               //   LOGICAL_MAXIMUM (1)
  0x75, 0x01,               //   REPORT_SIZE (1)
  0x95, 0x19,               //   REPORT_COUNT (25)
  0x81, 0x02,               //   INPUT (Data, Var, Abs)
  0x95, 0x07,               //   REPORT_COUNT (7) — padding to byte boundary
  0x75, 0x01,               //   REPORT_SIZE (1)
  0x81, 0x03,               //   INPUT (Cnst, Var, Abs)
  0xC0                      // END_COLLECTION
};

// ── Global State ───────────────────────────────────────────────────

CRGB        leds[NUM_BUTTONS];
ButtonPrefs preferences[NUM_BUTTONS];

bool     toggleState[NUM_BUTTONS];
bool     prevPhysical[NUM_BUTTONS];
bool     currPhysical[NUM_BUTTONS];
bool     pressEdge[NUM_BUTTONS];
bool     releaseEdge[NUM_BUTTONS];

uint32_t      outputButtons       = 0;
EditStage     editStage           = EDIT_NONE;
unsigned long editHoldStartMs     = 0;
bool          editHoldTriggered   = false;
bool          ignoreCornerRelease = false;

// ── Helpers ────────────────────────────────────────────────────────

inline bool isCornerButton(uint8_t i) {
  return i == 0 || i == NUM_BUTTONS - 1;
}

inline bool cornersPressed() {
  return currPhysical[0] && currPhysical[NUM_BUTTONS - 1];
}

inline bool cornersReleased() {
  return !currPhysical[0] && !currPhysical[NUM_BUTTONS - 1];
}

void clearEdges() {
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    pressEdge[i]   = false;
    releaseEdge[i] = false;
  }
}

// ── EEPROM ─────────────────────────────────────────────────────────

void savePreferences() {
  EEPROM.update(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VALUE);
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    int addr = EEPROM_PREFS_ADDR + i * EEPROM_BYTES_PER_BTN;
    EEPROM.update(addr,     preferences[i].colorIndex);
    EEPROM.update(addr + 1, preferences[i].isToggle ? 1 : 0);
  }
#ifdef DEBUG
  Serial.println(F("Preferences saved to EEPROM"));
#endif
}

void loadPreferences() {
  if (EEPROM.read(EEPROM_MAGIC_ADDR) != EEPROM_MAGIC_VALUE) {
#ifdef DEBUG
    Serial.println(F("No preferences in EEPROM, using defaults"));
#endif
    return;
  }
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    int addr = EEPROM_PREFS_ADDR + i * EEPROM_BYTES_PER_BTN;
    preferences[i].colorIndex = EEPROM.read(addr);
    preferences[i].isToggle   = (EEPROM.read(addr + 1) != 0);
  }
#ifdef DEBUG
  Serial.println(F("Preferences loaded from EEPROM"));
#endif
}

void resetPreferences() {
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    preferences[i].colorIndex = 0;
    preferences[i].isToggle   = false;
  }
  savePreferences();
}

// ── Button Matrix Scanning ─────────────────────────────────────────

void scanMatrix() {
  uint8_t index = 0;

  for (uint8_t row = 0; row < NUM_ROWS; row++) {
    digitalWrite(ROW_PINS[row], LOW);

    for (uint8_t col = 0; col < NUM_COLS; col++) {
      bool pressed = (digitalRead(COL_PINS[col]) == LOW);

      currPhysical[index] = pressed;
      pressEdge[index]    = pressed && !prevPhysical[index];
      releaseEdge[index]  = !pressed && prevPhysical[index];
      prevPhysical[index] = pressed;
      index++;
    }

    digitalWrite(ROW_PINS[row], HIGH);
  }
}

// ── Button Output Logic ────────────────────────────────────────────

void updateOutputs() {
  if (editStage != EDIT_NONE) return;

  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    if (preferences[i].isToggle) {
      if (pressEdge[i])
        toggleState[i] = !toggleState[i];
      bitWrite(outputButtons, i, toggleState[i]);
    } else {
      bitWrite(outputButtons, i, currPhysical[i]);
    }
  }
}

// ── Edit Mode Actions ──────────────────────────────────────────────

void applyEditActions() {
  if (editStage == EDIT_NONE) return;

  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    if (!releaseEdge[i]) continue;
    if (ignoreCornerRelease && isCornerButton(i)) continue;

    if (editStage == EDIT_COLOR) {
      preferences[i].colorIndex = (preferences[i].colorIndex + 1) % NUM_COLORS;
    } else if (editStage == EDIT_TOGGLE) {
      preferences[i].isToggle = !preferences[i].isToggle;
    }
  }
}

void advanceEditStage() {
  switch (editStage) {
    case EDIT_NONE:   editStage = EDIT_COLOR;  break;
    case EDIT_COLOR:  editStage = EDIT_TOGGLE; break;
    case EDIT_TOGGLE:
      editStage = EDIT_NONE;
      savePreferences();
      break;
  }
}

void handleEditModeTransition() {
  if (cornersPressed()) {
    if (editHoldStartMs == 0) {
      editHoldStartMs   = millis();
      editHoldTriggered = false;
    } else {
      unsigned long heldMs = millis() - editHoldStartMs;

      if (heldMs >= HOLD_MS_RESET) {
        resetPreferences();
        editStage           = EDIT_NONE;
        editHoldStartMs     = 0;
        outputButtons       = 0;
        ignoreCornerRelease = true;
        clearEdges();
      } else if (heldMs >= HOLD_MS_EDIT && !editHoldTriggered) {
        advanceEditStage();
        editHoldTriggered   = true;
        ignoreCornerRelease = true;
        clearEdges();
      }
    }
  } else {
    editHoldStartMs   = 0;
    editHoldTriggered = false;
  }

  if (ignoreCornerRelease && cornersReleased()) {
    ignoreCornerRelease = false;
  }
}

// ── LED Update ─────────────────────────────────────────────────────

void updateLEDs() {
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    const LedColor &color = LED_COLORS[preferences[i].colorIndex];

    switch (editStage) {
      case EDIT_COLOR:
        leds[i] = color.pressed;
        break;
      case EDIT_TOGGLE:
        leds[i] = preferences[i].isToggle ? color.pressed : CRGB::Black;
        break;
      default:
        leds[i] = bitRead(outputButtons, i) ? color.pressed : color.idle;
        break;
    }
  }
  FastLED.show();
}

// ── HID Report ─────────────────────────────────────────────────────

void sendHIDReport() {
  uint32_t report = outputButtons;

#ifdef DEBUG
  Serial.print(F("Buttons: "));
  Serial.println(outputButtons, BIN);
#endif

  HID().SendReport(JOYSTICK_REPORT_ID, &report, sizeof(report));
}

// ── Setup & Loop ───────────────────────────────────────────────────

void setup() {
#ifdef DEBUG
  Serial.begin(115200);
  delay(50);
  Serial.println(F("Button box starting"));
#endif

  for (uint8_t i = 0; i < NUM_ROWS; i++) {
    pinMode(ROW_PINS[i], OUTPUT);
    digitalWrite(ROW_PINS[i], HIGH);
  }
  for (uint8_t i = 0; i < NUM_COLS; i++) {
    pinMode(COL_PINS[i], INPUT_PULLUP);
  }

  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, NUM_BUTTONS);
  loadPreferences();

  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    leds[i] = CRGB::Black;
  }
  updateLEDs();

  static HIDSubDescriptor node(HID_DESCRIPTOR, sizeof(HID_DESCRIPTOR));
  HID().AppendDescriptor(&node);
}

void loop() {
  outputButtons = 0;

  scanMatrix();
  updateOutputs();
  applyEditActions();
  handleEditModeTransition();
  updateLEDs();
  sendHIDReport();
}
