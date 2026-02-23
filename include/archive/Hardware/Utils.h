
#ifndef UTILS_H
#define UTILS_H

// *********************************************************************************************************************************
//                       This file has various utility functions needed for the project.
// *********************************************************************************************************************************/

// *********************************************************************************************************************************/
// Here is the Function Pointers' Array. It is an array of pointers to functions.
// When a button is pressed on the Nextion screen, the number is sent to the Teensy.
// The Teensy then uses that number to call the correct function from the array.
// This is a very efficient way to handle button presses, as it is very fast and uses very little memory.
// To add a new function, simply add a new function here, and be sure button sends the correct number.
// *********************************************************************************************************************************/

#define NUMBER_OF_FUNCTIONS 7 // MUST be one more than final function, because first is number zero.

void (*FunctionPointersArray[NUMBER_OF_FUNCTIONS])()={

    DoNothing,      // 0  (0 is not used, but it must be included.)
    FillModelTank,  // 1  (Nextion sent 1, meaning 'Fill' was pressed.)
    DrainModelTank, // 2  (Nextion sent 2, meaning 'Drain' was pressed.)
    StopThePump,    // 3  (Nextion sent 3, meaning 'Stop' was pressed.)
    DrainSlowly,    // 4  (Nextion sent 4, meaning 'Slow' drain was pressed.)
    FillSlowly,     // 5  (Nextion sent 5, meaning 'Slow' fill was pressed.)
    ResetVolume,    // 6  (Nextion sent 6, meaning 'Reset' was pressed.)

    // ... add more functions here. And when you do, change the NUMBER_OF_FUNCTIONS to one more than the last one.
};

// *********************************************************************************************************************************
// This function is called when a button is pressed on the Nextion screen.
// It assumes that the first 4 bytes of TextIn contain the 32BIT number sent by button, and nothing else, ever.
// If the number is not in our list, it does nothing, because an out of range number might crash the program.
// *********************************************************************************************************************************

void ButtonWasPressed() {
    union
    { // This union is used to convert the first 4 bytes of TextIn to a 32BIT number.
        uint8_t First4Bytes[4];
        uint32_t FirstDWord;
    } NextionCommand;

    if ((TextIn[0]) || (TextIn[1]) || (TextIn[2]) || (TextIn[3])) // If there is anything in first four bytes ...
    {
        for (uint8_t i = 0; i < 4; ++i)
        {
            NextionCommand.First4Bytes[i] = TextIn[i]; // ... Copy the first 4 bytes of TextIn to the 'union' in order to convert them to a 32 bit number.
        }

        uint32_t ThisCommand = NextionCommand.FirstDWord;              // Copy to a 32 bit unsigned int var.
        if ((ThisCommand > 0) && (ThisCommand <= NUMBER_OF_FUNCTIONS)) // If the number is in our list of functions, do something.
        {
            FunctionPointersArray[ThisCommand](); // <<< **** Call the function associated with the number!!! **** >>>
        }
    }
}
// ********************************************************************************************************************************
// This function is called very frequently from loop() to check if a button has been pressed on the Nextion screen.
// When a button is pressed, it calls ButtonWasPressed() to handle the button press
// ********************************************************************************************************************************

void CheckForNextionButtonPress()
{
    static uint8_t w0 = 0, w1 = 0, w2 = 0, w3 = 0; // 4-byte sliding window

    while (NEXTION.available())
    {
        uint8_t b = (uint8_t)NEXTION.read();

        // shift window: oldest drops off, newest becomes w0
        w3 = w2;
        w2 = w1;
        w1 = w0;
        w0 = b;

        // Valid command pattern in your system is: [cmd][0][0][0]
        if (w1 == 0 && w2 == 0 && w3 == 0)
        {
            uint32_t cmd = w0;

            if (cmd > 0 && cmd <= NUMBER_OF_FUNCTIONS)
            {
                FunctionPointersArray[cmd]();   // execute action immediately

                // clear window so one press triggers once
                w0 = w1 = w2 = w3 = 0;
            }
        }
    }
}

// ********************************************************************************************************************************
// Replaces delay(ms) and allows for a watchdog call etc to be inserted.
// ********************************************************************************************************************************

void DelayWithDog(uint32_t ms) {

    uint32_t start = millis();
    while (millis() - start < ms)
    {
        // insert watchdog call etc here
        delay(1);
    }
}

// *********************************************************************************************************************************
// @returns position of text1 within text2 or 0 if not found
// *********************************************************************************************************************************

int InStrng(char *text1, char *text2)
{
    for (uint16_t j = 0; j < strlen(text2); ++j)
    {
        bool flag = false;
        for (uint16_t i = 0; i < strlen(text1); ++i)
        {
            if (text1[i] != text2[i + j])
            {
                flag = true;
                break;
            }
        }
        if (!flag)
            return j + 1; // Found match
    }
    return 0; // Found no match
}

//*********************************************************************************************************************************
// This function converts a signed int to a char[] array, then adds a comma, a dot, or nothing at the end.
// It builds the char[] array at a pointer (*s) Where there MUST be enough space for all characters plus a zero terminator.
//*********************************************************************************************************************************

