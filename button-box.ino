#include <HID.h>

// Uncomment to enable serial debug
// #define DEBUG

// Pin definitions (5x5 matrix)
const int PIN_BUTTON_ARRAY_ROWS[] = {2, 3, 4, 5, 6};
const int PIN_BUTTON_ARRAY_COLS[] = {7, 8, 9, 10, 14};

// Joystick Report ID
#define JOYSTICK_REPORT_ID 0x03

typedef struct JoystickReport
{
  uint32_t buttons;
 } JoystickReport;

int buttonArrayRowSize = sizeof(PIN_BUTTON_ARRAY_ROWS) / sizeof(PIN_BUTTON_ARRAY_ROWS[0]);
int buttonArrayColSize = sizeof(PIN_BUTTON_ARRAY_COLS) / sizeof(PIN_BUTTON_ARRAY_COLS[0]);

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

uint32_t outputButtons = 0;
int buttonCurrentIndex = 0;

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
  for (int row = 0; row < buttonArrayRowSize; row++)
  {
    digitalWrite(PIN_BUTTON_ARRAY_ROWS[row], LOW);
    
    for (int col = 0; col < buttonArrayRowSize; col++)
      setButton(buttonCurrentIndex++, digitalRead(PIN_BUTTON_ARRAY_COLS[col]) == LOW);
    
    digitalWrite(PIN_BUTTON_ARRAY_ROWS[row], HIGH);
  }
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

  for (int i = 0; i < buttonArrayRowSize; i++)
    pinMode(PIN_BUTTON_ARRAY_ROWS[i], OUTPUT),
    digitalWrite(PIN_BUTTON_ARRAY_ROWS[i], HIGH);
  for (int i = 0; i < buttonArrayColSize; i++)
    pinMode(PIN_BUTTON_ARRAY_COLS[i], INPUT_PULLUP);

  static HIDSubDescriptor node(_hidReportDescriptor, sizeof(_hidReportDescriptor));
  HID().AppendDescriptor(&node);
}

void loop()
{
  buttonCurrentIndex = 0;
  outputButtons = 0;
  loadButtonArray();
  sendState();
}
