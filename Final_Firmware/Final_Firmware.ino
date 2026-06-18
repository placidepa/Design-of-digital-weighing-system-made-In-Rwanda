#include <HX711.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

//====================================================
// Hardware
//====================================================

constexpr uint8_t HX_DT_PIN      = 2;
constexpr uint8_t HX_SCK_PIN     = 3;

constexpr uint8_t BTN_TARE_PIN   = 4;

constexpr uint8_t LED_CAL_PIN    = 6;
constexpr uint8_t LED_READY_PIN  = 7;

//====================================================
// Calibration
//====================================================

constexpr float DEFAULT_COUNTS_PER_GRAM = 103.1167f;
constexpr long  DEFAULT_TARE_OFFSET     = 159610;

//====================================================
// EEPROM
//====================================================

constexpr uint32_t EEPROM_SIGNATURE = 0x554D4E5AUL;

struct CalibrationData
{
  uint32_t signature;
  float countsPerGram;
  long tareOffset;
};

//====================================================
// Objects
//====================================================

HX711 scale;
LiquidCrystal_I2C lcd(0x27, 16, 2);

CalibrationData calib;

//====================================================
// FSM
//====================================================

enum class State
{
  SPLASH,
  READY,
  TARE
};

State state;

//====================================================
// Timing
//====================================================

uint32_t stateStartTime = 0;

uint32_t lastHXTime     = 0;
uint32_t lastLCDTime    = 0;
uint32_t lastBlinkTime  = 0;
uint32_t lastButtonTime = 0;

constexpr uint32_t HX_PERIOD_MS      = 20;
constexpr uint32_t LCD_PERIOD_MS     = 250;
constexpr uint32_t BUTTON_PERIOD_MS  = 10;
constexpr uint32_t BLINK_PERIOD_MS   = 100;

//====================================================
// Button Debounce
//====================================================

bool stableButtonState = HIGH;
bool lastButtonReading = HIGH;
uint32_t debounceStart = 0;

constexpr uint32_t DEBOUNCE_MS = 50;

//====================================================
// Filter
//====================================================

constexpr uint8_t FILTER_SIZE = 16;

long samples[FILTER_SIZE];

uint8_t sampleIndex = 0;
bool filterReady = false;

long filteredRaw = 0;

//====================================================
// EEPROM
//====================================================

void saveCalibration()
{
  EEPROM.put(0, calib);
}

void loadCalibration()
{
  EEPROM.get(0, calib);

  if (calib.signature != EEPROM_SIGNATURE)
  {
    calib.signature = EEPROM_SIGNATURE;
    calib.countsPerGram = DEFAULT_COUNTS_PER_GRAM;
    calib.tareOffset = DEFAULT_TARE_OFFSET;

    saveCalibration();
  }
}

//====================================================
// FSM
//====================================================

void enterState(State newState)
{
  state = newState;
  stateStartTime = millis();

  switch(state)
  {
    case State::SPLASH:

      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("UMUNZANI");

      lcd.setCursor(0,1);
      lcd.print("Made in Rwanda");

      digitalWrite(LED_READY_PIN, LOW);
      digitalWrite(LED_CAL_PIN, LOW);

      break;

    case State::READY:

      lcd.clear();

      digitalWrite(LED_READY_PIN, HIGH);
      digitalWrite(LED_CAL_PIN, LOW);

      break;

    case State::TARE:

      lcd.clear();

      lcd.setCursor(0,0);
      lcd.print("Calibrating...");
      lcd.setCursor(0,1);
      lcd.print("Please Wait");

      digitalWrite(LED_READY_PIN, LOW);

      break;
  }
}

//====================================================
// Button
//====================================================

bool tarePressed()
{
  bool reading = digitalRead(BTN_TARE_PIN);

  if (reading != lastButtonReading)
  {
    debounceStart = millis();
  }

  if ((millis() - debounceStart) > DEBOUNCE_MS)
  {
    if (reading != stableButtonState)
    {
      stableButtonState = reading;

      if (stableButtonState == LOW)
      {
        lastButtonReading = reading;
        return true;
      }
    }
  }

  lastButtonReading = reading;

  return false;
}

//====================================================
// Filter
//====================================================

void updateFilter()
{
  if (!scale.is_ready())
  {
    return;
  }

  long raw = scale.read();

  samples[sampleIndex] = raw;

  sampleIndex++;

  if(sampleIndex >= FILTER_SIZE)
  {
    sampleIndex = 0;
    filterReady = true;
  }

  uint8_t count =
      filterReady ?
      FILTER_SIZE :
      sampleIndex;

  if(count == 0)
  {
    return;
  }

  int64_t sum = 0;

  for(uint8_t i=0;i<count;i++)
  {
    sum += samples[i];
  }

  filteredRaw = sum / count;
}

//====================================================
// Weight
//====================================================

float getWeightKg()
{
  float grams =
    (filteredRaw - calib.tareOffset)
    / calib.countsPerGram;

  if (grams < 0)
    grams = 0;

  return grams / 1000.0f;
}

//====================================================
// Setup
//====================================================

void setup()
{
  Serial.begin(115200);

  pinMode(BTN_TARE_PIN, INPUT_PULLUP);

  pinMode(LED_CAL_PIN, OUTPUT);
  pinMode(LED_READY_PIN, OUTPUT);

  lcd.init();
  lcd.backlight();

  scale.begin(HX_DT_PIN, HX_SCK_PIN);

  loadCalibration();

  enterState(State::SPLASH);
}

//====================================================
// Loop
//====================================================

void loop()
{
  uint32_t now = millis();

  //====================================
  // Splash
  //====================================

  if(state == State::SPLASH)
  {
    if(now - stateStartTime >= 3000)
    {
      enterState(State::READY);
    }

    return;
  }

  //====================================
  // HX711 Sampling
  //====================================

  if(now - lastHXTime >= HX_PERIOD_MS)
  {
    lastHXTime = now;
    updateFilter();
  }

  //====================================
  // Button
  //====================================

  if(now - lastButtonTime >= BUTTON_PERIOD_MS)
  {
    lastButtonTime = now;

    if(tarePressed())
    {
      enterState(State::TARE);
    }
  }

  //====================================
  // TARE
  //====================================

  if(state == State::TARE)
  {
    if(now - lastBlinkTime >= BLINK_PERIOD_MS)
    {
      lastBlinkTime = now;

      digitalWrite(
        LED_CAL_PIN,
        !digitalRead(LED_CAL_PIN)
      );
    }

    if(now - stateStartTime >= 2000)
    {
      calib.tareOffset = scale.read_average(20);

      saveCalibration();

      digitalWrite(LED_CAL_PIN, LOW);

      enterState(State::READY);
    }

    return;
  }

  //====================================
  // Display
  //====================================

  if(now - lastLCDTime >= LCD_PERIOD_MS)
  {
    lastLCDTime = now;

    float weightKg = getWeightKg();

    lcd.setCursor(0,0);
    lcd.print("Ibiro:");

    lcd.setCursor(0,1);

    char buffer[17];

    dtostrf(weightKg, 6, 2, buffer);

    lcd.print(buffer);
    lcd.print(" Kg   ");

    Serial.print("Raw=");
    Serial.print(filteredRaw);

    Serial.print("  Weight=");
    Serial.print(weightKg, 3);

    Serial.println(" kg");
  }
}