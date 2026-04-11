/*The sketch below runs the FIELD NODE (Arduino Nano + LoRa radio), a greenhouse control node.
INPUTS:
* temperature and humidity from DHT22 -- one for greenhouse and one for outside (ambient)
* soil moisture from an analog probe
* light from a photoresistor
* time and date from a DS3231 RTC module

OUTPUTS:
* micro SD card data log
* two 120 VAC relays for fan and louver
* two 24 VDC relays for reversing motors for roll-up sides
* one 24 VDC relay for irrigation solenoid
* LCD I2C display
* LoRa radio uplink to Telegram forwarder node
*/


//========================================================================BEGIN DECLARATIONS=========================================================

#include <Arduino.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include "GreenhouseControlNode.h"
#include "RYLR_LoRaAT_Software_Serial.h"

// LoRa radio uses SoftwareSerial on pins 5/6 (pins 2/3 are reserved for the DHT22 sensors)
#define LORA_RX 5
#define LORA_TX 6

// Address of this sender and the forwarder we are sending to
#define LOCAL_ADDRESS  1
#define REMOTE_ADDRESS 2

#include <RTClib.h> //includes the library for the DS3231 real-time clock module
RTC_DS3231 rtc;     // sets up the time module

#include <LiquidCrystal_I2C.h> //includes the library for the 20x4 I2C LCD display
LiquidCrystal_I2C lcd(0x27, 20, 4); // I2C address 0x27, 20 columns and 4 rows

#include "DHTStable.h" //include the DHTStable library
DHTStable DHTgreenhouse; //creates an instance of the DHTStable class for the greenhouse sensor
DHTStable DHTambient;    //creates an instance of the DHTStable class for the ambient sensor

#define relayZeroPin     A0 //declare all relay and sensor pins; relay 0 is the 120 VAC fan relay
#define relayOnePin      A1 //relay 1 is the 120 VAC louver relay
#define relayTwoPin      A2 //relay 2 activates the roll-up sides motor in the UP direction (24 VDC)
#define relayThreePin    A3 //relay 3 activates the roll-up sides motor in the DOWN direction (24 VDC)
//A4 pin is shared (by necessity) between the RTC clock SDA and the LCD I2C SDA
//A5 pin is shared (by necessity) between the RTC clock SCL and the LCD I2C SCL
#define relayFourPin     4  //relay 4 is the irrigation solenoid relay -- uses DIGITAL PIN 4
#define insolationPin    A6 //analog read for the light meter
#define soilMoisturePin  A7 //analog read for the soil moisture probe
#define grnhousePin      2  //DHT22 pin for greenhouse temperature and humidity
#define grnhousePowerPin 3  //DHT22 power pin -- allows automatic rebooting of the sensor
#define speakerPin       7  //output to speaker for pre-action melodies
#define ambientPin       8  //DHT22 pin for ambient temperature and humidity
#define ambientPowerPin  9  //DHT22 power pin -- allows automatic rebooting of the sensor
//digital pin 10 is the CS pin on the microSD card module
//digital pin 11 is (by necessity) the microSD card module MOSI
//digital pin 12 is (by necessity) the microSD card module MISO
//digital pin 13 is (by necessity) the microSD card module SCK

#include <SPI.h> //library for Serial Peripheral Interface communication
#include <SD.h>  //library for the microSD card
const int chipSelect = 10; //sets pin 10 for CS on the microSD module pinout
File myFile; //creates a file object for the microSD card

//these variables store the temperatures (in Fahrenheit!) and humidities (%rH) from the sensors
int greenhouseTemp     = 0;
int greenhouseHumidity = 0;
int ambientTemp        = 0;
int ambientHumidity    = 0;

//float targetTemp1 = 85.0; //Fahrenheit! -- production targets, comment out for testing
//float targetTemp2 = 100.0; //Fahrenheit!
float targetTemp1    = 80.0f; //for testing, comment out when done
float targetTemp2    = 85.0f; //for testing, comment out when done
float tempDelta      = 4.0f;  //Fahrenheit! -- hysteresis for the temperature targets

