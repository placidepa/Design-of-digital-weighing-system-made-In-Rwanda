#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "HX711.h"

// =====================================================
// Hardware Configuration
// =====================================================

constexpr uint8_t HX_DT_PIN     = 2;
constexpr uint8_t HX_SCK_PIN    = 3;

constexpr uint8_t TARE_BTN_PIN  = 4;

constexpr uint8_t CAL_LED_PIN   = 6;
constexpr uint8_t READY_LED_PIN = 7;

// =====================================================
// LCD
// =====================================================

LiquidCrystal_I2C lcd(0x27, 16, 2);

// =====================================================
// HX711
// =====================================================

HX711 scale;

// =====================================================
// Timing
// =====================================================

constexpr uint32_t SPLASH_TIME_MS      = 3000;
constexpr uint32_t LCD_UPDATE_MS       = 250;
constexpr uint32_t SERIAL_UPDATE_MS    = 500;
constexpr uint32_t DEBOUNCE_MS         = 50;
constexpr uint32_t BLINK_INTERVAL_MS   = 100;

// =====================================================
// FSM States
// =====================================================

enum class SystemState
{
    SPLASH,
    NORMAL,
    CALIBRATING
};

SystemState state = SystemState::SPLASH;

// =====================================================
// Variables
// =====================================================

long tareOffset = 0;

uint32_t stateEntryTime = 0;

uint32_t lastLCDUpdate = 0;
uint32_t lastSerialUpdate = 0;
uint32_t lastBlinkTime = 0;

bool blinkState = false;

// =====================================================
// Button Handling
// =====================================================

bool buttonStableState = HIGH;
bool buttonLastReading = HIGH;

uint32_t lastDebounceTime = 0;

// =====================================================
// Forward Declarations
// =====================================================

void enterState(SystemState newState);
bool tareButtonPressed();
void updateSplash();
void updateNormal();
void updateCalibrating();

// =====================================================

void setup()
{
    Serial.begin(115200);

    pinMode(TARE_BTN_PIN, INPUT_PULLUP);

    pinMode(CAL_LED_PIN, OUTPUT);
    pinMode(READY_LED_PIN, OUTPUT);

    digitalWrite(CAL_LED_PIN, LOW);
    digitalWrite(READY_LED_PIN, LOW);

    lcd.init();
    lcd.backlight();

    scale.begin(HX_DT_PIN, HX_SCK_PIN);

    enterState(SystemState::SPLASH);
}

// =====================================================

void loop()
{
    switch(state)
    {
        case SystemState::SPLASH:
            updateSplash();
            break;

        case SystemState::NORMAL:
            updateNormal();
            break;

        case SystemState::CALIBRATING:
            updateCalibrating();
            break;
    }
}

// =====================================================

void enterState(SystemState newState)
{
    state = newState;
    stateEntryTime = millis();

    switch(state)
    {
        case SystemState::SPLASH:

            lcd.clear();
            lcd.setCursor(0,0);
            lcd.print("UMUNZANI");

            lcd.setCursor(0,1);
            lcd.print("Made in Rwanda");

            digitalWrite(CAL_LED_PIN, LOW);
            digitalWrite(READY_LED_PIN, LOW);

            break;

        case SystemState::NORMAL:

            lcd.clear();

            digitalWrite(CAL_LED_PIN, LOW);
            digitalWrite(READY_LED_PIN, HIGH);

            break;

        case SystemState::CALIBRATING:

            lcd.clear();
            lcd.setCursor(0,0);
            lcd.print("Calibrating...");
            lcd.setCursor(0,1);
            lcd.print("Remove weight");

            digitalWrite(READY_LED_PIN, LOW);

            break;
    }
}

// =====================================================

void updateSplash()
{
    if(millis() - stateEntryTime >= SPLASH_TIME_MS)
    {
        enterState(SystemState::NORMAL);
    }
}

// =====================================================

void updateNormal()
{
    if(tareButtonPressed())
    {
        enterState(SystemState::CALIBRATING);
        return;
    }

    if(millis() - lastLCDUpdate >= LCD_UPDATE_MS)
    {
        lastLCDUpdate = millis();

        long raw = scale.read();

        lcd.setCursor(0,0);
        lcd.print("Ibiro:");

        lcd.setCursor(0,1);

        char buf[17];
        snprintf(buf,sizeof(buf),"RAW:%ld    ",raw);

        lcd.print(buf);
    }
}

// =====================================================

void updateCalibrating()
{
    // Fast blink LED

    if(millis() - lastBlinkTime >= BLINK_INTERVAL_MS)
    {
        lastBlinkTime = millis();

        blinkState = !blinkState;

        digitalWrite(CAL_LED_PIN, blinkState);
    }

    // Take tare automatically after 2 sec

    if(millis() - stateEntryTime > 2000 &&
       tareOffset == 0)
    {
        tareOffset = scale.read_average(20);

        Serial.println();
        Serial.println("===========");
        Serial.print("Offset: ");
        Serial.println(tareOffset);
        Serial.println("Place known weight now");
        Serial.println("===========");
    }

    if(tareOffset != 0 &&
       millis() - lastSerialUpdate >= SERIAL_UPDATE_MS)
    {
        lastSerialUpdate = millis();

        long raw = scale.read_average(5);

        Serial.print("Raw: ");
        Serial.println(raw);
    }

    // Press button again to exit

    if(tareOffset != 0 &&
       tareButtonPressed())
    {
        digitalWrite(CAL_LED_PIN, LOW);

        enterState(SystemState::NORMAL);
    }
}

// =====================================================

bool tareButtonPressed()
{
    bool reading = digitalRead(TARE_BTN_PIN);

    if(reading != buttonLastReading)
    {
        lastDebounceTime = millis();
    }

    if((millis() - lastDebounceTime) > DEBOUNCE_MS)
    {
        if(reading != buttonStableState)
        {
            buttonStableState = reading;

            if(buttonStableState == LOW)
            {
                buttonLastReading = reading;
                return true;
            }
        }
    }

    buttonLastReading = reading;
    return false;
}