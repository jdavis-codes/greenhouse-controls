#include <Arduino.h>
#include "WS_RTC.h"
#include "I2C_Driver.h"

void setup() {
  Serial.begin(115200);
  delay(2000);
  printf("\n--- RTC Setup ---\r\n");

  // Initialize I2C first
  I2C_Init();

  // Create a time structure using real data
  datetime_t new_datetime;
  new_datetime.year = 2026;       // The RTC year offset logic calculates it relative to 1970
  new_datetime.month = 7;
  new_datetime.day = 12;
  new_datetime.dotw = 0;          // Sunday (0-6)
  new_datetime.hour = 12;
  new_datetime.minute = 0;
  new_datetime.second = 0;

  printf("Setting RTC Time...\r\n");
  PCF85063_Set_All(new_datetime);

  // Initialize PCF85063 and start tasks AFTER setting the time to avoid I2C collision with freeRTOS task
  RTC_Init();

  printf("RTC Time Set! Beginning periodic output.\r\n");
}

void loop() {
  // datetime struct is automatically updated by the PCF85063Task from WS_PCF85063.cpp
  char datetime_str[50];
  datetime_to_str(datetime_str, datetime);
  
  printf("Current RTC Time: %s\r\n", datetime_str);
  
  delay(1000);
}