int   soilMoisture   = 0;     //stores the soil moisture as a number 0-100
float targetMoisture = 50.0f; //target soil moisture expressed on a 0-100 scale
float moistureDelta  = 10.0f; //hysteresis for the soil moisture target

bool fanOn        = false; //stores the condition of the fan (true = ON)
bool sidesUp      = false; //stores the position of the roll-up sides (true = UP)
bool irrigationOn = false; //stores the condition of the irrigation solenoid (true = ON)

int insolation = 0; //stores the light level on a 0-100 scale

//variables for the timer on the microSD card write function
unsigned long writeInterval = 5000;
unsigned long runTime;
unsigned long startTime1 = 0;

//variables for the timer on the sensor read function
unsigned long readInterval = 3000;
unsigned long readTime;
unsigned long startTime2 = 0;

//====================================begin declarations for tone melody subroutines================================
#include "pitches.h" //gets library of tones
#define REST 0       //defines rest as zero tones
//=======end declarations for tone melody subroutines=============

RYLR_LoRaAT_Software_Serial rylr;
GreenhouseControlNode greenhouseNode;
SoftwareSerial radioSerial(LORA_RX, LORA_TX);

constexpr telegram_data_pipe data_pipe = radio;

//==========================================================END DECLARATIONS======================================================================


//forward declare all callbacks and subroutines
void onFanSet(bool state);
void onSidesSet(bool state);
void onWaterSet(bool state);

GreenhouseControlNode::SensorBinding sensors[] = {
    {"GH_TEMP",  &greenhouseTemp},
    {"GH_HUM",   &greenhouseHumidity},
    {"SOIL",     &soilMoisture},
    {"SUN",      &insolation},
    {"AMB_TEMP", &ambientTemp},
    {"AMB_HUM",  &ambientHumidity}
};

GreenhouseControlNode::EventBinding events[] = {
    {"FAN",   &fanOn,        onFanSet},
    {"SIDES", &sidesUp,      onSidesSet},
    {"WATER", &irrigationOn, onWaterSet}
};

GreenhouseControlNode::SettingBinding settings[] = {
    {"TEMP1",  &targetTemp1},
    {"TEMP2",  &targetTemp2},
    {"TDELTA", &tempDelta},
    {"MOIST",  &targetMoisture},
    {"MDELTA", &moistureDelta}
};


#define ENABLE_DEBUG_SERIAL // comment this our if you're running out of memory and don't need debug prints -- saves about 200 bytes of flash and 10 bytes of RAM

#ifdef ENABLE_DEBUG_SERIAL
  #define DEBUG_PRINT(...)   Serial.print(__VA_ARGS__)
  #define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
  #define DEBUG_PRINT(...)
  #define DEBUG_PRINTLN(...)
#endif

int freeRam() {
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}

//==============================================================BEGIN SET UP======================================================================

void setup() {

  // Avoid blocking forever on boards without native USB serial.
  Serial.begin(9600);
  unsigned long serialWaitStart = millis();
  while (!Serial && (millis() - serialWaitStart < 1500)) {
  }

  DEBUG_PRINTLN(F("BOOT: setup start"));

  greenhouseNode.setupStatusLed();
  DEBUG_PRINTLN(F("BOOT: status LED ready"));

  Wire.begin();
  DEBUG_PRINTLN(F("BOOT: I2C bus ready"));

  setupSD(); //setup SD card logger
  DEBUG_PRINTLN(F("BOOT: SD ready"));
  delay(20);

  if (!rtc.begin()) {
    DEBUG_PRINTLN(F("BOOT: RTC not detected"));
  } else {
    DEBUG_PRINTLN(F("BOOT: RTC ready"));
  }

  DEBUG_PRINTLN(F("BOOT: init LCD"));
  setupLCD();
  DEBUG_PRINTLN(F("BOOT: LCD ready"));

  initializePins(); //set up pins as input or output and power on the temp/humidity sensors
  DEBUG_PRINTLN(F("BOOT: pins ready"));

  greenhouseNode.configure(sensors, events, settings); //no sample callback; sensors are read manually on a timer
  DEBUG_PRINTLN(F("BOOT: node configured"));

  setupRadio();
  DEBUG_PRINT(F("BOOT: freeRam="));
  DEBUG_PRINTLN(freeRam());
  Serial.flush();
  DEBUG_PRINTLN(F("BOOT: setup complete"));
  Serial.flush();
}

