#ifndef BATTERY_H       // If it is already inculded ignore
#define BATTERY_H
#include "Hardware/Telemetry.h"
// *********************************************************************************************************************************/
//                  This file contains the functions that are used for Battery
// *********************************************************************************************************************************/

void CheckBattery(){

  float shuntvoltage = 0;
  float current_mA = 0;
  float loadvoltage = 0;
  float power_mW = 0;

// ********************************************************************************************
// This looks at the current time and if less than 1 second has elaped it returns. 
// If more than 1 second has elaped it does the sets the local timer to current time and 
// runs the rest of the code
// ******************************************************************************************** 

  static uint32_t LocalTimer = 0;
  if ((millis() - LocalTimer) < 1000) return;
  LocalTimer = millis();

  shuntvoltage = ina219.getShuntVoltage_mV();
  busvoltage = ina219.getBusVoltage_V();
  current_mA = ina219.getCurrent_mA();
  power_mW = ina219.getPower_mW();
  loadvoltage = busvoltage + (shuntvoltage / 1000);
  CheckTXVolts();
  
  // Serial.print("Bus Voltage:   "); Serial.print(busvoltage); Serial.println(" V");
  // Serial.print("Shunt Voltage: "); Serial.print(shuntvoltage); Serial.println(" mV");
  // Serial.print("Load Voltage:  "); Serial.print(loadvoltage); Serial.println(" V");
  // Serial.print("Current:       "); Serial.print(current_mA); Serial.println(" mA");
  // Serial.print("Power:         "); Serial.print(power_mW); Serial.println(" mW");
  // Serial.println("");


  

}

#endif