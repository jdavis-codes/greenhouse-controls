
/*The sketch below runs the RESIDENT, a greenhouse attendant
INPUTS:
* temperature and humidity from DHT 22 --one for greehnouse and one for outside (ambient)
* soil moisture from an analog probe
* light from a photoresistor
*time and date from a DS3231 RTD module

OUTPUTS:
* micro SD card data log
* two 120 VAC relays for fans, auto-louvers
* two 24 VDC relays for reversing motors for roll up sides
* one 24 vdc relay for irrigation solenoid
* possible wifi connection for warning messages
* LCD I2C display
*/

#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>

//========================================================================BEGIN HARDWARE DRIVERS=========================================================

// Waveshare Board specific drivers
#include "I2C_Driver.h"
#include "I2C_Scanner.h"
#include "WS_GPIO.h"
#include "WS_Relay.h"
#include "WS_RTC.h"

// Modbus SHT20 temp/humidity sensor over RS485
#include "ModbusSHT20.h"

#define RS485_RX_PIN 18
#define RS485_TX_PIN 17

ModbusSHT20 modbusSht20(RS485_RX_PIN, RS485_TX_PIN);

//========================================================================END HARDWARE DRIVERS=========================================================

//========================================================================BEGIN DECLARATIONS=========================================================

// Note: The Waveshare board uses a TCA9554PWR I2C expander for its relays
// Therefore, we no longer declare direct digital/analog output pins for relays.
// Instead, we will map logic directly to Relay_Open() / Relay_Close() using the WS_Relay library.

#define INSOLATION_PIN 4      // analog read for the light meter (using a safe ESP32 pin)
#define SOIL_MOISTURE_PIN 5   // analog read for the soil moisture probe (using a safe ESP32 pin)
// RS485 handles both ambient and greenhouse Temp/Hum via Modbus Slave IDs.

#define GREENHOUSE_SENSOR_ID 1
#define AMBIENT_SENSOR_ID 2 

float grnhouseTemp; // these 4 variables will store the temperatures (in Celcius -> Fahrenheit) and humidities (%rH) from the sensors
float grnhouseHum;
float ambientTemp;
float ambientHum;

// float grnhouseTargetTemp1 = 85.0 ; //Fahrenheit! these are the variables for two target temperatures in the greehnouse and a delta, or hysteresis
float grnhouseTargetTemp1 = 80.0; // for testing, comment out when done
// float grnhouseTargetTemp2 = 100.0; //Fahrenheit!
float grnhouseTargetTemp2 = 85.0; // for testing, comment out when done

float grnhouseTempDelta = 4.0; // Fahrenheit!

int soilMoisture;            // this stores the soil moisture as a number 0-100
int soilTargetMoisture = 50; // this is the moisture expressed on a 0-100 scale
int soilMoistureDelta = 10;  // this is the hysteresis for the soil moisture target

bool motorUp = false; // this variable stores the position of the motor (1 or true means motor is UP, 0 or false means motor is DOWN)
bool fanOn = false;   // this variable stores the condition of the fan (1 means fan is ON, 0 means fan is OFF)
bool waterOn = false; // this variable stores the condition of the irrigation solenoid (1 means ON, 0 means OFF)

int insolation; // this variable stores the light level on a 0-100 scale

#include "GreenhouseTelegram.h"
#include "secrets.h"

RingBuffer* logBuffer = nullptr;
GreenhouseTelegramBot telBot(BOT_TOKEN, MODE_DASHBOARD);

// Variables for task/timer logic
constexpr unsigned long readInterval = 3000UL;
unsigned long readTime;
unsigned long startTime2 = 0;

// variables for the timer on the telegram dashboard history
unsigned long historyTime;
unsigned long startTime3 = 0;
constexpr unsigned long historyInterval = 5UL * 60UL * 1000UL;                      // 5 minutes

// Relay mapping for Waveshare board (1 through 8 available)
#define RELAY_FAN         1
#define RELAY_SIDES_UP    3 
#define RELAY_SIDES_DOWN  4 
#define RELAY_WATER       5 