//==============================================================END SET UP=======================================================================

//===============================================================BEGIN MAIN LOOP==================================================================

void loop() {
  DEBUG_PRINT(F("LOOP ram="));
  DEBUG_PRINTLN(freeRam());
  Serial.flush();

  readInterval = (1000 * 3);         //the read interval in milliseconds for readSensors()
  readTime     = (millis() - startTime2);
  if (readTime > readInterval) {
    DEBUG_PRINTLN(F("LOOP: readSensors")); Serial.flush();
    readSensors();                   //gets readings from all sensors
    startTime2 = millis();           //resets the timer
  }

  DEBUG_PRINTLN(F("LOOP: displayLCD")); Serial.flush();
  displayLCD();                      //displays readings on the 20x4 LCD
  DEBUG_PRINTLN(F("LOOP: logic")); Serial.flush();
  logicAndControl();                 //controls the relays based on sensor conditions
  DEBUG_PRINTLN(F("LOOP: tick")); Serial.flush();
  greenhouseNode.tick(millis());     //handles telemetry transmission and incoming LoRa commands
  DEBUG_PRINTLN(F("LOOP: replies")); Serial.flush();
  receiveReplies();                  //processes any incoming LoRa reply messages

  DEBUG_PRINTLN(F("LOOP: print")); Serial.flush();
  printToMonitor();                  //prints data to the Serial Monitor if a computer is connected

  writeInterval = (1000 * 5);        //writes every 5 seconds
  //writeInterval = 900000;          //writes every 15 minutes
  runTime = (millis() - startTime1);
  if (runTime > writeInterval) {
    DEBUG_PRINTLN(F("LOOP: writeSD")); Serial.flush();
    writeToSD();                     //writes data to the microSD card
    startTime1 = millis();
  }

  DEBUG_PRINTLN(F("LOOP: end")); Serial.flush();
  delay(100);
}

//==========================================================END MAIN LOOP=======================================================

//===========================================================BEGIN SUBROUTINES==============================================================

//===========================================================SETUP SUBROUTINES================================================================
void setupSD() {
  DEBUG_PRINT(F("SD init..."));

  if (!SD.begin(chipSelect)) {
    DEBUG_PRINTLN(F("FAIL! Check card/wiring."));
    while (true);
  }
  DEBUG_PRINTLN(F("OK"));
}

void setupLCD() {
  lcd.init();     //initialize the lcd object, clear it, turn on the backlight
  lcd.clear();
  lcd.backlight();
  lcd.setCursor(0, 0);
}

void initializePins() {
  pinMode(relayZeroPin,     OUTPUT);
  pinMode(relayOnePin,      OUTPUT);
  pinMode(relayTwoPin,      OUTPUT);
  pinMode(relayThreePin,    OUTPUT);
  pinMode(relayFourPin,     OUTPUT);
  pinMode(insolationPin,    INPUT);
  pinMode(soilMoisturePin,  INPUT);
  pinMode(grnhousePin,      INPUT);
  pinMode(grnhousePowerPin, OUTPUT);
  pinMode(speakerPin,       OUTPUT);
  pinMode(ambientPin,       INPUT);
  pinMode(ambientPowerPin,  OUTPUT);

  digitalWrite(grnhousePowerPin, HIGH); //power on the greenhouse DHT22
  digitalWrite(ambientPowerPin,  HIGH); //power on the ambient DHT22
  digitalWrite(relayZeroPin,  LOW);     //turn off the fan relay
  digitalWrite(relayOnePin,   LOW);     //turn off the louver relay
  digitalWrite(relayTwoPin,   LOW);     //turn off the roll-up sides UP relay
  digitalWrite(relayThreePin, LOW);     //turn off the roll-up sides DOWN relay
  digitalWrite(relayFourPin,  LOW);     //turn off the irrigation relay
}

