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
int tempo;           //stores the tempo of the songs
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
void readSensors();
void displayLCD();
void printToMonitor();
void writeToSD();
void printDHTError(int chk);
void rebootDHT22(int powerPin);
void logicAndControl();
void tone_melody_beethoven();
void tone_melody_brahms();
void setupSD();
void setupLCD();
void initializePins();
void receiveReplies();

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


//==============================================================BEGIN SET UP======================================================================

void setup() {

  // wait for Serial Monitor to connect. Needed for native USB port boards only:
  Serial.begin(115200);
  while (!Serial);

  greenhouseNode.setupStatusLed();

  setupSD(); //setup SD card logger
  delay(20);

  rtc.begin(); //initialize the DS3231 real-time clock object

  setupLCD();

  initializePins(); //set up pins as input or output and power on the temp/humidity sensors

  greenhouseNode.configure(sensors, events, settings); //no sample callback; sensors are read manually on a timer

  switch (data_pipe) {
    case radio: {
      radioSerial.begin(9600);
      rylr.setSerial(&radioSerial);

      int result = rylr.checkStatus();
      Serial.print(F("LoRa status: "));
      Serial.println(result);

      rylr.setAddress(LOCAL_ADDRESS);
      rylr.setRFPower(14);
      greenhouseNode.begin(&rylr, REMOTE_ADDRESS, data_pipe);
      Serial.println(F("Greenhouse sender ready - transmitting via the rylr RADIO data pipe."));
      break;
    }
    case uart_rx_tx: {
      radioSerial.begin(9600);
      greenhouseNode.begin(nullptr, REMOTE_ADDRESS, data_pipe, &radioSerial);
      Serial.println(F("Greenhouse sender ready - transmitting via the UART RX/TX data pipe."));
      break;
    }
  }
}

//==============================================================END SET UP=======================================================================

//===============================================================BEGIN MAIN LOOP==================================================================

void loop() {
  readInterval = (1000 * 3);         //the read interval in milliseconds for readSensors()
  readTime     = (millis() - startTime2);
  if (readTime > readInterval) {
    readSensors();                   //gets readings from all sensors
    startTime2 = millis();           //resets the timer
  }

  displayLCD();                      //displays readings on the 20x4 LCD
  logicAndControl();                 //controls the relays based on sensor conditions
  greenhouseNode.tick(millis());     //handles telemetry transmission and incoming LoRa commands
  receiveReplies();                  //processes any incoming LoRa reply messages

  printToMonitor();                  //prints data to the Serial Monitor if a computer is connected

  writeInterval = (1000 * 5);        //writes every 5 seconds
  //writeInterval = 900000;          //writes every 15 minutes
  runTime = (millis() - startTime1);
  if (runTime > writeInterval) {
    writeToSD();                     //writes data to the microSD card
    startTime1 = millis();
  }

  delay(100);
}

//==========================================================END MAIN LOOP=======================================================

//===========================================================BEGIN SUBROUTINES==============================================================

//===========================================================SETUP SUBROUTINES================================================================
void setupSD() {
  Serial.print(F("Initializing SD card..."));

  if (!SD.begin(chipSelect)) {
    Serial.println(F("initialization failed. Things to check:"));
    Serial.println(F("1. is a card inserted?"));
    Serial.println(F("2. is your wiring correct?"));
    Serial.println(F("3. did you change the chipSelect pin to match your shield or module?"));
    Serial.println(F("Note: press reset button on the board and reopen this Serial Monitor after fixing your issue!"));
    while (true);
  }
  Serial.println(F("SD initialization done."));
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
//=====================================================SUBROUTINE TO PRINT TO SERIAL MONITOR===========================================================
  //send date and time to the serial monitor
  DateTime now = rtc.now();
  Serial.print(now.timestamp(DateTime::TIMESTAMP_DATE));
  Serial.print(F(" -- "));
  Serial.println(now.timestamp(DateTime::TIMESTAMP_TIME));

  //print temperature and humidity to the serial monitor
  Serial.print(F("Greenhouse Temperature F:  "));
  Serial.println(greenhouseTemp);
  Serial.print(F("Greenhouse Humidity:  "));
  Serial.println(greenhouseHumidity);

  Serial.print(F("Ambient Temperature F:  "));
  Serial.println(ambientTemp);
  Serial.print(F("Ambient Humidity:  "));
  Serial.println(ambientHumidity);

  Serial.print(F("Soil Moisture:  "));
  Serial.println(soilMoisture);

  Serial.print(F("Sunlight Insolation:  "));
  Serial.println(insolation);

  if (sidesUp == true) {
    Serial.println(F("motor is UP"));
  } else {
    Serial.println(F("motor is DOWN"));
  }

  if (fanOn == true) {
    Serial.println(F("fan is ON"));
  } else {
    Serial.println(F("fan is OFF"));
  }

  if (irrigationOn == true) {
    Serial.println(F("water is ON"));
  } else {
    Serial.println(F("water is OFF"));
  }

  delay(50);
//=======================================================END SERIAL MONITOR SUBROUTINE===============================================
}

