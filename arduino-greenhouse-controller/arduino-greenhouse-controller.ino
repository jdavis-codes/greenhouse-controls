
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


//========================================================================BEGIN DECLARATIONS=========================================================
#include <DS3231.h> //includes the library for using the DS3231 Time module
DS3231  rtc(SDA, SCL); //sets up the time module

#include <LiquidCrystal_I2C.h> //includes the library for using the 2x16 display with the built in I2C adapter
LiquidCrystal_I2C lcd(0x27,20,4); // I2C address 0x27, 20 column and 4 row

#include "DHTStable.h" //Include dhtStable library
DHTStable DHTgreenhouse; //creates an instance of the DHTStable class for the inlet
DHTStable DHTambient;//creates an instance of the DHTStable class for the outlet

#define relayZeroPin  A0 //declare all pins; uses analog pins for digital output; zero and one are the 120vac relays
#define relayOnePin  A1 
#define relayTwoPin  A2// two and three are the reversing motor relays for the roll up sides
#define relayThreePin  A3
//A4 pin is shared (by necessity) between the rtd clock SDA and the digital display I2C SDA
//A5 pin is shared (by necessity) between the rtd clock SCL and the digital display I2C SCL
#define relayFourPin  4 //four is the relay for the irrigation solenoid--it uses DIGITAL PIN 4
#define insolationPin  A6 //analog read for the light meter
#define soilMoisturePin  A7 //analog read for the soil moisture probe
#define grnhousePin  2 //Dht 22 pin for greehnouse temp and humidity
#define grnhousePowerPin  3 // dht 22 pin for power to sensor-- allows automatic rebooting of sensor
#define speakerPin  7 // output to speaker to play before motion
#define ambientPin  8 //dht 22 pin for ambient temperature and humidity
#define ambientPowerPin  9 // dht 22 pin for power to sensor-- allows automatic rebooting of sensor
//digital pin 10 is the "CS" pin on the microSD card module this is chosen below where the SD library is included
//digital pin 11 is (by necessity) the microSD card module MOSI
//digital pin 12 is (by necessity) the micro SD card module MISO
// digital pin 13 is (by necessity) the micro SD card module SCK

float grnhouseTemp; //these 4 variables will store the temperatures (in Celcius!) and humidities (%rH) from the sensors
float grnhouseHum;
float ambientTemp;
float ambientHum;

//float grnhouseTargetTemp1 = 85.0 ; //Fahrenheit! these are the variables for two target temperatures in the greehnouse and a delta, or hysteresis
float grnhouseTargetTemp1= 80.0; //for testing, comment out when done
//float grnhouseTargetTemp2 = 100.0; //Fahrenheit!
float grnhouseTargetTemp2 = 85.0; //for testing, comment out when done

float grnhouseTempDelta = 4.0; //Fahrenheit!

int soilMoisture; //this stores the soil moisture as a number 0-100
int soilTargetMoisture = 50; //this is the moisture expressed on a 0-100 scale
int soilMoistureDelta = 10; // this is the hysteresis for the soil moisture target

bool motorUp = false;  //this variable stores the position of the motor (1 or true means motor is UP, 0 or false means motor is DOWN)
bool fanOn = false; // this variable stores the condition of the fan (1 means fan is ON, 0 means fan is OFF)
bool waterOn = false; //this variable stores the condition of the irrigation solenoid (1 means ON, 0 means OFF)

int insolation; //this variable stores the light level on a 0-100 scale

#include <SPI.h> //library for Serial Peripheral Interface communication 
#include <SD.h> //library for microSD card
const int chipSelect = 10;// sets pin 10 for CS on the microSD module pinout
File myFile; //creates a file for the micro SD card

//variables for timer on for the microSD card write function
unsigned long writeInterval = 5000;
unsigned long runTime;
unsigned long startTime1 = 0;

//variables for the timer on the function to read the sensors
unsigned long readInterval = 3000;
unsigned long readTime ;
unsigned long startTime2 = 0;

//==========================================================END DECLARATIONS======================================================================