//=============================================================END SETUP SUBROUTINES===============================================================


//===============================================SUBROUTINE TO READ TEMPERATURE, HUMIDITY, AND ANALOG SENSORS=============================================

void readSensors() {
  int chkGreenhouse = DHTgreenhouse.read22(grnhousePin); //check the greenhouse sensor for read errors -- ok=0; errors are 1, 2, etc.
  int chkAmbient    = DHTambient.read22(ambientPin);     //check the ambient sensor for read errors
  if (chkGreenhouse != 0) { //if the read returns other-than-zero, there is a sensor problem
    printDHTError(chkGreenhouse);
    rebootDHT22(grnhousePowerPin);
    delay(10);
  }
  if (chkAmbient != 0) {
    printDHTError(chkAmbient);
    rebootDHT22(ambientPowerPin);
    delay(10);
  }

  greenhouseTemp     = (int)((DHTgreenhouse.getTemperature() * 9.0) / 5.0 + 32.0); //Fahrenheit
  greenhouseHumidity = (int)DHTgreenhouse.getHumidity();
  ambientTemp        = (int)((DHTambient.getTemperature() * 9.0) / 5.0 + 32.0);    //Fahrenheit
  ambientHumidity    = (int)DHTambient.getHumidity();

  //now read the soil moisture and convert to 0-100
  soilMoisture = map(analogRead(soilMoisturePin), 0, 1023, 100, 0);

  //now read the light level and convert to 0-100
  insolation = map(analogRead(insolationPin), 0, 1023, 0, 100);
}

//=================================================END SENSOR READ SUBROUTINE===========================================================


void displayLCD() {
//==================================================SUBROUTINE TO DISPLAY INFO ON THE LCD SCREEN======================================================
  lcd.setCursor(0, 0);
  lcd.print(F("GH: "));
  lcd.print(greenhouseTemp);
  lcd.print(F("F--"));
  lcd.print(greenhouseHumidity);
  lcd.print(F("%rH"));

  lcd.setCursor(0, 1);
  lcd.print(F("AMB:"));
  lcd.print(ambientTemp);
  lcd.print(F("F--"));
  lcd.print(ambientHumidity);
  lcd.print(F("%rH"));

  lcd.setCursor(0, 2);
  lcd.print(F("SUNLITE:"));
  lcd.print(insolation);
  lcd.print(F(" %"));

  lcd.setCursor(0, 3);
  lcd.print(F("SOIL:"));
  lcd.print(soilMoisture);
  lcd.print(F(" %"));

  delay(20);

//===================================================END LCD SUBROUTINE================================================================================
}

void printToMonitor() {
#ifdef ENABLE_DEBUG_SERIAL
//=====================================================SUBROUTINE TO PRINT TO SERIAL MONITOR===========================================================
  DateTime now = rtc.now();
  DEBUG_PRINT(now.year(), DEC); DEBUG_PRINT('/');
  DEBUG_PRINT(now.month(), DEC); DEBUG_PRINT('/');
  DEBUG_PRINT(now.day(), DEC); DEBUG_PRINT(' ');
  DEBUG_PRINT(now.hour(), DEC); DEBUG_PRINT(':');
  DEBUG_PRINT(now.minute(), DEC); DEBUG_PRINT(':');
  DEBUG_PRINTLN(now.second(), DEC);

  DEBUG_PRINT(F("GH T:")); DEBUG_PRINT(greenhouseTemp);
  DEBUG_PRINT(F(" H:")); DEBUG_PRINTLN(greenhouseHumidity);
  DEBUG_PRINT(F("AM T:")); DEBUG_PRINT(ambientTemp);
  DEBUG_PRINT(F(" H:")); DEBUG_PRINTLN(ambientHumidity);
  DEBUG_PRINT(F("Soil:")); DEBUG_PRINTLN(soilMoisture);
  DEBUG_PRINT(F("Sun:"));  DEBUG_PRINTLN(insolation);

  DEBUG_PRINT(F("Sides:")); DEBUG_PRINTLN(sidesUp ? F("UP") : F("DN"));
  DEBUG_PRINT(F("Fan:"));   DEBUG_PRINTLN(fanOn ? F("ON") : F("OFF"));
  DEBUG_PRINT(F("Water:")); DEBUG_PRINTLN(irrigationOn ? F("ON") : F("OFF"));

  delay(50);
//=======================================================END SERIAL MONITOR SUBROUTINE===============================================
#endif
}

