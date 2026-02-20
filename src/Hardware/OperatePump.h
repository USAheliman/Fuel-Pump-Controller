
#ifndef OPERATEPUMP_H
#define OPERATEPUMP_H

// *********************************************************************************************************************************/
//              This file contains the functions that are called when the buttons on the pump page are pressed.
//              The functions are called from the FunctionPointersArray[] array in Utils.h
// *********************************************************************************************************************************/

/*********************************************************************************************************************************/
// Pump Functions
/*********************************************************************************************************************************/
void DoNothing()
{
    // This function does nothing and is never called but must be included in the array.
}
// *********************************************************************************************************************************/
// Function to set the speed of the pump

 void SetPumpSpeed(int speed)
{
    speed = constrain(speed, -255, 255);

    if (speed > 0)
    {
        digitalWrite(PUMP_IN1, HIGH);
        digitalWrite(PUMP_IN2, LOW);
    }
    else if (speed < 0)
    {
        digitalWrite(PUMP_IN1, LOW);
        digitalWrite(PUMP_IN2, HIGH);
        speed = -speed; // magnitude for PWM
    }
    else
    {
        digitalWrite(PUMP_IN1, LOW);
        digitalWrite(PUMP_IN2, LOW);
    }

    analogWrite(PUMP_ENA, speed); // 0..255
}

// *********************************************************************************************************************************/
// If reset is pressed on the main screen reset the volume.

void ResetVolume()
{
    Serial.println("'Reset' was pressed");
    VolumeSoFar = 0;
    Serial.println(VolumeSoFar = 0);

}

// *********************************************************************************************************************************/
void FillModelTank()
{
    PumpEnabled = true;
    int startSpeed = 200;           // pick your default
    SetPumpSpeed(startSpeed);

    SendCommand(pFillPage);         // This is the command to go to the Fill Page.
    CurrentPage = FILLPAGE;

    SendValue((char*)"PumpSpeedFill", startSpeed);  // sync slider
}

// **********************************************************************************************************************************/
void DrainModelTank()
{
    PumpEnabled = true;
    int startSpeed = 200;               // magnitude
    SetPumpSpeed(-startSpeed);

    SendCommand(pDrainPage);            // This is the command to go to the Drain Page.
    CurrentPage = DRAINPAGE;

    SendValue((char*)"PumpSpeedDrain", startSpeed); // sync slider
  
}
// *********************************************************************************************************************************/
void StopThePump()
{

    PumpEnabled = false;
    SetPumpSpeed(0);

    // Optional but very helpful: force the UI sliders to 0 so it doesn't “want” to restart
    SendValue((char*)"PumpSpeedFill", 0);
    SendValue((char*)"PumpSpeedDrain", 0);

    if (CurrentPage !=  MAINPAGE){   // If we are not on the Main Page, go to the Main Page.
        SendCommand(pMainPage);     // This is the command to go to the Main Page.
        CurrentPage = MAINPAGE;
        // Serial.println(CurrentPage);
    }
}
// *********************************************************************************************************************************/
void DrainSlowly()
{
    // Serial.println("'<<' was pressed");
    SetPumpSpeed(-150);

    // uint8_t in1 = digitalRead(PUMP_IN1);
    // Serial.print("IN1 set to ");
    // Serial.println(in1);

    // uint8_t in2 = digitalRead(PUMP_IN2);
    // Serial.print("IN2 set to ");
    // Serial.println(in2);
    
}

// *********************************************************************************************************************************/

void FillSlowly()
{
    // Serial.println("'>>' was pressed");
    SetPumpSpeed(150);

    // uint8_t in1 = digitalRead(PUMP_IN1);
    // Serial.print("IN1 set to ");
    // Serial.println(in1);

    // uint8_t in2 = digitalRead(PUMP_IN2);
    // Serial.print("IN2 set to ");
    // Serial.println(in2);
}



// *********************************************************************************************************************************/

void PumpRamp(){
    
    for (int i = 0; i < 255; i++){
        SetPumpSpeed(i);
        delay(10);
    }
    delay(1500);
    for (int i = 255; i > 0; i--)
    {
        SetPumpSpeed(i);
        delay(10);
    }
}



#endif