//==============================================================BEGIN SET UP======================================================================
void setup() {


  Serial.begin(9600);

  // wait for Serial Monitor to connect. Needed for native USB port boards only:
  while (!Serial);


  Serial.print(F("Initializing SD card..."));

  if (!SD.begin(chipSelect)) {
    Serial.println(F("initialization failed. Things to check:"));
    Serial.println(F("1. is a card inserted?"));
    Serial.println(F("2. is your wiring correct?"));
    Serial.println(F("3. did you change the chipSelect pin to match your shield or module?"));
    Serial.println(F("Note: press reset button on the board and reopen this Serial Monitor after fixing your issue!"));
    while (true);
  }

  Serial.println(F("initialization done."));
  delay(20);


 rtc.begin(); // Initialize the DS3231 realtimeclock object

  lcd.init(); // Initialize the lcd object, clear it, turn on the backlight, set the cursor (?)
  lcd.clear();
  lcd.backlight();
  lcd.setCursor(0,0);


//set up pins as input or output
pinMode (relayZeroPin, OUTPUT); 
pinMode (relayOnePin, OUTPUT); 
pinMode (relayTwoPin, OUTPUT);
pinMode (relayThreePin, OUTPUT); 
pinMode (relayFourPin, OUTPUT); 
pinMode (insolationPin, INPUT); 
pinMode (soilMoisturePin, INPUT);    
pinMode (grnhousePin, INPUT); 
pinMode (grnhousePowerPin, OUTPUT);
pinMode (speakerPin, OUTPUT);
pinMode (ambientPin, INPUT); 
pinMode (ambientPowerPin, OUTPUT); 

//write the pins high for turning on the temp/hum sensors
digitalWrite (grnhousePowerPin, HIGH);
digitalWrite (ambientPowerPin, HIGH);
digitalWrite (relayZeroPin, LOW); //turn off relay that controls fan
digitalWrite (relayOnePin, LOW); //turn off relay for 120vac output (louver motor)
digitalWrite (relayTwoPin, LOW); //turn off relays that control roll-up motor
digitalWrite (relayThreePin, LOW); //turn off relays that control roll-up motor
digitalWrite (relayFourPin, LOW); //turn off relay that controls irrigation solenoid

}

//==============================================================END SET UP=======================================================================

//===============================================================BEGIN MAIN LOOP==================================================================

void loop() {
  readInterval = (1000 * 3);  //the read interval in milliseconds for the readSensors function which gets the temperatures and humidities
 readTime = (millis() - startTime2);
  if (readTime > readInterval){
     readSensors(); //gets readings from sensors
     startTime2 = millis(); //resets the timer
  }
 displayLCD(); //displays things on LCD (4x20)


 logicAndControl();//controls the relay based on the conditions
 
 printToMonitor(); //prints data to Serial Monitor if a computer is connected with an active serial monitor


 writeInterval = (1000 *5);  //writes every 5 seconds
 //writeInterval = 900000;  //writes every 15 minutes
 runTime = (millis() - startTime1);
  if (runTime > writeInterval){
   writeToSD();  //writes data to microSD card
   startTime1 = millis();
  }

 delay(100);
}

//==========================================================END MAIN LOOP=======================================================

//===========================================================BEGIN SUBROUTINES==============================================================

//===============================================SUBROUTINE TO READ TEMPERATURE AND HUMIDITY WITH DHT-22=============================================

void readSensors(void) {
int chkGreenhouse = DHTgreenhouse.read22(grnhousePin); //calls a function to check the sensor for read errors--ok=0;errors are 1,2,etc
int chkAmbient = DHTambient.read22(ambientPin);//calls a function to check the sensor for read errors.
if (chkGreenhouse != 0){//if the read error function returns other-than-zero, there's a problem with the sensor and it prints the error and gets rebooted
   printDHTError(chkGreenhouse);
   rebootDHT22 (grnhousePowerPin);
   delay (10);
  }
if (chkAmbient != 0){
   printDHTError(chkAmbient);
   rebootDHT22 (ambientPowerPin);
   delay (10);
  }


grnhouseTemp = ((DHTgreenhouse.getTemperature() * 9.0) / 5.0 + 32.0);//Fahrenheit

grnhouseHum = DHTgreenhouse.getHumidity();
ambientTemp = ((DHTambient.getTemperature() * 9.0) / 5.0 + 32.0);//Fahrenheit!
ambientHum = DHTambient.getHumidity(); 

//now read the soil moisture and convert to 0-100
soilMoisture = map(analogRead(soilMoisturePin), 0, 1023, 100, 0);

//now read the light level and convert to 0-100
insolation = map(analogRead(insolationPin), 0, 1023, 0, 100);

}
  //=================================================END TEMPERATURE AND HUMIDITY SUBROUTINE===========================================================