void writeToSD() {
  DEBUG_PRINTLN(F("LOOP: start"));

//=======================================================SUBROUTINE TO WRITE DATA TO MICROSD CARD=====================================
  //open the file on the microSD card and write data
  //note that only one file can be open at a time, so you have to close this one before opening another
    DEBUG_PRINTLN(F("LOOP: readSensors"));
  myFile = SD.open("grnhs.txt", FILE_WRITE); //grnhs.txt is the file name, FILE_WRITE tells the computer this is a write operation

  //if the file opened okay, write to it:
  if (myFile) {
  DEBUG_PRINTLN(F("LOOP: displayLCD"));
    DateTime now = rtc.now();
  DEBUG_PRINTLN(F("LOOP: logicAndControl"));
    DEBUG_PRINT(F("SD write..."));
  DEBUG_PRINTLN(F("LOOP: node.tick"));
    //with the file open, write the date, time, temperature, and humidity separated by commas
  DEBUG_PRINTLN(F("LOOP: receiveReplies"));
    myFile.print(now.year(), DEC); myFile.print('/');
    myFile.print(now.month(), DEC); myFile.print('/');
  DEBUG_PRINTLN(F("LOOP: printToMonitor"));
    myFile.print(now.day(), DEC); myFile.print(',');
    myFile.print(now.hour(), DEC); myFile.print(':');
    myFile.print(now.minute(), DEC); myFile.print(':');
    myFile.print(now.second(), DEC);

    DEBUG_PRINTLN(F("LOOP: writeToSD"));
    myFile.print(F(","));
    myFile.print(greenhouseTemp);
    myFile.print(F(","));
    myFile.print(greenhouseHumidity);
  DEBUG_PRINTLN(F("LOOP: end"));

    myFile.print(F(","));
    myFile.print(ambientTemp);
    myFile.print(F(","));
    myFile.print(ambientHumidity);

    myFile.print(F(","));
    myFile.print(soilMoisture);
    myFile.print(F(","));
    myFile.print(insolation);
    myFile.print(F(","));
    myFile.println(sidesUp);

    //close the file:
    myFile.close();
    DEBUG_PRINTLN(F("ok"));
  } else {
    //if the file didn't open, print an error:
    DEBUG_PRINTLN(F("SD err"));
  }
  delay(100);
//=======================================================END MICROSD CARD SUBROUTINE=======================================================
}

//=======================================================BEGIN SUBROUTINE TO CHECK DHT22 SENSOR ERROR=================================================
void printDHTError(int chk) {
  switch (chk)
  {
  case DHTLIB_OK:
    DEBUG_PRINT(F("DHT22 SENSOR OK,\t"));
    break;
  case DHTLIB_ERROR_CHECKSUM:
    DEBUG_PRINT(F("DHT22 Checksum error,\t"));
    break;
  case DHTLIB_ERROR_TIMEOUT:
    DEBUG_PRINT(F("DHT22 Time out error,\t"));
    break;
  default:
    DEBUG_PRINT(F("DHT22 Unknown error,\t"));
    break;
  }
}
//=======================================================END SUBROUTINE TO CHECK DHT22 SENSOR ERROR======================