// Telegram Sensor and Event Metadata mapping directly to LogEntry fields
SensorMetadata activeSensors[] = {
    {"Green House Temperature", "°F", "#ff4d4d", &LogEntry::grnhouseTemp},
    {"Ambient Temperature", "°F", "#ffb54d", &LogEntry::ambientTemp},
    {"Green House Humidity", "%", "#007bff", &LogEntry::grnhouseHum},
    {"Ambient Humidity", "%", "#9d00ff", &LogEntry::ambientHum},
    {"Insolation", "%", "#ffff4d", &LogEntry::insolation},
    {"Soil Moisture", "%", "#4dff4d", &LogEntry::soilMoisture}
};

// Forward declare callbacks
void onFanTelegramTrigger(bool state);
void onSidesTelegramTrigger(bool state);
void onIrrigationTelegramTrigger(bool state);

EventMetadata activeEvents[] = {
    {"Exhaust Fan", "🟩", "  ", "ON", "OFF", "#32cd32", &LogEntry::fanOn, onFanTelegramTrigger},
    {"Roller Sides", "🟧", "  ", "UP", "DOWN", "#ffa500", &LogEntry::motorUp, onSidesTelegramTrigger},
    {"Irrigation", "💧", "  ", "ON", "OFF", "#1e90ff", &LogEntry::waterOn, onIrrigationTelegramTrigger}
};

SettingsParameter botSettings[] = {
    {"🌡️", "Target Temp 1", &grnhouseTargetTemp1, "°F"},
    {"🌡️", "Target Temp 2", &grnhouseTargetTemp2, "°F"},
    {"📈", "Temp Delta", &grnhouseTempDelta, "°F"},
    {"💧", "Target Moisture", (float *)&soilTargetMoisture, "%"}, // Need cast safely or modify types
    {"📉", "Moisture Delta", (float *)&soilMoistureDelta, "%"}};
// ========================================================= END DECLARATIONS ===============================================

//==============================================================BEGIN SET UP======================================================================
void setup()
{

  Serial.begin(9600);

  // Start WiFi
  Serial.print("Connecting to WiFi ");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");


  // Setup Telegram Bot
  telBot.begin(WIFI_SSID, WIFI_PASSWORD, activeSensors, activeEvents, botSettings);
  
  // Get our own logBuffer pointer for logic to update
  logBuffer = telBot.getLogBuffer();

  // wait for Serial Monitor to connect. 
  // Disable blocking wait so headless runs don't hang
  // while (!Serial); 

  printf("\n--- Greenhouse Controller Booting ---\r\n");

  // Waveshare board initialization sequence:
  // Initialize I2C first, as both Relay Expander and RTC sit on it.
  I2C_Init();
  // I2C_Scanner::scan();  // Print all I2C devices on the bus
  
  // Initialize Buzzer and RGB LED
  GPIO_Init();

  // Initializes TCA9554PWR and starts the background RelayFailTask
  Relay_Init();
  printf("Relay Expanders Initialized.\r\n");

  // Initialize PCF85063 and start tasks AFTER setting the time if necessary
  RTC_Init();
  printf("RTC Initialized.\r\n");
  
  // Initialize Modbus for RS485 sensors (SHT20)
  if (!modbusSht20.begin(9600)) {
    Serial.println("Failed to start Modbus RTU Client!");
    // We don't halt here, we might just fail reading later.
  } else {
    Serial.println("Modbus started.");
  }

  // set up local pins as input
  pinMode(INSOLATION_PIN, INPUT);
  pinMode(SOIL_MOISTURE_PIN, INPUT);

  // Default Relays to closed (OFF)
  Relay_Close(RELAY_FAN);
  Relay_Close(RELAY_SIDES_UP);
  Relay_Close(RELAY_SIDES_DOWN);
  Relay_Close(RELAY_WATER);

  // do initial reading to prepopulate history arrays so graphs are instantly populated
  delay(2000);
  readSensors();
  updateHistoryArrays(getRTCUnixTime());
}

//==============================================================END SET UP=======================================================================

//===============================================================BEGIN MAIN LOOP==================================================================