void writeToSD() {
//=======================================================SUBROUTINE TO WRITE DATA TO MICROSD CARD=====================================
  //open the file on the microSD card and write data
  //note that only one file can be open at a time, so you have to close this one before opening another
  myFile = SD.open("grnhs.txt", FILE_WRITE); //grnhs.txt is the file name, FILE_WRITE tells the computer this is a write operation

  //if the file opened okay, write to it:
  if (myFile) {
    DateTime now = rtc.now();
    Serial.print(F("Writing to grnhs.txt..."));
    //with the file open, write the date, time, temperature, and humidity separated by commas
    myFile.print(now.timestamp(DateTime::TIMESTAMP_DATE));
    myFile.print(F(","));
    myFile.print(now.timestamp(DateTime::TIMESTAMP_TIME));

    myFile.print(F(","));
    myFile.print(greenhouseTemp);
    myFile.print(F(","));
    myFile.print(greenhouseHumidity);

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
    Serial.println(F("done."));
  } else {
    //if the file didn't open, print an error:
    Serial.println(F("error opening grnhs.txt"));
  }
  delay(100);
//=======================================================END MICROSD CARD SUBROUTINE=======================================================
}

//=======================================================BEGIN SUBROUTINE TO CHECK DHT22 SENSOR ERROR=================================================
void printDHTError(int chk) {
  switch (chk)
  {
  case DHTLIB_OK:
    Serial.print(F("DHT22 SENSOR OK,\t"));
    break;
  case DHTLIB_ERROR_CHECKSUM:
    Serial.print(F("DHT22 Checksum error,\t"));
    break;
  case DHTLIB_ERROR_TIMEOUT:
    Serial.print(F("DHT22 Time out error,\t"));
    break;
  default:
    Serial.print(F("DHT22 Unknown error,\t"));
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
  Serial.print(F("[CTRL] FAN -> "));
  Serial.println(state ? F("ON") : F("OFF"));
  digitalWrite(relayZeroPin, state ? HIGH : LOW); //fan relay
  digitalWrite(relayOnePin,  state ? HIGH : LOW); //louver relay (always moves with the fan)
}

void onSidesSet(bool state) {
  Serial.print(F("[CTRL] SIDES -> "));
  Serial.println(state ? F("UP") : F("DOWN"));
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
  Serial.print(F("[CTRL] WATER -> "));
  Serial.println(state ? F("ON") : F("OFF"));
  digitalWrite(relayFourPin, state ? HIGH : LOW); //irrigation solenoid relay
}


//==============================================================BEGIN RECEIVE AND PROCESS INCOMING LORA MESSAGES==============================

void receiveReplies() {
  switch (data_pipe) {
    case radio: {
      RYLR_LoRaAT_Software_Serial_Message* message = rylr.checkMessage();
      if (!message) return;

      greenhouseNode.noteActivity(millis());
      Serial.print(F("RX from "));
      Serial.print(message->from_address);
      Serial.print(F(" (RSSI "));
      Serial.print(message->rssi);
      Serial.print(F(", SNR "));
      Serial.print(message->snr);
      Serial.print(F(") ["));
      Serial.print(message->data_len);
      Serial.print(F(" bytes]: "));
      Serial.println(message->data);

      greenhouseNode.handleIncomingMessage(message->data);
      break;
    }
    case uart_rx_tx: {
      char line[128];
      if (!greenhouseNode.readLineFromSerial(line, sizeof(line))) return;

      greenhouseNode.noteActivity(millis());
      Serial.print(F("RX UART: "));
      Serial.println(line);
      greenhouseNode.handleIncomingMessage(line);
      break;
    }
  }
}

//=============================================================END RECEIVE AND PROCESS INCOMING LORA MESSAGES============================


//==================================================================SUBROUTINE TO PLAY BEETHOVEN========================================================
 void tone_melody_beethoven() {
  // notes of the melody followed by the duration.
  // a 4 means a quarter note, 8 an eighth, 16 a sixteenth, so on
  // !!negative numbers are used to represent dotted notes,
  // so -4 means a dotted quarter note, that is, a quarter plus an eighth!!
  int melody[] = {

    NOTE_E4, 4,  NOTE_E4, 4,  NOTE_F4, 4,  NOTE_G4, 4, //1
    NOTE_G4, 4,  NOTE_F4, 4,  NOTE_E4, 4,  NOTE_D4, 4,
    NOTE_C4, 4,  NOTE_C4, 4,  NOTE_D4, 4,  NOTE_E4, 4,
    NOTE_E4, -4, NOTE_D4, 8,  NOTE_D4, 2,

    NOTE_E4, 4,  NOTE_E4, 4,  NOTE_F4, 4,  NOTE_G4, 4, //4
    NOTE_G4, 4,  NOTE_F4, 4,  NOTE_E4, 4,  NOTE_D4, 4,
    NOTE_C4, 4,  NOTE_C4, 4,  NOTE_D4, 4,  NOTE_E4, 4,
    NOTE_D4, -4, NOTE_C4, 8,  NOTE_C4, 2,
  };
  tempo = 114;

  // sizeof gives the number of bytes, each int value is composed of two bytes (16 bits)
  // there are two values per note (pitch and duration), so for each note there are four bytes
  int notes = sizeof(melody) / sizeof(melody[0]) / 2;

  // this calculates the duration of a whole note in ms
  int wholenote = (60000 * 4) / tempo;

  int divider = 0, noteDuration = 0;

  // iterate over the notes of the melody.
  // Remember, the array is twice the number of notes (notes + durations)
  for (int thisNote = 0; thisNote < notes * 2; thisNote = thisNote + 2) {

    // calculates the duration of each note
    divider = melody[thisNote + 1];
    if (divider > 0) {
      // regular note, just proceed
      noteDuration = (wholenote) / divider;
    } else if (divider < 0) {
      // dotted notes are represented with negative durations!!
      noteDuration = (wholenote) / abs(divider);
      noteDuration *= 1.5; // increases the duration in half for dotted notes
    }

    // we only play the note for 90% of the duration, leaving 10% as a pause
    tone(speakerPin, melody[thisNote], noteDuration * 0.9);

    // Wait for the specified duration before playing the next note.
    delay(noteDuration);

    // stop the waveform generation before the next note.
    noTone(speakerPin);
  }
 }


//==============================END SUBROUTINE TO PLAY BEETHOVEN====================================================

//=======================================BEGIN SUBROUTINE TO PLAY BRAHMS =================================================

 void tone_melody_brahms() {
  // notes of the melody followed by the duration.
  // a 4 means a quarter note, 8 an eighth, 16 a sixteenth, so on
  // !!negative numbers are used to represent dotted notes,
  // so -4 means a dotted quarter note, that is, a quarter plus an eighth!!
  int melody[] = {

    // Wiegenlied (Brahms' Lullaby)
    // Score available at https://www.flutetunes.com/tunes.php?id=54

    NOTE_G4, 4, NOTE_G4, 4, //1
    NOTE_AS4, -4, NOTE_G4, 8, NOTE_G4, 4,
    NOTE_AS4, 4, REST, 4, NOTE_G4, 8, NOTE_AS4, 8,
    NOTE_DS5, 4, NOTE_D5, -4, NOTE_C5, 8,
    NOTE_C5, 4, NOTE_AS4, 4, NOTE_F4, 8, NOTE_G4, 8,
    NOTE_GS4, 4, NOTE_F4, 4, NOTE_F4, 8, NOTE_G4, 8,
    NOTE_GS4, 4, REST, 4, NOTE_F4, 8, NOTE_GS4, 8,
    NOTE_D5, 8, NOTE_C5, 8, NOTE_AS4, 4, NOTE_D5, 4, NOTE_DS5, 8,

  };
  tempo = 85;

  // sizeof gives the number of bytes, each int value is composed of two bytes (16 bits)
  // there are two values per note (pitch and duration), so for each note there are four bytes
  int notes = sizeof(melody) / sizeof(melody[0]) / 2;

  // this calculates the duration of a whole note in ms
  int wholenote = (60000 * 4) / tempo;

  int divider = 0, noteDuration = 0;

  // iterate over the notes of the melody.
  // Remember, the array is twice the number of notes (notes + durations)
  for (int thisNote = 0; thisNote < notes * 2; thisNote = thisNote + 2) {

    // calculates the duration of each note
    divider = melody[thisNote + 1];
    if (divider > 0) {
      // regular note, just proceed
      noteDuration = (wholenote) / divider;
    } else if (divider < 0) {
      // dotted notes are represented with negative durations!!
      noteDuration = (wholenote) / abs(divider);
      noteDuration *= 1.5; // increases the duration in half for dotted notes
    }

    // we only play the note for 90% of the duration, leaving 10% as a pause
    tone(speakerPin, melody[thisNote], noteDuration * 0.9);

    // Wait for the specified duration before playing the next note.
    delay(noteDuration);

    // stop the waveform generation before the next note.
    noTone(speakerPin);
  }
 }
//==========================================================END OF SUBROUTINE TO PLAY BRAHMS=======================================================


//=====================================================================END OF PROGRAM==================================================================