//=======================================================BEGIN SUBROUTINE TO RE-BOOT DHT22 IN CASE OF ERROR============================
void rebootDHT22(int powerPin) { //accepts as input the power pin of the sensor that got an error code
  digitalWrite(powerPin, LOW);  //turns off the power to that sensor
  delay(50);                    //waits 50 milliseconds
  digitalWrite(powerPin, HIGH); //turns the sensor power back on
}
//========================================================END SUBROUTINE TO RE-BOOT DHT22 SENSOR ======================================


//==============================================================BEGIN LOGIC AND CONTROL SUBROUTINE===================================================

void logicAndControl() {

  if (sidesUp == false) { //if the sides are down...
    if (greenhouseTemp >= (targetTemp1 + tempDelta)) { //and if the greenhouse is hotter than the target...
      tone_melody_beethoven();         //play the about-to-open song
      digitalWrite(relayTwoPin, HIGH); //activate the 24 VDC relay scheme to open the roll-up sides
   // delay(45 * 1000);               //wait 45 seconds for the roll-up motor to do its thing
      delay(5000);                     //wait 5 seconds to check the action of the relay
      digitalWrite(relayTwoPin, LOW);  //turn off the relay so it is not consuming power
      sidesUp = true;                  //update the state variable to indicate the sides are open
    }
  }

  if (sidesUp == true) { //if the sides are up...
    if (greenhouseTemp <= (targetTemp1 - tempDelta)) { //and if the greenhouse is cooler than the target...
      tone_melody_brahms();              //play the about-to-close song
      digitalWrite(relayThreePin, HIGH); //activate the 24 VDC relay scheme to close the roll-up sides
    //delay(45 * 1000);                 //wait 45 seconds for the roll-up motor to do its thing
      delay(5000);                       //wait 5 seconds to check the action of the relay
      digitalWrite(relayThreePin, LOW);  //turn off the relay so it is not consuming power
      sidesUp = false;                   //update the state variable to indicate the sides are closed
    }
  }

  if (fanOn == false) { //if the fan is off...
    if (greenhouseTemp >= (targetTemp2 + tempDelta)) { //and if the greenhouse is hotter than the target...
      //open the louver
      digitalWrite(relayZeroPin, HIGH); //activate the 120 VAC relay for the fan
      digitalWrite(relayOnePin,  HIGH); //activate the 120 VAC relay for the louver
      fanOn = true;                     //update the state variable to indicate the fan is running
    }
  }

  if (fanOn == true) { //if the fan is on...
    if (greenhouseTemp <= (targetTemp2 - tempDelta)) { //and if the greenhouse is cooler than the target...
      digitalWrite(relayZeroPin, LOW); //open the relay and turn off the fan
      digitalWrite(relayOnePin,  LOW); //open the relay and turn off the louver
      //close the louver
      fanOn = false;                   //update the state variable to indicate the fan is off
    }
  }

  if (irrigationOn == false) { //if the irrigation is off...
    if (soilMoisture <= (targetMoisture - moistureDelta)) { //and if the soil is drier than the target...
      digitalWrite(relayFourPin, HIGH); //open the relay for the irrigation solenoid
      irrigationOn = true;              //update the state variable to indicate the irrigation is on
    }
  }

  if (irrigationOn == true) { //if the irrigation is on...
    if (soilMoisture >= (targetMoisture + moistureDelta)) { //and if the soil is wetter than the target...
      digitalWrite(relayFourPin, LOW); //close the relay for the irrigation solenoid
      irrigationOn = false;            //update the state variable to indicate the irrigation is off
    }
  }
}

//====================================================================END LOGIC AND CONTROL SUBROUTINE================================================


// Callbacks fired when the Telegram forwarder sends remote control commands over LoRa
void onFanSet(bool state) {
  DEBUG_PRINT(F("[CTRL] FAN -> "));
  DEBUG_PRINTLN(state ? F("ON") : F("OFF"));
  digitalWrite(relayZeroPin, state ? HIGH : LOW); //fan relay
  digitalWrite(relayOnePin,  state ? HIGH : LOW); //louver relay (always moves with the fan)
}