void loop()
{
  readTime = (millis() - startTime2);
  if (readTime > readInterval)
  {
    readSensors();         // gets readings from sensors
    startTime2 = millis(); // resets the timer
  }
  // displayLCD(); // displays things on LCD (4x20) - Removed from Waveshare target

  logicAndControl(); // controls the relay based on the conditions

  printToMonitor(); // prints data to Serial Monitor if a computer is connected with an active serial monitor

  // writeToSD(); // removed for now unless refitting SPI CS for Waveshare later

  historyTime = (millis() - startTime3);
  if (historyTime > historyInterval)
  {
    updateHistoryArrays(getRTCUnixTime());
    telBot.refreshDashboard();
    startTime3 = millis();
  }

  bot_loop(); // Handles incoming queries/messages for telegram

  delay(100);
}

//==========================================================END MAIN LOOP=======================================================

//===========================================================BEGIN SUBROUTINES==============================================================

//===============================================SUBROUTINE TO READ TEMPERATURE AND HUMIDITY WITH MODBUS SHT20=============================================

void readSensors(void)
{
  float t, h;
  // Read Greenhouse Unit
  if (modbusSht20.readSensor(GREENHOUSE_SENSOR_ID, t, h)) {
      grnhouseTemp = ((t * 9.0) / 5.0 + 32.0); // Convert C to F
      grnhouseHum = h;
  } else {
      Serial.println("Failed to read from Greenhouse SHT20 (ID 1)");
  }

  // Read Ambient Unit
  if (modbusSht20.readSensor(AMBIENT_SENSOR_ID, t, h)) {
      ambientTemp = ((t * 9.0) / 5.0 + 32.0); // Convert C to F
      ambientHum = h;
  } else {
      Serial.println("Failed to read from Ambient SHT20 (ID 2)");
  }

  // now read the soil moisture and convert to 0-100 (Assumes Analog)
  soilMoisture = map(analogRead(SOIL_MOISTURE_PIN), 0, 4095, 100, 0); // ESP32 ADC is 12-bit (0-4095)

  // now read the light level and convert to 0-100
  insolation = map(analogRead(INSOLATION_PIN), 0, 4095, 0, 100);
}
//=================================================END SENSOR GATHERING===========================================================

void printToMonitor(void)
{
  //=====================================================SUBROUTINE TO PRINT TO SERIAL MONITOR===========================================================
  char ts[30];
  datetime_to_str(ts, datetime);
  Serial.print(ts);
  Serial.print(F(" -- "));

  // Printing the temperature and humidity on the serial monitor

  Serial.print(F("Greenhouse Temperature F:  "));
  Serial.println(grnhouseTemp);
  Serial.print(F("Greenhouse Humidity:  "));
  Serial.println(grnhouseHum);

  Serial.print(F("ambient Temperature F:  "));
  Serial.println(ambientTemp);
  Serial.print(F("ambient Humidity:  "));
  Serial.println(ambientHum);

  Serial.print(F("Soil Moisture:  "));
  Serial.println(soilMoisture);

  Serial.print(F("Sunlight Insolation:  "));
  Serial.println(insolation);

  if (motorUp == true)
  {
    Serial.println(F("motor is UP"));
  }
  else
  {
    Serial.println(F("motor is DOWN"));
  }

  if (fanOn == true)
  {
    Serial.println(F("fan is ON"));
  }
  else
  {
    Serial.println(F("fan is OFF"));
  }
  if (waterOn == true)
  {
    Serial.println(F("water is ON"));
  }
  else
  {
    Serial.println(F("water is OFF"));
  }

  delay(50);
}  //=======================================================END SERIAL MONITOR SUBROUTINE===============================================

