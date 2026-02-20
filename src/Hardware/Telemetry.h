#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <Arduino.h>
#include "Nextion.h"
#include <Wire.h>
#include <Adafruit_INA219.h>
#include "Hardware/1Definitions.h"
#include "Hardware/Battery.h"
#include "Hardware/Nextion.h"
#include "Hardware/OperatePump.h"
#include "Hardware/Utils.h"

/*********************************************************************************************************************************/
void  CheckTXVolts()
{

 #define VMIN  3.8
 #define VMAX  4.2 

 char GoBack[] = "page MAINPAGE";
 char Prompt[] = "Battery out of range!\r\n   Replace battery!";

uint8_t cells = 0;
float vmin = 0;
float vmax = 0;

/*************************************************Check how many cells the battery has *********************************************/


if ((busvoltage >= 7.65) && (busvoltage <= 8.4)){
    cells = 2;
}

if ((busvoltage >= 11.46) && (busvoltage <= 12.6)){
    cells = 3;
}

if (cells == 0){
MsgBox(GoBack,Prompt);

}

int BatteryPercentLeft = 0;
char BatteryState[] ="BatteryState";
char Battery_level[] ="Battery_level";

vmin = VMIN * (float) cells;
vmax =  VMAX * (float) cells;


BatteryPercentLeft = map(busvoltage, vmin, vmax, 0,100) ;
SendValue(BatteryState, BatteryPercentLeft);
SendValue(Battery_level, BatteryPercentLeft);
/*
Serial.print(F("busvoltage="));
Serial.print(busvoltage);

Serial.print(F(" cells="));
Serial.print(cells);

Serial.print(F(" BatteryState="));
Serial.println(BatteryPercentLeft);
*/

}

#endif