void onSidesSet(bool state) {
  DEBUG_PRINT(F("[CTRL] SIDES -> "));
  DEBUG_PRINTLN(state ? F("UP") : F("DOWN"));
  if (state) {                                //open the sides
    digitalWrite(relayTwoPin,   HIGH);
    delay(5000);
    digitalWrite(relayTwoPin,   LOW);
  } else {                                    //close the sides
    digitalWrite(relayThreePin, HIGH);
    delay(5000);
    digitalWrite(relayThreePin, LOW);
  }
}

void onWaterSet(bool state) {
  DEBUG_PRINT(F("[CTRL] WATER -> "));
  DEBUG_PRINTLN(state ? F("ON") : F("OFF"));
  digitalWrite(relayFourPin, state ? HIGH : LOW); //irrigation solenoid relay
}


//==============================================================BEGIN RECEIVE AND PROCESS INCOMING LORA MESSAGES==============================

void receiveReplies() {
  switch (data_pipe) {
    case radio: {
      RYLR_LoRaAT_Software_Serial_Message* message = rylr.checkMessage();
      if (!message) return;

      greenhouseNode.noteActivity(millis());
      DEBUG_PRINT(F("RX "));
      DEBUG_PRINT(message->from_address);
      DEBUG_PRINT(' ');
      DEBUG_PRINT(message->rssi);
      DEBUG_PRINT(' ');
      DEBUG_PRINTLN(message->data);

      greenhouseNode.handleIncomingMessage(message->data);
      break;
    }
    case uart_rx_tx: {
      char line[64];
      if (!greenhouseNode.readLineFromSerial(line, sizeof(line))) return;

      greenhouseNode.noteActivity(millis());
      DEBUG_PRINTLN(line);
      greenhouseNode.handleIncomingMessage(line);
      break;
    }
  }
}

//=============================================================END RECEIVE AND PROCESS INCOMING LORA MESSAGES============================


//==================================================================MELODY DATA AND PLAYBACK=========================================================

// Melody data lives in PROGMEM to avoid copying to the stack at call time.
// Each melody is an array of (pitch, duration) pairs.
// Positive durations are regular notes; negative durations are dotted notes.

static const int melody_beethoven[] PROGMEM = {
  NOTE_E4, 4,  NOTE_E4, 4,  NOTE_F4, 4,  NOTE_G4, 4, //1
  NOTE_G4, 4,  NOTE_F4, 4,  NOTE_E4, 4,  NOTE_D4, 4,
  NOTE_C4, 4,  NOTE_C4, 4,  NOTE_D4, 4,  NOTE_E4, 4,
  NOTE_E4, -4, NOTE_D4, 8,  NOTE_D4, 2,

  NOTE_E4, 4,  NOTE_E4, 4,  NOTE_F4, 4,  NOTE_G4, 4, //4
  NOTE_G4, 4,  NOTE_F4, 4,  NOTE_E4, 4,  NOTE_D4, 4,
  NOTE_C4, 4,  NOTE_C4, 4,  NOTE_D4, 4,  NOTE_E4, 4,
  NOTE_D4, -4, NOTE_C4, 8,  NOTE_C4, 2,
};
static const int melody_beethoven_len = sizeof(melody_beethoven) / sizeof(melody_beethoven[0]);

static const int melody_brahms[] PROGMEM = {
  // Wiegenlied (Brahms' Lullaby)
  NOTE_G4, 4, NOTE_G4, 4, //1
  NOTE_AS4, -4, NOTE_G4, 8, NOTE_G4, 4,
  NOTE_AS4, 4, REST, 4, NOTE_G4, 8, NOTE_AS4, 8,
  NOTE_DS5, 4, NOTE_D5, -4, NOTE_C5, 8,
  NOTE_C5, 4, NOTE_AS4, 4, NOTE_F4, 8, NOTE_G4, 8,
  NOTE_GS4, 4, NOTE_F4, 4, NOTE_F4, 8, NOTE_G4, 8,
  NOTE_GS4, 4, REST, 4, NOTE_F4, 8, NOTE_GS4, 8,
  NOTE_D5, 8, NOTE_C5, 8, NOTE_AS4, 4, NOTE_D5, 4, NOTE_DS5, 8,
};
static const int melody_brahms_len = sizeof(melody_brahms) / sizeof(melody_brahms[0]);