void displayLCD(){ 
//==================================================SUBROUTINE TO DISPLAY INFO ON THE LCD SCREEN======================================================
// Now display the greehnouse and ampbient temperature and humidity on the lcd
 lcd.setCursor(0,0);
 lcd.print(F("GH: "));
 lcd.print(grnhouseTemp);
 lcd.print(F("F--"));
 lcd.print(grnhouseHum);
 lcd.print (F("%rH"));

 lcd.setCursor(0,1);
 lcd.print(F("AMB:"));
 lcd.print(ambientTemp);
 lcd.print(F("F--"));
 lcd.print(ambientHum);
 lcd.print(F("%rH"));
 

 lcd.setCursor(0,2);
 lcd.print(F("SUNLITE:"));
 lcd.print(insolation);
 lcd.print(F(" %"));

 lcd.setCursor(0,3);
 lcd.print(F("SOIL:"));
 lcd.print(soilMoisture);
 lcd.print(F(" %"));

 delay(20);


//===================================================END LCD SUBROUTINE================================================================================
}
void printToMonitor(void){
//=====================================================SUBROUTINE TO PRINT TO SERIAL MONITOR===========================================================
   // Send date to serial monitor
  Serial.print(rtc.getDateStr());
  Serial.print(F(" -- "));
  // Send time to serial monitor
  Serial.println(rtc.getTimeStr());

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

   if (motorUp == true) {
     Serial.println(F("motor is UP"));
   }
    else {
      Serial.println(F("motor is DOWN"));
    }

   if (fanOn == true) {
     Serial.println(F("fan is ON"));
   }
    else {
      Serial.println(F("fan is OFF"));
    }
   if (waterOn == true) {
     Serial.println(F("water is ON"));
   }
    else {
      Serial.println(F("water is OFF"));
    }

  
  delay(50);
//=======================================================END SERIAL MONITOR SUBROUTINE===============================================
}

void writeToSD(void){
//=======================================================SUBROUTINE TO WRITE DATA TO MICROSD CARD=====================================
   //open file on micro-sd card and write data to file
  // open the file. note that only one file can be open at a time,
  // so you have to close this one before opening another.
  myFile = SD.open("grnhs.txt", FILE_WRITE);  //greenhouse.txt is the file name, FILE_WRITE tells the computer that this is a write operation

  // if the file opened okay, write to it:
  if (myFile) {
    Serial.print(F("Writing to grnhs.txt..."));
     //with the file open, write the date, time, temp, and humidity separated by commas
    myFile.print(rtc.getDateStr());
    myFile.print(F(","));
    myFile.print(rtc.getTimeStr());

    myFile.print(F(","));
    myFile.print(grnhouseTemp);
    myFile.print(F(","));
    myFile.print(grnhouseHum);
   
    myFile.print(F(","));
    myFile.print(ambientTemp);
    myFile.print(F(","));
    myFile.print(ambientHum);

    myFile.print(F(","));
    myFile.println(motorUp);

    // close the file:
    myFile.close();
    Serial.println(F("done."));
  } else {
    // if the file didn't open, print an error:
    Serial.println(F("error opening grnhs.txt"));
  }
 delay(100); 
//=======================================================END MICRO SD CARD SUBROUTINE=======================================================
}
//=======================================================BEGIN SUBROUTINE TO CHECK DHT22 SENSOR ERROR=================================================
void printDHTError(int chk){
    switch (chk)
    {
    case DHTLIB_OK:
        //counter.ok++;
        Serial.print("DHT22 SENSOR OK,\t");
        break;
    case DHTLIB_ERROR_CHECKSUM:
        //counter.crc_error++;
        Serial.print("DHT22 Checksum error,\t");
        break;
    case DHTLIB_ERROR_TIMEOUT:
        //counter.time_out++;
        Serial.print("DHT22 Time out error,\t");
        break;
    default:
        ///counter.unknown++;
        Serial.print("DHT22 Unknown error,\t");
        break;
    }
    
}
//=======================================================END SUBROUTINE TO CHECK DHT22 SENSOR ERROR======================
//=======================================================BEGIN SUBROUTINE TO RE-BOOT DHT22 IN CASE OF ERROR============================
void rebootDHT22( int powerPin){ //accepts as input the powerpin of the sensor that got an error code
digitalWrite (powerPin, LOW); //turns off the power to that sensor
delay (50); //waits 50 milliseconds
digitalWrite (powerPin, HIGH); //turns the sensor's power back on
}
//========================================================END SUBROUTINE TO RE-BOOT DHT22 SENSOR ======================================

