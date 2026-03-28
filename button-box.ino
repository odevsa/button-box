#include <HID.h>
#include <FastLED.h>


// Uncomment to enable serial debug
// #define DEBUG

// Pin definitions (5x5 matrix)
const int PIN_BUTTON_ARRAY_ROWS[] = {2, 3}; // {2, 3, 4, 5, 6};
const int PIN_BUTTON_ARRAY_COLS[] = {7, 8}; // {7, 8, 9, 10, 14};

// LED strip config
#define LED_PIN A0

typedef struct LedState
{
  CRGB idle;
  CRGB pressed;
} LedState;

typedef struct ButtonPreference
{
  uint8_t colorIndex = 0;
  bool isToggle = false;  
} ButtonPreference;

enum LedStateIndex
{
  WHITE = 0,
  BLACK = 1,
  RED = 2,
  GREEN = 3,
  BLUE = 4,
  YELLOW = 5,
  MAGENTA = 6,
  CYAN = 7
};

const LedState LED_STATE_COLORS[] = {
  { idle: {0, 0, 0}, pressed: {100, 100, 100} }, // black
  { idle: {50, 50, 50}, pressed: {255, 255, 255} }, // white
  { idle: {50, 0, 0}, pressed: {255, 0, 0} }, // red
  { idle: {0, 50, 0}, pressed: {0, 255, 0} }, // green
  { idle: {0, 0, 50}, pressed: {0, 0, 255} }, // blue
  { idle: {50, 50, 0}, pressed: {255, 255, 0} }, // yellow
  { idle: {50, 0, 50}, pressed: {255, 0, 255} }, // magenta
  { idle: {0, 50, 50}, pressed: {0, 255, 255} }, // cyan
};

const uint8_t BUTTON_ROW_AMOUNT = sizeof(PIN_BUTTON_ARRAY_ROWS) / sizeof(PIN_BUTTON_ARRAY_ROWS[0]);
const uint8_t BUTTON_COL_AMOUNT = sizeof(PIN_BUTTON_ARRAY_COLS) / sizeof(PIN_BUTTON_ARRAY_COLS[0]);
const uint8_t BUTTON_AMOUNT = BUTTON_ROW_AMOUNT * BUTTON_COL_AMOUNT;
const uint8_t LED_STATE_AMOUNT = sizeof(LED_STATE_COLORS) / sizeof(LED_STATE_COLORS[0]);
CRGB leds[BUTTON_AMOUNT];


ButtonPreference preferences[BUTTON_AMOUNT];
bool currentToggleState[BUTTON_AMOUNT];
bool previousPhysicalState[BUTTON_AMOUNT];
uint32_t outputButtons = 0;

// Joystick Report ID
#define JOYSTICK_REPORT_ID 0x03

typedef struct JoystickReport
{
  uint32_t buttons;
} JoystickReport;


JoystickReport joystickReport = {0};

// HID descriptor: 25 buttons + padding to byte boundary
static const uint8_t _hidReportDescriptor[] PROGMEM = {
    0x05, 0x01,               // USAGE_PAGE (Generic Desktop)
    0x09, 0x04,               // USAGE (Joystick)
    0xa1, 0x01,               // COLLECTION (Application)
    0x85, JOYSTICK_REPORT_ID, //   REPORT_ID

    0x05, 0x09, //   USAGE_PAGE (Button)
    0x19, 0x01, //      USAGE_MINIMUM (Button 1)
    0x29, 0x19, //      USAGE_MAXIMUM (Button 25)
    0x15, 0x00, //      LOGICAL_MINIMUM (0)
    0x25, 0x01, //      LOGICAL_MAXIMUM (1)
    0x75, 0x01, //      REPORT_SIZE (1)
    0x95, 0x19, //      REPORT_COUNT (25)
    0x81, 0x02, //      INPUT (Data,Var,Abs)
    // Padding bits to align to full bytes (7 bits)
    0x95, 0x07, //      REPORT_COUNT (7)
    0x75, 0x01, //      REPORT_SIZE (1)
    0x81, 0x03, //      INPUT (Cnst,Var,Abs)

    0xc0 // END_COLLECTION
};

void pressButton(uint8_t button)
{
  bitSet(outputButtons, button);
}
void releaseButton(uint8_t button)
{
  bitClear(outputButtons, button);
}
void setButton(uint8_t button, bool pressed)
{
  pressed ? pressButton(button) : releaseButton(button);
}

void loadButtonArray()
{
  uint8_t index = 0;
  for (uint8_t row = 0; row < BUTTON_ROW_AMOUNT; row++)
  {
    digitalWrite(PIN_BUTTON_ARRAY_ROWS[row], LOW);
    
    for (uint8_t col = 0; col < BUTTON_COL_AMOUNT; col++) {
      bool physicalPressed = digitalRead(PIN_BUTTON_ARRAY_COLS[col]) == LOW;

      if (preferences[index].isToggle) {
        if (physicalPressed && !previousPhysicalState[index])
          currentToggleState[index] = !currentToggleState[index];
        
        setButton(index, currentToggleState[index]);
      } else {
        setButton(index, physicalPressed);
      }

      previousPhysicalState[index] = physicalPressed;
      index++;
    }
    
    digitalWrite(PIN_BUTTON_ARRAY_ROWS[row], HIGH);
  }
}

void updateLEDs()
{
  for (uint8_t i = 0; i < BUTTON_AMOUNT; i++) {
    const LedState color = LED_STATE_COLORS[preferences[i].colorIndex];
    bool pressed = ((outputButtons >> i) & 0x1) != 0;
    leds[i] = pressed ? color.pressed : color.idle;
  }
  FastLED.show();
}

void sendState()
{
  joystickReport.buttons = outputButtons;

  #ifdef DEBUG
  Serial.print("Buttons raw: ");
  Serial.println(outputButtons, BIN);
  Serial.println();
  #endif

  HID().SendReport(JOYSTICK_REPORT_ID, &joystickReport, sizeof(joystickReport));
}

void setup()
{
  #ifdef DEBUG
  Serial.begin(115200);
  delay(50);
  Serial.println("DEBUG: button array starting");
  #endif

  for (uint8_t i = 0; i < BUTTON_ROW_AMOUNT; i++)
    pinMode(PIN_BUTTON_ARRAY_ROWS[i], OUTPUT),
    digitalWrite(PIN_BUTTON_ARRAY_ROWS[i], HIGH);
  for (uint8_t i = 0; i < BUTTON_COL_AMOUNT; i++)
    pinMode(PIN_BUTTON_ARRAY_COLS[i], INPUT_PULLUP);

  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, BUTTON_AMOUNT);
  for (uint8_t i = 0; i < BUTTON_AMOUNT; i++) leds[i] = CRGB::Black;
  FastLED.show();

  static HIDSubDescriptor node(_hidReportDescriptor, sizeof(_hidReportDescriptor));
  HID().AppendDescriptor(&node);
}

void loop()
{
  outputButtons = 0;
  loadButtonArray();
  updateLEDs();
  sendState();
}