// Single playback engine that reads note data from PROGMEM
void playMelodyPROGMEM(const int* melodyPgm, int numElements, int tempoVal) {
  int notes = numElements / 2;
  int wholenote = (60000L * 4) / tempoVal;
  int divider, noteDuration;

  for (int i = 0; i < notes * 2; i += 2) {
    int pitch = pgm_read_word_near(&melodyPgm[i]);
    divider   = pgm_read_word_near(&melodyPgm[i + 1]);

    if (divider > 0) {
      noteDuration = wholenote / divider;
    } else {
      noteDuration = wholenote / abs(divider);
      noteDuration += noteDuration / 2; // dotted note = 1.5x
    }

    // play the note for 90% of the duration, leave 10% as a pause
    tone(speakerPin, pitch, (unsigned long)(noteDuration * 9L / 10));
    delay(noteDuration);
    noTone(speakerPin);
  }
}

void tone_melody_beethoven() {
  playMelodyPROGMEM(melody_beethoven, melody_beethoven_len, 114);
}

void tone_melody_brahms() {
  playMelodyPROGMEM(melody_brahms, melody_brahms_len, 85);
}

//==========================================================END MELODY SUBROUTINES=================================================================

//==============================================================BEGIN RADIO SETUP=================================================================

void setupRadio() {
  DEBUG_PRINTLN(F("BOOT: radio setup start"));

  switch (data_pipe) {
    case radio: {
      // First try to connect at the desired 9600 baud rate (more stable for SoftwareSerial)
      radioSerial.begin(9600);
      rylr.setSerial(&radioSerial);
      DEBUG_PRINTLN(F("BOOT: radio check @9600"));

      if (rylr.checkStatus() != 0) {
        DEBUG_PRINTLN(F("LoRa not at 9600. Trying 115200..."));
        // Module might be at factory default 115200 baud
        radioSerial.begin(115200);
        DEBUG_PRINTLN(F("BOOT: radio check @115200"));
        if (rylr.checkStatus() == 0) {
          DEBUG_PRINTLN(F("LoRa found at 115200. Setting to 9600..."));
          // Send the AT command to permanently drop the baud rate to 9600
          radioSerial.print(F("AT+IPR=9600\r\n"));
          delay(500); // Give the module time to save the new rate to flash
          
          // Re-initialize our end to match the new speed
          radioSerial.begin(9600);
          if (rylr.checkStatus() == 0) {
             DEBUG_PRINTLN(F("Successfully switched to 9600."));
          } else {
             DEBUG_PRINTLN(F("Warning: 9600 verification failed."));
          }
        } else {
          DEBUG_PRINTLN(F("Radio not responding. Falling back to 9600."));
          radioSerial.begin(9600);
        }
      } else {
        DEBUG_PRINTLN(F("LoRa already at 9600."));
      }

      rylr.setAddress(LOCAL_ADDRESS);
      rylr.setRFPower(14);
      greenhouseNode.begin(&rylr, REMOTE_ADDRESS, data_pipe);
      DEBUG_PRINTLN(F("Sender ready (radio)"));
      break;
    }
    case uart_rx_tx: {
      radioSerial.begin(9600);
      greenhouseNode.begin(nullptr, REMOTE_ADDRESS, data_pipe, &radioSerial);
      DEBUG_PRINTLN(F("Sender ready (uart)"));
      break;
    }
  }
}

//===============================================================END RADIO SETUP==================================================================

//=====================================================================END OF PROGRAM==================================================================