//==============================================================BEGIN LOGIC AND CONTROL SUBROUTINE===================================================

void logicAndControl(){

if (motorUp == false){ //if the sides are down...
  if (grnhouseTemp >= (grnhouseTargetTemp1 + grnhouseTempDelta)){ //and if the greenhouse is hotter than the target...
    digitalWrite (relayTwoPin, HIGH); //this activates the 24 vdc relay scheme to open the roll-up sides
 // delay (45 * 1000); //wait 45 seconds for roll-up motor to do its thing
    delay (5000); //wait 5 seconds to check action of relay
    digitalWrite (relayTwoPin, LOW);  //this turns off the relay so it isn't consuming power
    motorUp = true; //changes the state of the motorUp variable to true which indicates the sides are open
  }}

if (motorUp == true){ //if the sides are up...
  if (grnhouseTemp <= (grnhouseTargetTemp1 - grnhouseTempDelta)){ //this checks if the greenhouse is colder than the target, and...
    //play beethoven
    digitalWrite (relayThreePin, HIGH); //this activates the 24 vdc relay scheme to close the roll-up sides
    //delay (45 * 1000); //wait 45 seconds for roll-up motor to do its thing
    delay (5000); //wait 5 seconds to check action of relay
    digitalWrite (relayThreePin, LOW);  //this turns off the relay so it isn't consuming power
    motorUp = false; //changes the state of the motorUp variable to false which indicates the sides are closed
  }}

if (fanOn == false){ //if the fan is off...
  if (grnhouseTemp >= (grnhouseTargetTemp2 + grnhouseTempDelta)){ //and if the greenhouse is hotter than the target...
    //open the louver
    digitalWrite (relayZeroPin, HIGH); //this activates the 120 vac relay for the fan
    fanOn = true; //changes the state of the fanOn variable to true which indicates the fan is running
  }}

if (fanOn == true){ //if the sides
  if (grnhouseTemp <= (grnhouseTargetTemp2 - grnhouseTempDelta)){ //this checks if the greenhouse is colder than the target, and...
    digitalWrite (relayZeroPin, LOW); //this opens the relay and turns off the fan
    //close the louver
    fanOn = false; //changes the state of the fanOn variable to false which indicates the fan is not running
  }}

if (waterOn == false){ //if the irrigation is off...
  if (soilMoisture <= (soilTargetMoisture - soilMoistureDelta)){ //and if the soil is dryer than the target...
    digitalWrite (relayFourPin, HIGH); //this opens the relay for the irrigation solenoid
    waterOn = true; //changes the state of the waterOn variable to indicate the irrigation is on
  }}

if (waterOn == true){ //if the irrigation is on...
  if (soilMoisture >= (soilTargetMoisture + soilMoistureDelta)){ //and if the soil is wetter than the target...
    digitalWrite (relayFourPin, LOW); //this closes the relay for the irrigation solenoid
    waterOn = false; //changes the state of the waterOn variable to indicate the irrigation is off
  }}

}

//====================================================================END LOGIC AND CONTROL SUBROUTINE================================================

//=====================================================================END OF PROGRAM==================================================================

