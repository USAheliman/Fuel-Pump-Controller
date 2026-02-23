
#ifndef Definitions_H
#define Definitions_H

// *********************************************************************************************************************************/
//              This file has all definitions for the project. It is included in all files that need to use the definitions.
// *********************************************************************************************************************************/

// DEFINES

#define MAXBUFFERSIZE 1024 * 6
#define MAXFILELEN 1024 * 3
#define CHARSMAX 250        // Max length for char arrays
#define NEXTION Serial1     // uses pin 0 and 1 ...  change if not using Serial1
#define SCREENCHANGEWAIT 10 // allow 10ms for screen to appear

// *********************************************************************************************************************************/
// Sensor PIN Definitions

#define FILL_RELAY 2          // Fill Relay         - Pin 2
#define DRAIN_RELAY 3         // Drain Relay        - Pin 3
#define FILLSENSOR 4          // Fill Sensor        - Pin 4
#define DRAINSENSOR 5         // Drain Sensor       - Pin 5
#define FULLSENSOR 6          // Tank full sensor   - pin 6
#define PUMP_ENA 7            // Pump Enable        - Pin 7
#define PUMP_IN1 8            // Pump IN1           - Pin 8
#define PUMP_IN2 9            // Pump IN2           - Pin 9

#define USERUPDATEFREQUENCY 500  // Update the user every 500ms
#define CALIBRATIONFACTOR 450 // The hall-effect flow sensor outputs approximately 4.5 pulses per second per litre/minute of flow.

// *********************************************************************************************************************************/
// COMMANDS FROM NEXTION DISPLAY
#define FILLMODEL 1
#define DRAINMODEL 2
#define STOPPUMP 3
#define SLOWDRAIN 4
#define SLOWFILL 5
#define RESETVOLUME 6


// *********************************************************************************************************************************/
// SCREENS ON NEXTION DISPLAY
#define MAINPAGE 0
#define FILLPAGE 1
#define DRAINPAGE 2
#define POPUPVIEW 3

// *********************************************************************************************************************************/
// Function prototypes

void SendCommand(char *tbox);
int InStrng(char *text1, char *text2);
void DelayWithDog(uint32_t ms);
FASTRUN char *Str(char *s, int n, int comma); // comma = 0 add nothing, 1 = add comma, 2 = add dot.
void ChecksensorPin();
void PumpRamp();
void CheckBattery();

// *********************************************************************************************************************************/
// GLOBAL variables and Constants

Adafruit_INA219 ina219;                  // Creat an INA219 instance
bool ValueSent = false;
char TextIn[200];
bool ButtonClicks = true;
bool INA219Connected = false;

uint8_t CurrentPage = 0;   // Create a variable to store which page is currently loaded
char pFillPage[] = "page Fill Page";
char pDrainPage[] = "page Drain Page";
char pMainPage[] = "page Main Page";

// The hall-effect flow sensor outputs approximately 4.5 pulses per second per
// litre/minute of flow.
volatile uint32_t PulseCount = 0;
float FlowRate = 0.0f;   // IMPORTANT
float VolumeSoFar = 0.0f;

uint32_t flowMilliLitres;
uint64_t totalMilliLitres;
uint32_t oldTime;
double busvoltage = 0;
extern bool PumpEnabled;

#endif