FASTRUN char *Str(char *s, int n, int comma) {// comma = 0 for nothing, 1 for a comma, 2 for a dot.

    int r, i, m, flag;
    char cma[] = ",";
    char dot[] = ".";

    flag = 0;
    i = 0;
    m = 1000000000;
    if (n < 0)
    {
        s[0] = '-';
        i = 1;
        n = -n;
    }
    if (n == 0)
    {
        s[0] = 48;
        s[1] = 0;

        if (comma == 1)
        {
            strcat(s, cma);
        }
        if (comma == 2)
        {
            strcat(s, dot);
        }
        return s;
    }
    while (m >= 1)
    {
        r = n / m;
        if (r > 0)
        {
            flag = 1;
        } //  first digit
        if (flag == 1)
        {
            s[i] = 48 + r;
            ++i;
            s[i] = 0;
        }
        n -= (r * m);
        m /= 10;
    }
    if (comma == 1)
    {
        strcat(s, cma);
    }
    if (comma == 2)
    {
        strcat(s, dot);
    }
    return s;
}

// ****************************************************************************************
void UpdatePumpSpeedFromSliders()
{
    if (!PumpEnabled) return;           // <-- key line

    static uint32_t lastPoll = 0;
    if (millis() - lastPoll < 100) return;
    lastPoll = millis();

    if (CurrentPage == FILLPAGE)
    {
        char s[] = "PumpSpeedFill";
        int v = (int)GetValue(s);
        v = constrain(v, 0, 255);
        SetPumpSpeed(v);
    }
    else if (CurrentPage == DRAINPAGE)
    {
        char s[] = "PumpSpeedDrain";
        int v = (int)GetValue(s);
        v = constrain(v, 0, 255);
        SetPumpSpeed(-v);
    }
}

// *********************************************************************************************************************************/
void GetCurrentFlowRate()
{
  static uint32_t lastTime = 0;
  static uint32_t lastPulse = 0;

  uint32_t now = millis();
  if (lastTime == 0) { lastTime = now; return; }

  uint32_t dt = now - lastTime;
  if (dt < 1000) return;              // update once per second

  noInterrupts();
  uint32_t p = PulseCount;
  interrupts();

  uint32_t dp = p - lastPulse;

  float pulsesPerSec = (dp * 1000.0f) / dt;
  FlowRate = pulsesPerSec / (float)CALIBRATIONFACTOR;

  lastPulse = p;
  lastTime = now;

  // Serial.print("dp="); Serial.print(dp);
  // Serial.print(" dt="); Serial.print(dt);
  // Serial.print(" pps="); Serial.print(pulsesPerSec, 3);
  // Serial.print(" Flow="); Serial.println(FlowRate, 6);
}

// *********************************************************************************************************************************/

void GetVolumeSoFar()
{
    
    noInterrupts();
    uint32_t pulseSnapshot = PulseCount;
    interrupts();

    VolumeSoFar = (float)pulseSnapshot / (float)CALIBRATIONFACTOR;
}

// *********************************************************************************************************************************/

void GetUserInfo()
{
    char Volume_ml[] = "Volume_ml";    // initialises the array with the text "Volume_ml"
    char Flow_Rate[] = "Flow_Rate";    // initialises the array with the text "Flow Rate"

    static uint32_t localtimer = 0;
    if ((millis() - localtimer) < USERUPDATEFREQUENCY)
    {
        return;
    }
    localtimer = millis();
    GetCurrentFlowRate();
    GetVolumeSoFar();

    SendValue(Volume_ml, VolumeSoFar);     // Display Volume on Nextion
    SendValue(Flow_Rate, FlowRate*1000);   // Display Flow Rate on Nextion
    
    
    // Serial.print("Volume So Far: ");
    // Serial.print(VolumeSoFar);
    // Serial.println(" ml");
    // Serial.print("Flow Rate: ");
    // Serial.print(FlowRate);
    // Serial.println(FlowRate, 3); // 3 decimal places 
    // Serial.println(" ml/m");

}

// ************************************************************
void ChecksensorPin()
// This is the pulse counter
{
    uint8_t ThisSensor;
    uint8_t ThisReading;
    static uint32_t localtimer = 0;
    static uint8_t PreviousSensorReading = 0;

    if (micros() - localtimer < 1000)
    {
        return;
    }
    localtimer = micros();

    if (CurrentPage == FILLPAGE)
    {
        ThisSensor = FILLSENSOR;
    }
    else
    {
        ThisSensor = DRAINSENSOR;
    }
    ThisReading = (digitalRead(ThisSensor));

    if (ThisReading != PreviousSensorReading)
    {
        PreviousSensorReading = ThisReading;
        PulseCount++;
    }

    
}

/******************************************************************************************************************************/
// Windows style MSGBOX
// params:
// Prompt is the prompt
// goback is the command needed to return to calling page

void MsgBox(char* goback, char* Prompt)
{
    char GoPopupView[] = "page PopupView";
    char Dialog[]      = "Dialog";
    char NoCancel[]    = "vis b1,0"; // hide cancel button
    char NoOK[]        = "vis b0,0"; // hide OK button
    uint8_t C = 1;
    SendCommand(GoPopupView);
    SendCommand(NoCancel);
    SendCommand(NoOK);
    SendText(Dialog, Prompt);
//  GetYesOrNo();

    while (C == 1){

    }
  //  SendCommand(goback);
    return;
}

// /******************************************************************************************************************************/
// void YesPressed() { Confirmed[0] = 'Y'; }
// /******************************************************************************************************************************/
// void NoPressed() { Confirmed[0] = 'N'; }
// /******************************************************************************************************************************/


// /******************************************************************************************************************************/

// void GetYesOrNo(){                  // on return from here, Confirmed[0] will be Y or N
//     Confirmed[0] = '?';
//     while (Confirmed[0] == '?') { // await user response
//         CheckForNextionButtonPress();
//         CheckPowerOffButton();// heer
//         KickTheDog();
//         if (BoundFlag && ModelMatched) {
//            GetNewChannelValues();
//            FixMotorChannel();
//            SendData();
//         }
//     }
// }


#endif // UTILS_H