void logicAndControl()
{

  if (motorUp == false)
  { // if the sides are down...
    if (grnhouseTemp >= (grnhouseTargetTemp1 + grnhouseTempDelta))
    {                                  // and if the greenhouse is hotter than the target...
      Relay_Open(RELAY_SIDES_UP); // this activates the 24 vdc relay scheme to open the roll-up sides
      // delay (45 * 1000); //wait 45 seconds for roll-up motor to do its thing
      delay(5000);                    // wait 5 seconds to check action of relay
      Relay_Close(RELAY_SIDES_UP); // this turns off the relay so it isn't consuming power
      motorUp = true;                 // changes the state of the motorUp variable to true which indicates the sides are open
    }
  }

  if (motorUp == true)
  { // if the sides are up...
    if (grnhouseTemp <= (grnhouseTargetTemp1 - grnhouseTempDelta))
    { // this checks if the greenhouse is colder than the target, and...
      Relay_Open(RELAY_SIDES_DOWN); // this activates the 24 vdc relay scheme to close the roll-up sides
      // delay (45 * 1000); //wait 45 seconds for roll-up motor to do its thing
      delay(5000);                      // wait 5 seconds to check action of relay
      Relay_Close(RELAY_SIDES_DOWN); // this turns off the relay so it isn't consuming power
      motorUp = false;                  // changes the state of the motorUp variable to false which indicates the sides are closed
    }
  }

  if (fanOn == false)
  { // if the fan is off...
    if (grnhouseTemp >= (grnhouseTargetTemp2 + grnhouseTempDelta))
    { // and if the greenhouse is hotter than the target...
      // open the louver
      Relay_Open(RELAY_FAN); // this activates the relay for the fan
      fanOn = true;                     // changes the state of the fanOn variable to true which indicates the fan is running
    }
  }

  if (fanOn == true)
  { // if the sides
    if (grnhouseTemp <= (grnhouseTargetTemp2 - grnhouseTempDelta))
    {                                  // this checks if the greenhouse is colder than the target, and...
      Relay_Close(RELAY_FAN); // this opens the relay and turns off the fan
      // close the louver
      fanOn = false; // changes the state of the fanOn variable to false which indicates the fan is not running
    }
  }

  if (waterOn == false)
  { // if the irrigation is off...
    if (soilMoisture <= (soilTargetMoisture - soilMoistureDelta))
    {                                   // and if the soil is dryer than the target...
      Relay_Open(RELAY_WATER); // this opens the relay for the irrigation solenoid
      waterOn = true;                   // changes the state of the waterOn variable to indicate the irrigation is on
    }
  }

  if (waterOn == true)
  { // if the irrigation is on...
    if (soilMoisture >= (soilTargetMoisture + soilMoistureDelta))
    {                                  // and if the soil is wetter than the target...
      Relay_Close(RELAY_WATER); // this closes the relay for the irrigation solenoid
      waterOn = false;                 // changes the state of the waterOn variable to indicate the irrigation is off
    }
  }
}

//====================================================================END LOGIC AND CONTROL SUBROUTINE================================================

//==============================================================BEGIN TELEGRAM LOGIC AND CALLBACKS===================================================

// Callbacks for Telegram manual overrides
void onFanTelegramTrigger(bool state)
{
  fanOn = state;
  if(state) Relay_Open(RELAY_FAN); else Relay_Close(RELAY_FAN);
}

void onIrrigationTelegramTrigger(bool state)
{
  waterOn = state;
  if(state) Relay_Open(RELAY_WATER); else Relay_Close(RELAY_WATER);
}

void onSidesTelegramTrigger(bool state)
{
  motorUp = state;
  if (state) {
      Relay_Open(RELAY_SIDES_UP);
      Relay_Close(RELAY_SIDES_DOWN);
  } else {
      Relay_Close(RELAY_SIDES_UP);
      Relay_Open(RELAY_SIDES_DOWN);
  }
  delay(5000);
  Relay_Close(RELAY_SIDES_UP);
  Relay_Close(RELAY_SIDES_DOWN);
}

void updateHistoryArrays(uint32_t ts)
{
  if (!logBuffer) return;

  LogEntry newEntry;
  newEntry.timestamp = ts;
  newEntry.grnhouseTemp = grnhouseTemp;
  newEntry.ambientTemp = ambientTemp;
  newEntry.grnhouseHum = grnhouseHum;
  newEntry.ambientHum = ambientHum;
  newEntry.insolation = insolation;
  newEntry.soilMoisture = soilMoisture;

  newEntry.fanOn = fanOn;
  newEntry.motorUp = motorUp;
  newEntry.waterOn = waterOn;

  logBuffer->push_back(newEntry);
}

void bot_loop()
{
  telBot.tick();
}

//=====================================================================END OF PROGRAM==================================================================
