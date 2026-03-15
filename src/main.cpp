#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <SD.h>
#include "version.h"

// ===============================
// PIN DEFINITIONS
// ===============================
#define FILL_RELAY 2
#define DRAIN_RELAY 3

#define FILL_FLOW_PIN  4
#define DRAIN_FLOW_PIN 5

#define TANK_FULL_PIN 6
#define TANK_FULL_ACTIVE_HIGH 1

// ===============================
// PUMP DRIVER (BTS7960)
// ===============================
#define PUMP_RPWM 7
#define PUMP_LPWM 10
#define PUMP_REN  8
#define PUMP_LEN  9

// ===============================
// SD CARD
// ===============================
#define SD_CS_PIN    BUILTIN_SDCARD
#define CONFIG_FILE  "/models.cfg"
#define STATION_FILE "/station.cfg"

// ===============================
// NEXTION
// ===============================
#define NEXTION Serial1
#define NEXTION_BAUD 921600

// ===============================
// PAGE NAMES
// ===============================
#define PAGE_MAIN    "MainPage"
#define PAGE_FILL    "FillPage"
#define PAGE_DRAIN   "DrainPage"
#define PAGE_LOWBATT "LowBatPage"
#define PAGE_SETUP   "SetupPage"
#define PAGE_STATION "StationPage"

#define SLIDER_FILL  "PumpSpeedFill"
#define SLIDER_DRAIN "PumpSpeedDrain"

// Fill page objects
#define NX_FLOW_RATE_FILL_OBJ "FlowFill"
#define NX_VOLUME_FILL_OBJ    "VolFill"
#define NX_TARGET_FILL_OBJ    "TgtFill"
#define NX_PROGRESS_FILL_OBJ  "ProgFill"
#define NX_PERCENT_FILL_OBJ   "PctFill"
#define NX_STOP_REASON_OBJ    "StopReason"

// Drain page objects
#define NX_FLOW_RATE_DRAIN_OBJ "FlowDrain"
#define NX_VOLUME_DRAIN_OBJ    "VolDrain"
#define NX_TARGET_DRAIN_OBJ    "TgtDrain"

// Main page objects
#define NX_VOLUME_MAIN_OBJ  "VolMain"
#define NX_ACTIVE_MODEL_OBJ "tActiveModel"

// Battery / Current UI objects
#define NX_BATT_BAR_OBJ "BatBar"
#define NX_BATT_PCT_OBJ "BatPct"
#define NX_CUR_BAR_OBJ  "CurBar"
#define NX_CUR_TXT_OBJ  "CurTxt"

// Low battery page objects
#define NX_LB_PACKV_TXT "LbPackV"
#define NX_LB_CELLV_TXT "LbCellV"
#define NX_LB_CELLS_TXT "LbCells"

// Setup page objects
#define NX_S_NAME     "sName"
#define NX_S_PIC      "sPic"
#define NX_S_TANK_VOL "sTankVol"
#define NX_S_SENSOR   "sSensor"
#define NX_S_PUMP_SPD "sPumpSpd"
#define NX_S_OVERFLOW   "sOverflow"
#define NX_S_DRAIN_SPD  "sDrainSpd"

// Speed label objects (show ml/min next to sliders)
#define NX_FILL_SPD_LABEL "t0"  // on FillPage
#define NX_DRAIN_SPD_LABEL "t0" // on DrainPage

// Heli and supply bar graph objects (Fill and Drain pages)
#define NX_HELI_BAR_OBJ "ProgHeli"
#define NX_HELI_PCT_OBJ "tHeliPct"
#define NX_HELI_VOL_OBJ "tHeliVol"
#define NX_SUP_BAR_OBJ  "ProgSup"
#define NX_SUP_PCT_OBJ  "tSupPct"
#define NX_SUP_VOL_OBJ  "tSupVol"

// Station page objects
#define NX_ST_CAP_VAL         "tCapVal"
#define NX_ST_REM_VAL         "tRemVal"
#define NX_ST_LOW_VAL         "tLowVal"
#define NX_ST_FLOW_DROP_VAL   "tFlowDrop"
#define NX_ST_FILL_PULSE_VAL  "tFillPulseVal"
#define NX_ST_FILL_STATUS     "tFillCalStatus"
#define NX_ST_DRAIN_PULSE_VAL "tDrainPulseVal"
#define NX_ST_DRAIN_STATUS    "tDrainCalStat"

// ===============================
// PAGE STATE
// ===============================
#define MAINPAGE    0
#define FILLPAGE    1
#define DRAINPAGE   2
#define LOWBATTPAGE 3
#define SETUPPAGE   4
#define STATIONPAGE 5

uint8_t CurrentPage = MAINPAGE;
bool PumpEnabled = false;

#define NX_PAGE_REPORT_MAIN   5001
#define NX_PAGE_REPORT_FILL   5002
#define NX_PAGE_REPORT_DRAIN  5003
#define NX_PAGE_REPORT_LOWBAT 5004

// Setup page command codes
#define NX_CMD_SETUP_PAGE  4000
#define NX_CMD_MODEL1      4001
#define NX_CMD_MODEL2      4002
#define NX_CMD_MODEL3      4003
#define NX_CMD_MODEL4      4004
#define NX_CMD_SELECT      4010
#define NX_CMD_SAVE        4011
#define NX_CMD_BACK_SETUP  4020
#define NX_CMD_STATION     4030

// Station page command codes
#define NX_CMD_BACK_STATION    7000
#define NX_CMD_FILL_CAL_START  7001
#define NX_CMD_FILL_CAL_STOP   7002
#define NX_CMD_DRAIN_CAL_START 7003
#define NX_CMD_DRAIN_CAL_STOP  7004
#define NX_CMD_SET_CAP         7010
#define NX_CMD_RESET_FULL      7011
#define NX_CMD_SET_LOW         7012
#define NX_CMD_SET_FLOW_DROP   7013
#define NX_CMD_FILL_CAL_VOL    7020
#define NX_CMD_DRAIN_CAL_VOL   7021

// Calibration pump speed (fixed)
#define CAL_PWM 150

// Setup page command codes
#define NX_CMD_OVERFLOW_PURGE 6004
#define NX_CMD_DRAIN_SPEED    6005

// ===============================
// MODEL CONFIGURATION
// ===============================
#define NUM_MODELS 4

struct ModelConfig
{
  char    name[24];
  int     tankVolumeMl;
  bool    hasTankSensor;
  int     pumpSpeed;         // fill speed
  uint8_t picIndex;
  int     overflowPurgeSecs; // 0 = skip purge, 1-10 seconds
  int     drainSpeed;        // drain speed (all drain operations including purge)
};

ModelConfig models[NUM_MODELS] = {
  { "BO 105",   2000, true,  127, 3, 3, 127 },
  { "Whiplash", 1000, false, 127, 5, 3, 127 },
  { "Alouette", 1500, true,  127, 2, 3, 127 },
  { "EC 145",   3000, true,  127, 4, 3, 127 },
};

int activeModelIndex  = 0;
int previewModelIndex = 0;

// ===============================
// SUPPLY TANK
// ===============================
#define SUPPLY_TANK_DEFAULT_ML 20000
#define SUPPLY_LOW_DEFAULT_ML   2000  // 2L default warning threshold

int supplyTankCapacityMl   = SUPPLY_TANK_DEFAULT_ML;
int supplyTankRemainingMl  = SUPPLY_TANK_DEFAULT_ML;
int supplyAtSessionStartMl = SUPPLY_TANK_DEFAULT_ML;
int supplyLowThresholdMl   = SUPPLY_LOW_DEFAULT_ML;

// Flash state for low supply warning
static bool     supplyLowFlash    = false;
static uint32_t supplyFlashLastMs = 0;

// Flash state for StopReason message (auto sequence status)
static bool stopReasonFlash       = false;
static bool stopReasonFlashActive = false;

// ===============================
// FLOW SENSOR CALIBRATION
// ===============================
#define FILL_PULSES_DEFAULT  1696.0f
#define DRAIN_PULSES_DEFAULT 1696.0f

float fillPulsesPerLiter  = FILL_PULSES_DEFAULT;
float drainPulsesPerLiter = DRAIN_PULSES_DEFAULT;

bool     fillCalActive       = false;
bool     drainCalActive      = false;
uint32_t fillCalStartPulses  = 0;
uint32_t drainCalStartPulses = 0;
uint32_t fillCalStartMs      = 0;   // calibration start timestamp
uint32_t drainCalStartMs     = 0;   // calibration start timestamp

// Flow rate to PWM conversion factor
// Derived from calibration: ml/min per PWM unit above MIN_PWM
float mlPerMinPerPwm = 0.0f;  // 0 = not yet calibrated

// ===============================
// AUTO DRAIN-THEN-FILL SEQUENCE
// ===============================
enum AutoFillState { AF_NONE, AF_DRAIN_PENDING, AF_DRAINING, AF_FILLING, AF_PURGING };
AutoFillState autoFillSequence = AF_NONE;

static uint32_t autoFillTransitionMs = 0;  // timestamp for drain->fill pause
#define AUTO_FILL_PAUSE_MS 2000            // 2 second pause between drain and fill

static uint32_t purgeStartMs = 0;         // timestamp for overflow purge start

// ===============================
// CLOSED LOOP FLOW CONTROLLER
// ===============================
#define CL_KP             0.015f  // proportional gain
#define CL_KI             0.010f  // integral gain
#define CL_INTEGRAL_MAX   50.0f   // anti-windup clamp

bool    closedLoopActive       = false;
int     closedLoopTargetMlMin  = 0;
float   closedLoopIntegral     = 0.0f;
float   closedLoopPwmFloat     = 0.0f;  // float accumulator to avoid rounding errors
int     closedLoopCurrentPwm   = 0;
uint8_t closedLoopSettledCount  = 0;  // consecutive readings within tolerance
#define CL_SETTLED_TOLERANCE  20   // ml/min — within this = settled
#define CL_SETTLED_COUNT       5   // readings to confirm settled

// ===============================
// TANK EMPTY DETECTION
// ===============================
#define TANK_EMPTY_FLOW_DROP_DEFAULT 30  // 30% drop from peak = tank empty
#define TANK_EMPTY_MIN_RUN_MS       3000 // must run 3s before detection active
#define TANK_EMPTY_CONFIRM_COUNT    2    // consecutive low readings to confirm
#define TANK_EMPTY_MIN_PEAK_FLOW    200  // ml/min — if peak never reaches this after min run, tank was already empty

int      tankEmptyFlowDropPct  = TANK_EMPTY_FLOW_DROP_DEFAULT;
int      drainPeakFlowMlMin    = 0;
uint32_t drainStartMs          = 0;
uint8_t  tankEmptyCount        = 0;
int lastFillVolumeMl  = 0;
int lastDrainVolumeMl = 0;
int targetFillMl      = 0;
int targetDrainMl     = 0;

// ===============================
// MINIMUM SPEED FLOOR
// ===============================
#define MIN_PWM 25   // minimum PWM before pump stalls
#define MAX_PWM 225  // maximum PWM at full flow (~1000ml/min on LiPo)

int ApplyMinimumFloor(uint32_t raw)
{
  int v = constrain((int)raw, 0, 255);
  if (v == 0) return 0;
  return map(v, 1, 255, MIN_PWM, MAX_PWM);
}

// ===============================
// FLOW RATE CONVERSION
// ===============================
static int PwmToMlMin(int pwm)
{
  if (mlPerMinPerPwm <= 0.0f) return 0;  // not calibrated
  int usable = pwm - MIN_PWM;
  if (usable <= 0) return 0;
  return (int)(usable * mlPerMinPerPwm + 0.5f);
}

static int MlMinToPwm(int mlMin)
{
  if (mlPerMinPerPwm <= 0.0f) return MIN_PWM;  // not calibrated
  if (mlMin <= 0) return 0;
  int pwm = MIN_PWM + (int)((float)mlMin / mlPerMinPerPwm + 0.5f);
  return constrain(pwm, MIN_PWM, MAX_PWM);
}

static void FormatMlMin(int pwm, char* buf, int bufSize)
{
  if (mlPerMinPerPwm <= 0.0f)
    snprintf(buf, bufSize, "--- ml/m");
  else
    snprintf(buf, bufSize, "%d ml/m", PwmToMlMin(pwm));
}

// ===============================
// RAMP SETTINGS
// ===============================
#define RAMP_INTERVAL_MS 50   // 50ms between steps
#define RAMP_STEP        1    // 1 PWM unit per step = 20 units/sec

int currentSpeedSigned = 0;
int targetSpeedSigned  = 0;

// ===============================
// FLOW SENSOR
// ===============================
#define HZ_PER_LPM 57.0f

volatile uint32_t fillPulses  = 0;
volatile uint32_t drainPulses = 0;

// ===============================
// INA219 / BATTERY
// ===============================
Adafruit_INA219 ina219;

#define CUTOFF_V_PER_CELL 3.82f
#define MAX_CURRENT_A     5.0f

int  cellCount         = 0;
bool lowBatteryLatched = false;

// ===============================
// VOLTAGE SAG FILTER
// ===============================
#define SAG_FILTER_ALPHA  0.20f
#define SAG_TRIP_COUNT    2
#define SAG_HYST_PER_CELL 0.05f

static float   filteredPackV = 0.0f;
static bool    filterInit    = false;
static uint8_t lowCount      = 0;

// ===============================
// FLOW UI STATE
// ===============================
#define FLOW_SMOOTH_SAMPLES 1  // no averaging — fastest response for closed loop

struct FlowUiState
{
  uint32_t lastMs     = 0;
  uint32_t lastPulses = 0;
  int lastSentFlow    = -1;
  int lastSentVol     = -1;
  int lastSentPct     = -1;
  int flowSamples[FLOW_SMOOTH_SAMPLES] = {0};
  int flowSampleIdx   = 0;
};

static FlowUiState gFillUi;
static FlowUiState gDrainUi;

static inline void ResetFlowUi(FlowUiState &s)
{
  s.lastMs       = 0;
  s.lastPulses   = 0;
  s.lastSentFlow = -1;
  s.lastSentVol  = -1;
  s.lastSentPct  = -1;
  for (int i = 0; i < FLOW_SMOOTH_SAMPLES; i++) s.flowSamples[i] = 0;
  s.flowSampleIdx = 0;
}

// ===============================
// FORWARD DECLARATIONS
// ===============================
static void UpdatePowerUIAndSafety();
void ProcessNextion();
void SetPumpOutput(int signedSpeed);
void SetTargetSpeed(int signedSpeed);
void UpdateRamp();
void StopPump();
void EnterFillPage();
void EnterDrainPage();
void RefreshFillPage();
void RefreshDrainPage();
void EnterSetupPage();
void EnterStationPage();
void BeginFill(int mlMin);
void BeginDrain(int pwm);
void BeginAutoSequenceDrain();
void BeginOverflowPurge();
void EnterLowBatteryPage(float packV, float vPerCell);
void UpdateFlowDisplaysAutoStopAndProgress();
void InitFlowSensors();
void SaveModelsToSD();
void LoadModelsFromSD();
void SaveStationToSD();
void LoadStationFromSD();
void ShowModelOnSetupPanel(int idx);
void ApplyActiveModel();
void UpdateMainPageModel();
static void UpdateSupplyTankUI();
static void UpdateSupplyLowWarning();
static void UpdateStationPageValues();

// ===============================
// NEXTION TX HELPERS
// ===============================
static inline void NxEnd()
{
  NEXTION.write(0xFF);
  NEXTION.write(0xFF);
  NEXTION.write(0xFF);
}

static void NxCmd(const char* cmd)
{
  NEXTION.print(cmd);
  NxEnd();
}

static void NxGotoPage(const char* pageName)
{
  char buf[48];
  snprintf(buf, sizeof(buf), "page %s", pageName);
  NxCmd(buf);
}

static void NxSetVal(const char* objName, int val)
{
  char buf[72];
  snprintf(buf, sizeof(buf), "%s.val=%d", objName, val);
  NxCmd(buf);
}

static void NxSetText(const char* objName, const char* txt)
{
  char buf[128];
  snprintf(buf, sizeof(buf), "%s.txt=\"%s\"", objName, txt);
  NxCmd(buf);
}

static void NxSetPic(const char* objName, int picIndex)
{
  char buf[72];
  snprintf(buf, sizeof(buf), "%s.pic=%d", objName, picIndex);
  NxCmd(buf);
}

// ===============================
// FLOW ISRs
// ===============================
void FillFlowISR()  { fillPulses++;  }
void DrainFlowISR() { drainPulses++; }

void InitFlowSensors()
{
  pinMode(FILL_FLOW_PIN, INPUT_PULLUP);
  pinMode(DRAIN_FLOW_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FILL_FLOW_PIN),  FillFlowISR,  RISING);
  attachInterrupt(digitalPinToInterrupt(DRAIN_FLOW_PIN), DrainFlowISR, RISING);
}

// ===============================
// PUMP DRIVER
// ===============================
static inline void PumpDriverEnable()
{
  digitalWrite(PUMP_REN, HIGH);
  digitalWrite(PUMP_LEN, HIGH);
}

static inline void PumpDriverDisable()
{
  analogWrite(PUMP_RPWM, 0);
  analogWrite(PUMP_LPWM, 0);
  digitalWrite(PUMP_REN, LOW);
  digitalWrite(PUMP_LEN, LOW);
}

void SetPumpOutput(int signedSpeed)
{
  signedSpeed = constrain(signedSpeed, -255, 255);
  int mag = abs(signedSpeed);

  if (signedSpeed > 0)
  {
    analogWrite(PUMP_LPWM, 0);
    analogWrite(PUMP_RPWM, mag);
  }
  else if (signedSpeed < 0)
  {
    analogWrite(PUMP_RPWM, 0);
    analogWrite(PUMP_LPWM, mag);
  }
  else
  {
    analogWrite(PUMP_RPWM, 0);
    analogWrite(PUMP_LPWM, 0);
  }
}

void SetTargetSpeed(int signedSpeed)
{
  targetSpeedSigned = constrain(signedSpeed, -255, 255);
}

void UpdateRamp()
{
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < RAMP_INTERVAL_MS) return;
  last = now;

  int desired = PumpEnabled ? targetSpeedSigned : 0;

  if ((currentSpeedSigned > 0 && desired < 0) || (currentSpeedSigned < 0 && desired > 0))
    desired = 0;

  if (currentSpeedSigned == desired) return;

  int diff = desired - currentSpeedSigned;
  if (diff > 0) currentSpeedSigned += min(RAMP_STEP, diff);
  else          currentSpeedSigned -= min(RAMP_STEP, -diff);

  SetPumpOutput(currentSpeedSigned);
}

// ===============================
// HELPERS
// ===============================
static bool IsTankFull()
{
  int raw = digitalRead(TANK_FULL_PIN);
  bool active = (TANK_FULL_ACTIVE_HIGH ? (raw == HIGH) : (raw == LOW));

  static uint8_t activeCount = 0;
  if (active) { if (activeCount < 255) activeCount++; }
  else          activeCount = 0;

  return (activeCount >= 2);
}

static int ClampPct(int pct) { if (pct < 0) return 0; if (pct > 100) return 100; return pct; }

static int LiPoPctFromV(float vPerCell)
{
  static const float V[] = {
    4.20f, 4.15f, 4.11f, 4.08f, 4.02f, 3.98f, 3.95f, 3.91f,
    3.87f, 3.85f, 3.84f, 3.82f, 3.80f, 3.78f, 3.75f, 3.73f, 3.70f
  };
  static const int P[] = {
    100, 95, 90, 85, 80, 75, 70, 60,
     50, 45, 40, 30, 20, 15, 10,  5,  0
  };
  const int N = (int)(sizeof(P) / sizeof(P[0]));

  if (vPerCell >= V[0])   return 100;
  if (vPerCell <= V[N-1]) return 0;

  for (int i = 0; i < N - 1; i++)
  {
    if (vPerCell <= V[i] && vPerCell >= V[i+1])
    {
      float t   = (vPerCell - V[i+1]) / (V[i] - V[i+1]);
      float pct = (float)P[i+1] + t * (float)(P[i] - P[i+1]);
      return ClampPct((int)(pct + 0.5f));
    }
  }
  return 0;
}

static int CurrentToPct(float currentA)
{
  float pct = (currentA / MAX_CURRENT_A) * 100.0f;
  return ClampPct((int)(pct + 0.5f));
}

static void DetectCellCount(float packV)
{
  if (cellCount != 0) return;
  if (packV >= 9.0f) cellCount = 3;
  else cellCount = 2;
}

// Nextion colour constants
#define NX_COLOR_WHITE       65535
#define NX_COLOR_TXT_NORMAL  1055   // tSupVol normal pco
#define NX_COLOR_RED         63488
#define NX_COLOR_BLACK       0
#define NX_COLOR_GREEN_BAR   1024   // ProgSup pco (bar fill colour)

// ===============================
// SUPPLY LOW WARNING FLASH
// Called from UpdatePowerUIAndSafety every 500ms
// ===============================
static void NxSetAttr(const char* objAttr, int val)
{
  char buf[72];
  snprintf(buf, sizeof(buf), "%s=%d", objAttr, val);
  NxCmd(buf);
}

static void UpdateSupplyLowWarning()
{
  if (CurrentPage != FILLPAGE && CurrentPage != MAINPAGE && CurrentPage != DRAINPAGE) return;

  bool isLow = (supplyTankRemainingMl <= supplyLowThresholdMl);

  if (!isLow)
  {
    NxSetAttr("ProgSup.pco", NX_COLOR_GREEN_BAR);
    NxSetAttr("tSupVol.pco", NX_COLOR_TXT_NORMAL);
    supplyLowFlash = false;
    return;
  }

  supplyLowFlash = !supplyLowFlash;
  NxSetAttr("ProgSup.pco", supplyLowFlash ? NX_COLOR_RED : NX_COLOR_GREEN_BAR);
  NxSetAttr("tSupVol.pco", NX_COLOR_RED);
}

// ===============================
// STOP REASON FLASH
// Called from UpdatePowerUIAndSafety every 500ms
// ===============================
static void UpdateStopReasonFlash()
{
  if (!stopReasonFlashActive) return;
  if (CurrentPage != FILLPAGE && CurrentPage != DRAINPAGE) return;

  stopReasonFlash = !stopReasonFlash;
  NxSetAttr("StopReason.pco", stopReasonFlash ? NX_COLOR_RED : NX_COLOR_BLACK);
}

// ===============================
// SUPPLY TANK UI UPDATE
// (used on Fill/Drain/Main pages)
// ===============================
static void UpdateSupplyTankUI()
{
  supplyTankRemainingMl = constrain(supplyTankRemainingMl, 0, supplyTankCapacityMl);

  int pct = 0;
  if (supplyTankCapacityMl > 0)
  {
    pct = (int)((100.0f * (float)supplyTankRemainingMl) /
          (float)supplyTankCapacityMl + 0.5f);
    pct = constrain(pct, 0, 100);
  }

  NxSetVal(NX_SUP_BAR_OBJ, pct);

  char pctStr[10];
  snprintf(pctStr, sizeof(pctStr), "%d%%", pct);
  NxSetText(NX_SUP_PCT_OBJ, pctStr);

  float remainingL = supplyTankRemainingMl / 1000.0f;
  float capacityL  = supplyTankCapacityMl  / 1000.0f;
  char volStr[32];
  snprintf(volStr, sizeof(volStr), "%.1f / %.1fL", (double)remainingL, (double)capacityL);
  NxSetText(NX_SUP_VOL_OBJ, volStr);
  // Colours are managed by UpdateSupplyLowWarning() - do not set here
}

// ===============================
// STATION PAGE VALUES UPDATE
// ===============================
static void UpdateStationPageValues()
{
  float capL = supplyTankCapacityMl  / 1000.0f;
  float remL = supplyTankRemainingMl / 1000.0f;
  float lowL = supplyLowThresholdMl  / 1000.0f;
  char buf[32];

  snprintf(buf, sizeof(buf), "%.1fL", (double)capL);
  NxSetText(NX_ST_CAP_VAL, buf);

  snprintf(buf, sizeof(buf), "%.1fL", (double)remL);
  NxSetText(NX_ST_REM_VAL, buf);

  snprintf(buf, sizeof(buf), "%.1fL", (double)lowL);
  NxSetText(NX_ST_LOW_VAL, buf);

  snprintf(buf, sizeof(buf), "%d%%", tankEmptyFlowDropPct);
  NxSetText(NX_ST_FLOW_DROP_VAL, buf);

  snprintf(buf, sizeof(buf), "%.1f", (double)fillPulsesPerLiter);
  NxSetText(NX_ST_FILL_PULSE_VAL, buf);

  snprintf(buf, sizeof(buf), "%.1f", (double)drainPulsesPerLiter);
  NxSetText(NX_ST_DRAIN_PULSE_VAL, buf);
}

// ===============================
// SD CARD — MODELS SAVE / LOAD
// ===============================
void SaveModelsToSD()
{
  if (!SD.begin(SD_CS_PIN))
  {
    Serial.println("SD: save failed - card not found");
    return;
  }

  SD.remove(CONFIG_FILE);
  File f = SD.open(CONFIG_FILE, FILE_WRITE);
  if (!f)
  {
    Serial.println("SD: could not open file for writing");
    return;
  }

  f.println(activeModelIndex);

  for (int i = 0; i < NUM_MODELS; i++)
  {
    f.print(models[i].name);                   f.print(',');
    f.print(models[i].tankVolumeMl);           f.print(',');
    f.print(models[i].hasTankSensor ? 1 : 0);  f.print(',');
    f.print(models[i].pumpSpeed);              f.print(',');
    f.print(models[i].picIndex);               f.print(',');
    f.print(models[i].overflowPurgeSecs);         f.print(',');
    f.println(models[i].drainSpeed);
  }

  f.close();
  Serial.println("SD: models saved");
}

void LoadModelsFromSD()
{
  if (!SD.begin(SD_CS_PIN))
  {
    Serial.println("SD: load failed - using defaults");
    return;
  }

  if (!SD.exists(CONFIG_FILE))
  {
    Serial.println("SD: no config file - using defaults");
    return;
  }

  File f = SD.open(CONFIG_FILE, FILE_READ);
  if (!f)
  {
    Serial.println("SD: could not open config - using defaults");
    return;
  }

  String line = f.readStringUntil('\n');
  activeModelIndex = constrain(line.toInt(), 0, NUM_MODELS - 1);

  for (int i = 0; i < NUM_MODELS; i++)
  {
    line = f.readStringUntil('\n');
    line.trim();

    int c1 = line.indexOf(',');
    int c2 = line.indexOf(',', c1 + 1);
    int c3 = line.indexOf(',', c2 + 1);
    int c4 = line.indexOf(',', c3 + 1);
    int c5 = line.indexOf(',', c4 + 1);

    if (c1 < 0 || c2 < 0 || c3 < 0 || c4 < 0) continue;

    String name = line.substring(0, c1);
    name.toCharArray(models[i].name, sizeof(models[i].name));
    models[i].tankVolumeMl     = line.substring(c1+1, c2).toInt();
    models[i].hasTankSensor    = line.substring(c2+1, c3).toInt() == 1;
    models[i].pumpSpeed        = line.substring(c3+1, c4).toInt();
    int c6 = line.indexOf(',', c5 + 1);
    models[i].picIndex          = (uint8_t)(c5 > 0 ? line.substring(c4+1, c5).toInt() : line.substring(c4+1).toInt());
    models[i].overflowPurgeSecs = (c5 > 0) ? constrain(line.substring(c5+1, c6 > 0 ? c6 : line.length()).toInt(), 0, 10) : 3;
    models[i].drainSpeed        = (c6 > 0) ? constrain(line.substring(c6+1).toInt(), 0, 255) : 127;
  }

  f.close();
  Serial.println("SD: models loaded");
}

// ===============================
// SD CARD — STATION SAVE / LOAD
// ===============================
void SaveStationToSD()
{
  if (!SD.begin(SD_CS_PIN))
  {
    Serial.println("SD: station save failed - card not found");
    return;
  }

  SD.remove(STATION_FILE);
  File f = SD.open(STATION_FILE, FILE_WRITE);
  if (!f)
  {
    Serial.println("SD: could not open station file for writing");
    return;
  }

  f.println(supplyTankCapacityMl);
  f.println(supplyTankRemainingMl);
  f.println((int)(fillPulsesPerLiter  * 10));  // stored x10 to preserve one decimal
  f.println((int)(drainPulsesPerLiter * 10));
  f.println(supplyLowThresholdMl);
  f.println(tankEmptyFlowDropPct);
  f.println((int)(mlPerMinPerPwm * 100));  // stored x100 to preserve two decimals

  f.close();
  Serial.println("SD: station saved");
}

void LoadStationFromSD()
{
  if (!SD.begin(SD_CS_PIN))
  {
    Serial.println("SD: station load failed - using defaults");
    return;
  }

  if (!SD.exists(STATION_FILE))
  {
    Serial.println("SD: no station file - using defaults");
    return;
  }

  File f = SD.open(STATION_FILE, FILE_READ);
  if (!f)
  {
    Serial.println("SD: could not open station file - using defaults");
    return;
  }

  String line = f.readStringUntil('\n');
  supplyTankCapacityMl = constrain(line.toInt(), 1000, 100000);

  line = f.readStringUntil('\n');
  supplyTankRemainingMl = constrain(line.toInt(), 0, supplyTankCapacityMl);

  line = f.readStringUntil('\n');
  int fillX10 = line.toInt();
  if (fillX10 > 0) fillPulsesPerLiter = fillX10 / 10.0f;

  line = f.readStringUntil('\n');
  int drainX10 = line.toInt();
  if (drainX10 > 0) drainPulsesPerLiter = drainX10 / 10.0f;

  line = f.readStringUntil('\n');
  int lowMl = line.toInt();
  if (lowMl > 0) supplyLowThresholdMl = constrain(lowMl, 500, supplyTankCapacityMl);

  line = f.readStringUntil('\n');
  int dropPct = line.toInt();
  if (dropPct > 0) tankEmptyFlowDropPct = constrain(dropPct, 5, 90);

  line = f.readStringUntil('\n');
  int mlPerMinX100 = line.toInt();
  if (mlPerMinX100 > 0) mlPerMinPerPwm = mlPerMinX100 / 100.0f;

  f.close();
  Serial.println("SD: station loaded");
  Serial.print("Supply: ");
  Serial.print(supplyTankRemainingMl);
  Serial.print(" / ");
  Serial.println(supplyTankCapacityMl);
  Serial.print("Fill cal: ");
  Serial.println((double)fillPulsesPerLiter);
  Serial.print("Drain cal: ");
  Serial.println((double)drainPulsesPerLiter);
  Serial.print("mlPerMinPerPwm: ");
  Serial.println((double)mlPerMinPerPwm);
}

// ===============================
// APPLY ACTIVE MODEL
// ===============================
void ApplyActiveModel()
{
  targetFillMl = models[activeModelIndex].tankVolumeMl;
  Serial.print("Active model: ");
  Serial.println(models[activeModelIndex].name);
}

// ===============================
// UPDATE MAIN PAGE MODEL
// ===============================
void UpdateMainPageModel()
{
  NxSetText(NX_ACTIVE_MODEL_OBJ, models[activeModelIndex].name);
  NxSetPic("mMainPic", models[activeModelIndex].picIndex);
}

// ===============================
// SETUP PAGE — SHOW MODEL IN PANEL
// ===============================
void ShowModelOnSetupPanel(int idx)
{
  if (idx < 0 || idx >= NUM_MODELS) return;
  previewModelIndex = idx;

  ModelConfig &m = models[idx];

  char fillMlBuf[16], drainMlBuf[16];
  // pumpSpeed and drainSpeed store ml/min directly — display as-is
  snprintf(fillMlBuf,  sizeof(fillMlBuf),  "%d ml/m", m.pumpSpeed);
  snprintf(drainMlBuf, sizeof(drainMlBuf), "%d ml/m", m.drainSpeed);

  NxSetText(NX_S_NAME,      m.name);
  NxSetVal(NX_S_TANK_VOL,   m.tankVolumeMl);
  NxSetText(NX_S_SENSOR,    m.hasTankSensor ? "YES" : "NO");
  { char spdBuf[8]; snprintf(spdBuf, sizeof(spdBuf), "%d", m.pumpSpeed); NxSetText(NX_S_PUMP_SPD, spdBuf); }
  NxSetVal(NX_S_OVERFLOW,   m.overflowPurgeSecs);
  NxSetVal(NX_S_DRAIN_SPD,  m.drainSpeed);
  NxSetText("sDrainSpdLbl", drainMlBuf);
  NxSetPic(NX_S_PIC,        m.picIndex);
}

// ===============================
// PUMP STOP IN PLACE
// ===============================
static void StopPumpInPlace()
{
  PumpEnabled              = false;
  closedLoopActive         = false;
  closedLoopIntegral       = 0.0f;
  closedLoopPwmFloat       = 0.0f;
  closedLoopSettledCount   = 0;
  SetTargetSpeed(0);
  SetPumpOutput(0);
  currentSpeedSigned = 0;
  PumpDriverDisable();
  digitalWrite(FILL_RELAY, LOW);
  digitalWrite(DRAIN_RELAY, LOW);
}

// ===============================
// LOW BATTERY LATCH
// ===============================
void EnterLowBatteryPage(float packV, float vPerCell)
{
  lowBatteryLatched = true;

  PumpEnabled = false;
  SetTargetSpeed(0);
  SetPumpOutput(0);
  currentSpeedSigned = 0;
  PumpDriverDisable();

  digitalWrite(FILL_RELAY, LOW);
  digitalWrite(DRAIN_RELAY, LOW);

  CurrentPage = LOWBATTPAGE;
  NxGotoPage(PAGE_LOWBATT);

  char buf[32];
  snprintf(buf, sizeof(buf), "Pack voltage : %.2fV", (double)packV);
  NxSetText(NX_LB_PACKV_TXT, buf);

  snprintf(buf, sizeof(buf), "Cell voltage : %.2fV", (double)vPerCell);
  NxSetText(NX_LB_CELLV_TXT, buf);

  if (cellCount == 2)      NxSetText(NX_LB_CELLS_TXT, "2 cell");
  else if (cellCount == 3) NxSetText(NX_LB_CELLS_TXT, "3 cell");
  else                     NxSetText(NX_LB_CELLS_TXT, "?");

  NxSetVal(SLIDER_FILL,  0);
  NxSetVal(SLIDER_DRAIN, 0);
}

// ===============================
// SESSION / PAGE LOGIC
// ===============================
void StopPump()
{
  uint8_t wasPage = CurrentPage;

  PumpEnabled = false;
  SetTargetSpeed(0);
  SetPumpOutput(0);
  currentSpeedSigned = 0;
  PumpDriverDisable();

  digitalWrite(FILL_RELAY, LOW);
  digitalWrite(DRAIN_RELAY, LOW);

  SaveStationToSD();

  CurrentPage = MAINPAGE;
  NxGotoPage(PAGE_MAIN);
  NxSetText("tVersion", FW_VERSION);
  NxSetText("tBattType", cellCount == 3 ? "3S Battery" : "2S Battery");
  UpdateMainPageModel();

  NxSetVal(SLIDER_FILL,  0);
  NxSetVal(SLIDER_DRAIN, 0);

  int showVol = 0;
  if (wasPage == FILLPAGE)  showVol = lastFillVolumeMl;
  if (wasPage == DRAINPAGE) showVol = lastDrainVolumeMl;
  NxSetVal(NX_VOLUME_MAIN_OBJ, showVol);

  // Refresh supply tank display with correct current values
  UpdateSupplyTankUI();
  UpdateSupplyLowWarning();
}

void EnterFillPage()
{
  // Models without tank sensor use auto drain-then-fill sequence
  if (!models[activeModelIndex].hasTankSensor)
  {
    ApplyActiveModel();
    BeginAutoSequenceDrain();
    return;
  }

  noInterrupts(); fillPulses = 0; interrupts();
  lastFillVolumeMl       = 0;
  supplyAtSessionStartMl = supplyTankRemainingMl;

  ResetFlowUi(gFillUi);

  PumpEnabled = false;
  SetTargetSpeed(0);

  digitalWrite(FILL_RELAY, LOW);
  digitalWrite(DRAIN_RELAY, LOW);

  ApplyActiveModel();

  CurrentPage = FILLPAGE;
  NxGotoPage(PAGE_FILL);

  NxSetText(NX_ACTIVE_MODEL_OBJ, models[activeModelIndex].name);
  NxSetPic("mMainPic", models[activeModelIndex].picIndex);

  NxSetVal(NX_TARGET_FILL_OBJ,    targetFillMl);
  NxSetVal(NX_FLOW_RATE_FILL_OBJ, 0);
  NxSetVal(NX_VOLUME_FILL_OBJ,    0);
  NxSetVal(NX_PROGRESS_FILL_OBJ,  0);
  NxSetText(NX_PERCENT_FILL_OBJ,  "0%");
  NxSetVal(SLIDER_FILL,            models[activeModelIndex].pumpSpeed);
  { char mlBuf[16]; snprintf(mlBuf, sizeof(mlBuf), "%d ml/m", models[activeModelIndex].pumpSpeed); NxSetText(NX_FILL_SPD_LABEL, mlBuf); }
  NxSetText(NX_STOP_REASON_OBJ,   "");
  stopReasonFlashActive = false;
  NxSetAttr("StopReason.pco", NX_COLOR_TXT_NORMAL);

  NxSetVal(NX_HELI_BAR_OBJ, 0);
  NxSetText(NX_HELI_PCT_OBJ, "0%");
  char buf[32];
  snprintf(buf, sizeof(buf), "0 / %dml", targetFillMl);
  NxSetText(NX_HELI_VOL_OBJ, buf);

  UpdateSupplyTankUI();
}

void RefreshFillPage()
{
  // Resend all current fill page values to Nextion (called on page reload)
  Serial.print("RefreshFillPage: pumpSpeed=");
  Serial.println(models[activeModelIndex].pumpSpeed);

  NxSetText(NX_ACTIVE_MODEL_OBJ, models[activeModelIndex].name);
  NxSetPic("mMainPic", models[activeModelIndex].picIndex);

  // Restore slider and speed label
  NxSetVal(SLIDER_FILL, models[activeModelIndex].pumpSpeed);
  { char mlBuf[16]; snprintf(mlBuf, sizeof(mlBuf), "%d ml/m", models[activeModelIndex].pumpSpeed); NxSetText(NX_FILL_SPD_LABEL, mlBuf); }

  NxSetVal(NX_TARGET_FILL_OBJ,    targetFillMl);
  NxSetVal(NX_FLOW_RATE_FILL_OBJ, gFillUi.lastSentFlow > 0 ? gFillUi.lastSentFlow : 0);
  NxSetVal(NX_VOLUME_FILL_OBJ,    lastFillVolumeMl);

  int pct = 0;
  if (targetFillMl > 0)
  {
    pct = (int)((100.0f * (float)lastFillVolumeMl) / (float)targetFillMl + 0.5f);
    pct = constrain(pct, 0, 100);
  }
  NxSetVal(NX_PROGRESS_FILL_OBJ, pct);
  char pctStr[10];
  snprintf(pctStr, sizeof(pctStr), "%d%%", pct);
  NxSetText(NX_PERCENT_FILL_OBJ, pctStr);

  NxSetVal(NX_HELI_BAR_OBJ, pct);
  NxSetText(NX_HELI_PCT_OBJ, pctStr);
  char buf[32];
  snprintf(buf, sizeof(buf), "%d / %dml", lastFillVolumeMl, targetFillMl);
  NxSetText(NX_HELI_VOL_OBJ, buf);

  UpdateSupplyTankUI();
  UpdateSupplyLowWarning();
}

void EnterDrainPage()
{
  noInterrupts(); drainPulses = 0; interrupts();
  lastDrainVolumeMl      = 0;
  targetDrainMl          = models[activeModelIndex].tankVolumeMl;
  supplyAtSessionStartMl = supplyTankRemainingMl;

  ResetFlowUi(gDrainUi);

  PumpEnabled = false;
  SetTargetSpeed(0);

  digitalWrite(FILL_RELAY, LOW);
  digitalWrite(DRAIN_RELAY, LOW);

  CurrentPage = DRAINPAGE;
  NxGotoPage(PAGE_DRAIN);

  NxSetText(NX_ACTIVE_MODEL_OBJ, models[activeModelIndex].name);
  NxSetPic("mMainPic", models[activeModelIndex].picIndex);

  NxSetVal(NX_TARGET_DRAIN_OBJ,    targetDrainMl);
  NxSetVal(NX_FLOW_RATE_DRAIN_OBJ, 0);
  NxSetVal(NX_VOLUME_DRAIN_OBJ,    0);
  NxSetVal(SLIDER_DRAIN,            models[activeModelIndex].drainSpeed);
  { char mlBuf[16]; FormatMlMin(models[activeModelIndex].drainSpeed, mlBuf, sizeof(mlBuf)); NxSetText(NX_DRAIN_SPD_LABEL, mlBuf); }

  NxSetVal(NX_HELI_BAR_OBJ, 100);
  NxSetText(NX_HELI_PCT_OBJ, "100%");
  int drainRef = (targetDrainMl > 0) ? targetDrainMl : models[activeModelIndex].tankVolumeMl;
  char buf[32];
  snprintf(buf, sizeof(buf), "0 / %dml", drainRef);
  NxSetText(NX_HELI_VOL_OBJ, buf);

  UpdateSupplyTankUI();
}

void RefreshDrainPage()
{
  // Resend all current drain page values to Nextion (called on page reload)
  NxSetText(NX_ACTIVE_MODEL_OBJ, models[activeModelIndex].name);
  NxSetPic("mMainPic", models[activeModelIndex].picIndex);

  int drainRef = (targetDrainMl > 0) ? targetDrainMl : models[activeModelIndex].tankVolumeMl;
  NxSetVal(NX_TARGET_DRAIN_OBJ,    drainRef);
  NxSetVal(NX_FLOW_RATE_DRAIN_OBJ, gDrainUi.lastSentFlow > 0 ? gDrainUi.lastSentFlow : 0);
  NxSetVal(NX_VOLUME_DRAIN_OBJ,    lastDrainVolumeMl);

  int heliPct = 100;
  if (drainRef > 0)
  {
    heliPct = 100 - (int)((100.0f * (float)lastDrainVolumeMl) / (float)drainRef + 0.5f);
    heliPct = constrain(heliPct, 0, 100);
  }
  NxSetVal(NX_HELI_BAR_OBJ, heliPct);
  char pctStr[10];
  snprintf(pctStr, sizeof(pctStr), "%d%%", heliPct);
  NxSetText(NX_HELI_PCT_OBJ, pctStr);
  char buf[32];
  snprintf(buf, sizeof(buf), "%d / %dml", lastDrainVolumeMl, drainRef);
  NxSetText(NX_HELI_VOL_OBJ, buf);

  UpdateSupplyTankUI();
  UpdateSupplyLowWarning();
}

void EnterSetupPage()
{
  CurrentPage = SETUPPAGE;
  NxGotoPage(PAGE_SETUP);
  ShowModelOnSetupPanel(activeModelIndex);
}

void EnterStationPage()
{
  fillCalActive  = false;
  drainCalActive = false;
  StopPumpInPlace();

  CurrentPage = STATIONPAGE;
  NxGotoPage(PAGE_STATION);
  UpdateStationPageValues();
  NxSetText(NX_ST_FILL_STATUS,  "Ready - Press Start to begin");
  NxSetText(NX_ST_DRAIN_STATUS, "Ready - Press Start to begin");
}

void BeginFill(int mlMin)
{
  if (lowBatteryLatched) return;

  if (models[activeModelIndex].hasTankSensor && IsTankFull())
  {
    StopPump();
    return;
  }

  // Initialise closed loop controller
  closedLoopTargetMlMin = (mlMin > 0) ? mlMin : models[activeModelIndex].pumpSpeed;
  closedLoopIntegral    = 0.0f;
  closedLoopActive      = true;

  // Start at 60% of estimated PWM — fast enough without overshoot
  int estimatedPwm = (mlPerMinPerPwm > 0.0f) ? MlMinToPwm(closedLoopTargetMlMin) : MIN_PWM;
  closedLoopCurrentPwm = MIN_PWM + (int)((estimatedPwm - MIN_PWM) * 0.6f);
  closedLoopPwmFloat   = (float)closedLoopCurrentPwm;  // initialise float accumulator to match

  Serial.println("--- BeginFill ---");
  Serial.print("mlMin="); Serial.println(closedLoopTargetMlMin);
  Serial.print("mlPerMinPerPwm="); Serial.println((double)mlPerMinPerPwm);
  Serial.print("initialPwm="); Serial.println(closedLoopCurrentPwm);

  PumpDriverEnable();
  PumpEnabled = true;

  NEXTION.flush();  // drain serial buffer before pump starts

  digitalWrite(FILL_RELAY, HIGH);
  digitalWrite(DRAIN_RELAY, LOW);

  SetTargetSpeed(+closedLoopCurrentPwm);
}

void BeginDrain(int pwm)
{
  if (lowBatteryLatched) return;

  pwm = constrain(pwm, 0, 255);
  if (pwm == 0) pwm = models[activeModelIndex].drainSpeed;
  if (pwm < MIN_PWM) pwm = MIN_PWM;

  PumpDriverEnable();
  PumpEnabled = true;

  NEXTION.flush();  // drain serial buffer before pump starts

  // Reset tank empty detection for new drain session
  drainPeakFlowMlMin = 0;
  tankEmptyCount     = 0;
  drainStartMs       = millis();

  digitalWrite(FILL_RELAY, LOW);
  digitalWrite(DRAIN_RELAY, HIGH);

  SetTargetSpeed(-pwm);
}

// ===============================
// AUTO DRAIN-THEN-FILL SEQUENCE
// ===============================
void BeginAutoSequenceDrain()
{
  // Set pending state immediately — pump starts after delay
  autoFillSequence     = AF_DRAIN_PENDING;
  autoFillTransitionMs = millis();

  // Set up drain page for auto sequence
  noInterrupts(); drainPulses = 0; interrupts();
  lastDrainVolumeMl      = 0;
  targetDrainMl          = 0;  // continuous drain until tank empty detected
  supplyAtSessionStartMl = supplyTankRemainingMl;

  ResetFlowUi(gDrainUi);

  PumpEnabled = false;
  SetTargetSpeed(0);
  SetPumpOutput(0);
  currentSpeedSigned = 0;
  digitalWrite(FILL_RELAY, LOW);
  digitalWrite(DRAIN_RELAY, LOW);

  CurrentPage = DRAINPAGE;
  NxGotoPage(PAGE_DRAIN);

  NxSetText(NX_ACTIVE_MODEL_OBJ, models[activeModelIndex].name);
  NxSetPic("mMainPic", models[activeModelIndex].picIndex);
  NxSetText(NX_STOP_REASON_OBJ, "Auto sequence: Draining...");
  stopReasonFlashActive = true;

  int drainRef = models[activeModelIndex].tankVolumeMl;
  NxSetVal(NX_TARGET_DRAIN_OBJ,    0);
  NxSetVal(NX_FLOW_RATE_DRAIN_OBJ, 0);
  NxSetVal(NX_VOLUME_DRAIN_OBJ,    0);
  // Do NOT set slider here — avoids any possible feedback loop
  NxSetVal(NX_HELI_BAR_OBJ, 100);
  NxSetText(NX_HELI_PCT_OBJ, "100%");
  char buf[32];
  snprintf(buf, sizeof(buf), "0 / %dml", drainRef);
  NxSetText(NX_HELI_VOL_OBJ, buf);
  UpdateSupplyTankUI();
}
// ===============================
// OVERFLOW PURGE
// ===============================
void BeginOverflowPurge()
{
  if (models[activeModelIndex].overflowPurgeSecs <= 0)
  {
    // No purge configured — show complete and stay on fill page
    autoFillSequence = AF_NONE;
    NxSetText(NX_STOP_REASON_OBJ, "Complete");
    stopReasonFlashActive = false;
    NxSetAttr("StopReason.pco", NX_COLOR_TXT_NORMAL);
    return;
  }

  autoFillSequence = AF_PURGING;
  purgeStartMs = millis();

  // Start drain briefly to purge overflow line
  noInterrupts(); drainPulses = 0; interrupts();
  ResetFlowUi(gDrainUi);

  digitalWrite(FILL_RELAY, LOW);
  digitalWrite(DRAIN_RELAY, HIGH);
  PumpDriverEnable();
  PumpEnabled = true;
  // Bypass ramp — start at full speed immediately for short purge duration
  int purgeSpd = models[activeModelIndex].drainSpeed;
  currentSpeedSigned = -purgeSpd;
  targetSpeedSigned  = -purgeSpd;
  SetPumpOutput(-purgeSpd);

  NxSetText(NX_STOP_REASON_OBJ, "Purging overflow line...");
  stopReasonFlashActive = true;
}

// ===============================
// CLOSED LOOP FLOW CONTROLLER UPDATE
// Called every 500ms from UpdateFillUiAndStops
// ===============================
static void UpdateClosedLoop(int actualFlowMlMin)
{
  if (!closedLoopActive || !PumpEnabled) return;
  if (autoFillSequence == AF_PURGING)   return;

  float error  = (float)(closedLoopTargetMlMin - actualFlowMlMin);

  // Only accumulate integral once flow has started — prevents windup during startup
  float newIntegral = closedLoopIntegral;
  if (actualFlowMlMin > 0)
    newIntegral = constrain(closedLoopIntegral + error, -CL_INTEGRAL_MAX, CL_INTEGRAL_MAX);

  float output = (CL_KP * error) + (CL_KI * newIntegral);

  // Limit ramp rate during zero flow startup — max 3 PWM units per cycle
  if (actualFlowMlMin == 0)
    output = constrain(output, -3.0f, 3.0f);

  // Use float PWM accumulator to avoid small changes being rounded to zero
  closedLoopPwmFloat += output;
  closedLoopPwmFloat = constrain(closedLoopPwmFloat, (float)MIN_PWM, (float)MAX_PWM);

  int newPwm = (int)(closedLoopPwmFloat + 0.5f);

  // Anti-windup — only keep integral update if PWM is not at its limits
  bool atLimit = (newPwm == MIN_PWM || newPwm == MAX_PWM);
  if (!atLimit)
    closedLoopIntegral = newIntegral;

  closedLoopCurrentPwm = newPwm;
  SetTargetSpeed(+closedLoopCurrentPwm);

  // Self-correct mlPerMinPerPwm when settled
  if (fabs(error) <= CL_SETTLED_TOLERANCE && !atLimit && closedLoopCurrentPwm > MIN_PWM)
  {
    closedLoopSettledCount++;
    if (closedLoopSettledCount >= CL_SETTLED_COUNT)
    {
      // Recalculate conversion factor from actual settled values
      int usablePwm = closedLoopCurrentPwm - MIN_PWM;
      if (usablePwm > 0)
      {
        float newFactor = (float)actualFlowMlMin / (float)usablePwm;
        // Smooth update — blend 20% new with 80% old
        mlPerMinPerPwm = mlPerMinPerPwm * 0.8f + newFactor * 0.2f;
        closedLoopSettledCount = 0;
        SaveStationToSD();
        Serial.print("CL: self-corrected mlPerMinPerPwm=");
        Serial.println((double)mlPerMinPerPwm);
      }
    }
  }
  else
  {
    closedLoopSettledCount = 0;
  }

  Serial.print("CL: target=");
  Serial.print(closedLoopTargetMlMin);
  Serial.print(" actual=");
  Serial.print(actualFlowMlMin);
  Serial.print(" err=");
  Serial.print((double)error);
  Serial.print(" pwm=");
  Serial.println(closedLoopCurrentPwm);
}

static void UpdateFillUiAndStops(uint32_t now)
{
  if (CurrentPage != FILLPAGE) return;

  noInterrupts();
  uint32_t p = fillPulses;
  interrupts();

  if (gFillUi.lastMs == 0)
  {
    gFillUi.lastMs     = now - 500;
    gFillUi.lastPulses = 0;
  }

  if (now - gFillUi.lastMs < 500) return;

  float dt = (now - gFillUi.lastMs) / 1000.0f;
  gFillUi.lastMs = now;

  uint32_t dp = p - gFillUi.lastPulses;
  gFillUi.lastPulses = p;

  float hz        = (dt > 0.0f) ? ((float)dp / dt) : 0.0f;
  float q_lpm     = hz / HZ_PER_LPM;
  int flow_ml_min_raw = (int)(q_lpm * 1000.0f + 0.5f);

  // Rolling average
  gFillUi.flowSamples[gFillUi.flowSampleIdx] = flow_ml_min_raw;
  gFillUi.flowSampleIdx = (gFillUi.flowSampleIdx + 1) % FLOW_SMOOTH_SAMPLES;
  int flowSum = 0;
  for (int i = 0; i < FLOW_SMOOTH_SAMPLES; i++) flowSum += gFillUi.flowSamples[i];
  int flow_ml_min = flowSum / FLOW_SMOOTH_SAMPLES;

  float liters  = (float)p / fillPulsesPerLiter;
  int volume_ml = (int)(liters * 1000.0f + 0.5f);

  lastFillVolumeMl = volume_ml;

  // Run closed loop controller with smoothed flow
  UpdateClosedLoop(flow_ml_min);

  int pct = 0;
  if (targetFillMl > 0)
  {
    pct = (int)((100.0f * (float)lastFillVolumeMl) / (float)targetFillMl + 0.5f);
    pct = constrain(pct, 0, 100);
  }

  if (flow_ml_min != gFillUi.lastSentFlow)
  {
    gFillUi.lastSentFlow = flow_ml_min;
    NxSetVal(NX_FLOW_RATE_FILL_OBJ, flow_ml_min);
  }

  if (volume_ml != gFillUi.lastSentVol)
  {
    gFillUi.lastSentVol = volume_ml;
    NxSetVal(NX_VOLUME_FILL_OBJ, volume_ml);
  }

  if (pct != gFillUi.lastSentPct)
  {
    gFillUi.lastSentPct = pct;
    NxSetVal(NX_PROGRESS_FILL_OBJ, pct);
    char pctStr[10];
    snprintf(pctStr, sizeof(pctStr), "%d%%", pct);
    NxSetText(NX_PERCENT_FILL_OBJ, pctStr);

    NxSetVal(NX_HELI_BAR_OBJ, pct);
    NxSetText(NX_HELI_PCT_OBJ, pctStr);
    char heliVolStr[32];
    snprintf(heliVolStr, sizeof(heliVolStr), "%d / %dml", volume_ml, targetFillMl);
    NxSetText(NX_HELI_VOL_OBJ, heliVolStr);

    supplyTankRemainingMl = supplyAtSessionStartMl - volume_ml;
    UpdateSupplyTankUI();
    UpdateSupplyLowWarning();
  }

  if (PumpEnabled && autoFillSequence != AF_PURGING)
  {
    if (targetFillMl > 0 && lastFillVolumeMl >= targetFillMl)
    {
      StopPumpInPlace();
      BeginOverflowPurge();
      return;
    }
    if (models[activeModelIndex].hasTankSensor && IsTankFull())
    {
      StopPumpInPlace();
      BeginOverflowPurge();
      return;
    }
  }
}

static void UpdateDrainUiAndStops(uint32_t now)
{
  if (CurrentPage != DRAINPAGE) return;

  noInterrupts();
  uint32_t p = drainPulses;
  interrupts();

  if (gDrainUi.lastMs == 0)
  {
    gDrainUi.lastMs     = now - 500;
    gDrainUi.lastPulses = 0;
  }

  if (now - gDrainUi.lastMs < 500) return;

  float dt = (now - gDrainUi.lastMs) / 1000.0f;
  gDrainUi.lastMs = now;

  uint32_t dp = p - gDrainUi.lastPulses;
  gDrainUi.lastPulses = p;

  float hz        = (dt > 0.0f) ? ((float)dp / dt) : 0.0f;
  float q_lpm     = hz / HZ_PER_LPM;
  int flow_ml_min_raw = (int)(q_lpm * 1000.0f + 0.5f);

  // Rolling average
  gDrainUi.flowSamples[gDrainUi.flowSampleIdx] = flow_ml_min_raw;
  gDrainUi.flowSampleIdx = (gDrainUi.flowSampleIdx + 1) % FLOW_SMOOTH_SAMPLES;
  int flowSum = 0;
  for (int i = 0; i < FLOW_SMOOTH_SAMPLES; i++) flowSum += gDrainUi.flowSamples[i];
  int flow_ml_min = flowSum / FLOW_SMOOTH_SAMPLES;

  float liters  = (float)p / drainPulsesPerLiter;
  int volume_ml = (int)(liters * 1000.0f + 0.5f);

  lastDrainVolumeMl = volume_ml;

  if (flow_ml_min != gDrainUi.lastSentFlow)
  {
    gDrainUi.lastSentFlow = flow_ml_min;
    NxSetVal(NX_FLOW_RATE_DRAIN_OBJ, flow_ml_min);
  }

  if (volume_ml != gDrainUi.lastSentVol)
  {
    gDrainUi.lastSentVol = volume_ml;
    NxSetVal(NX_VOLUME_DRAIN_OBJ, volume_ml);

    int drainRef = (targetDrainMl > 0) ? targetDrainMl : models[activeModelIndex].tankVolumeMl;
    int heliPct = 100;
    if (drainRef > 0)
    {
      heliPct = 100 - (int)((100.0f * (float)volume_ml) /
                (float)drainRef + 0.5f);
      heliPct = constrain(heliPct, 0, 100);
    }
    NxSetVal(NX_HELI_BAR_OBJ, heliPct);
    char heliPctStr[10];
    snprintf(heliPctStr, sizeof(heliPctStr), "%d%%", heliPct);
    NxSetText(NX_HELI_PCT_OBJ, heliPctStr);
    char heliVolStr[32];
   snprintf(heliVolStr, sizeof(heliVolStr), "%d / %dml",
             volume_ml, drainRef);
    NxSetText(NX_HELI_VOL_OBJ, heliVolStr);

    supplyTankRemainingMl = constrain(
      supplyAtSessionStartMl + volume_ml,
      0, supplyTankCapacityMl);
    UpdateSupplyTankUI();
    UpdateSupplyLowWarning();
  }

  if (PumpEnabled && targetDrainMl > 0 && lastDrainVolumeMl >= targetDrainMl)
  {
    NxSetText(NX_STOP_REASON_OBJ, "Stopped: Target volume reached");
    StopPumpInPlace();
    return;
  }

  // Tank empty detection — only when pump running for minimum time
  if (PumpEnabled && (millis() - drainStartMs) >= TANK_EMPTY_MIN_RUN_MS)
  {
    // Update peak flow
    if (flow_ml_min > drainPeakFlowMlMin)
    {
      drainPeakFlowMlMin = flow_ml_min;
      tankEmptyCount = 0;  // reset counter if flow rises
    }

    // Tank was already empty — peak flow never reached a meaningful level
    if (drainPeakFlowMlMin < TANK_EMPTY_MIN_PEAK_FLOW)
    {
      if (autoFillSequence == AF_DRAINING)
      {
        StopPumpInPlace();
        autoFillTransitionMs = millis();
        NxSetText(NX_STOP_REASON_OBJ, "Auto sequence: Tank empty - starting fill...");
      stopReasonFlashActive = true;
      }
      else
      {
        NxSetText(NX_STOP_REASON_OBJ, "Stopped: Tank already empty");
      stopReasonFlashActive = false;
      NxSetAttr("StopReason.pco", NX_COLOR_TXT_NORMAL);
        StopPumpInPlace();
      }
      return;
    }

    // Tank had fuel — check for flow drop indicating tank now empty
    int threshold = drainPeakFlowMlMin * (100 - tankEmptyFlowDropPct) / 100;
    if (flow_ml_min < threshold)
    {
      tankEmptyCount++;
      if (tankEmptyCount >= TANK_EMPTY_CONFIRM_COUNT)
      {
        if (autoFillSequence == AF_DRAINING)
        {
          // Auto sequence — transition to fill after pause
          StopPumpInPlace();
          autoFillTransitionMs = millis();
          NxSetText(NX_STOP_REASON_OBJ, "Auto sequence: Tank empty - starting fill...");
      stopReasonFlashActive = true;
        }
        else
        {
          NxSetText(NX_STOP_REASON_OBJ, "Stopped: Tank empty detected");
      stopReasonFlashActive = false;
      NxSetAttr("StopReason.pco", NX_COLOR_TXT_NORMAL);
          StopPumpInPlace();
        }
        return;
      }
    }
    else
    {
      tankEmptyCount = 0;
    }
  }
}

void UpdateFlowDisplaysAutoStopAndProgress()
{
  uint32_t now = millis();
  UpdateFillUiAndStops(now);
  UpdateDrainUiAndStops(now);

  // Auto drain-then-fill: delayed pump start
  if (autoFillSequence == AF_DRAIN_PENDING)
  {
    if (now - autoFillTransitionMs >= 500)  // 500ms delay for Nextion to settle
    {
      autoFillSequence = AF_DRAINING;
      BeginDrain(models[activeModelIndex].pumpSpeed);
    }
    return;
  }

  // Auto drain-then-fill transition: drain done, wait then start fill
  if (autoFillSequence == AF_DRAINING && !PumpEnabled && autoFillTransitionMs > 0)
  {
    if (now - autoFillTransitionMs >= AUTO_FILL_PAUSE_MS)
    {
      autoFillTransitionMs = 0;
      autoFillSequence = AF_FILLING;

      // Switch to fill page and start filling
      noInterrupts(); fillPulses = 0; interrupts();
      lastFillVolumeMl       = 0;
      supplyAtSessionStartMl = supplyTankRemainingMl;
      ResetFlowUi(gFillUi);

      CurrentPage = FILLPAGE;
      NxGotoPage(PAGE_FILL);

      NxSetText(NX_ACTIVE_MODEL_OBJ, models[activeModelIndex].name);
      NxSetPic("mMainPic", models[activeModelIndex].picIndex);
      NxSetVal(NX_TARGET_FILL_OBJ,    targetFillMl);
      NxSetVal(NX_FLOW_RATE_FILL_OBJ, 0);
      NxSetVal(NX_VOLUME_FILL_OBJ,    0);
      NxSetVal(NX_PROGRESS_FILL_OBJ,  0);
      NxSetText(NX_PERCENT_FILL_OBJ,  "0%");
      // Do NOT set slider here — avoids any possible echo from Nextion
      NxSetText(NX_STOP_REASON_OBJ,   "Auto sequence: Filling...");
      stopReasonFlashActive = true;
      NxSetVal(NX_HELI_BAR_OBJ, 0);
      NxSetText(NX_HELI_PCT_OBJ, "0%");
      char buf[32];
      snprintf(buf, sizeof(buf), "0 / %dml", targetFillMl);
      NxSetText(NX_HELI_VOL_OBJ, buf);
      UpdateSupplyTankUI();

      NEXTION.flush();  // discard anything Nextion sent during page transition
      BeginFill(models[activeModelIndex].pumpSpeed);
    }
  }

  // Auto fill complete — start purge if configured
  if (autoFillSequence == AF_FILLING && !PumpEnabled)
  {
    BeginOverflowPurge();
  }

  // Overflow purge timing
  if (autoFillSequence == AF_PURGING && PumpEnabled)
  {
    if ((millis() - purgeStartMs) >= (uint32_t)(models[activeModelIndex].overflowPurgeSecs * 1000))
    {
      StopPumpInPlace();
      autoFillSequence = AF_NONE;
      NxSetText(NX_STOP_REASON_OBJ, "Complete");
      stopReasonFlashActive = false;
      NxSetAttr("StopReason.pco", NX_COLOR_TXT_NORMAL);
    }
  }
}

// ===============================
// POWER UI + CUTOFF
// ===============================
static void UpdatePowerUIAndSafety()
{
  static uint32_t lastMs = 0;
  uint32_t now = millis();
  if (now - lastMs < 500) return;
  lastMs = now;

  float busV      = ina219.getBusVoltage_V();
  float shuntmV   = ina219.getShuntVoltage_mV();
  float packV_raw = busV + (shuntmV / 1000.0f);
  float currentA  = fabs(ina219.getCurrent_mA() / 1000.0f);

  if (!filterInit) { filteredPackV = packV_raw; filterInit = true; }
  else filteredPackV = filteredPackV + SAG_FILTER_ALPHA * (packV_raw - filteredPackV);

  DetectCellCount(filteredPackV);

  if (cellCount > 0 && CurrentPage != SETUPPAGE && CurrentPage != STATIONPAGE)
    NxSetText("tBattType", cellCount == 3 ? "3S Battery" : "2S Battery");

  float vPerCell_raw = (cellCount > 0) ? (packV_raw     / (float)cellCount) : packV_raw;
  float vPerCell_f   = (cellCount > 0) ? (filteredPackV / (float)cellCount) : filteredPackV;

  if (CurrentPage == LOWBATTPAGE)
  {
    char buf[40];
    snprintf(buf, sizeof(buf), "Pack voltage : %.2fV", (double)packV_raw);
    NxSetText(NX_LB_PACKV_TXT, buf);

    snprintf(buf, sizeof(buf), "Cell voltage : %.2fV", (double)vPerCell_raw);
    NxSetText(NX_LB_CELLV_TXT, buf);

    if (cellCount == 2)      NxSetText(NX_LB_CELLS_TXT, "2 cell");
    else if (cellCount == 3) NxSetText(NX_LB_CELLS_TXT, "3 cell");
    else                     NxSetText(NX_LB_CELLS_TXT, "?");

    if (lowBatteryLatched) return;
  }

  if (CurrentPage != LOWBATTPAGE && CurrentPage != SETUPPAGE && CurrentPage != STATIONPAGE)
  {
    int battPct = LiPoPctFromV(vPerCell_f);
    int curPct  = CurrentToPct(currentA);

    NxSetVal(NX_BATT_BAR_OBJ, battPct);
    char btxt[12];
    snprintf(btxt, sizeof(btxt), "%d%%", battPct);
    NxSetText(NX_BATT_PCT_OBJ, btxt);

    NxSetVal(NX_CUR_BAR_OBJ, curPct);
    char ctxt[16];
    snprintf(ctxt, sizeof(ctxt), "%.1fA", (double)currentA);
    NxSetText(NX_CUR_TXT_OBJ, ctxt);
  }

  if (!lowBatteryLatched && cellCount > 0)
  {
    if (vPerCell_raw <= CUTOFF_V_PER_CELL)
      { if (lowCount < 255) lowCount++; }
    else if (vPerCell_raw >= (CUTOFF_V_PER_CELL + SAG_HYST_PER_CELL))
      lowCount = 0;

    if (lowCount >= SAG_TRIP_COUNT)
      EnterLowBatteryPage(packV_raw, vPerCell_raw);
  }

  // Flash supply tank warning if low
  UpdateSupplyLowWarning();

  // Flash stop reason message if active
  UpdateStopReasonFlash();
}

// ===============================
// NEXTION RX
// ===============================
static bool ReadU32(uint32_t &out)
{
  if (NEXTION.available() < 4) return false;
  uint8_t b0 = NEXTION.read();
  uint8_t b1 = NEXTION.read();
  uint8_t b2 = NEXTION.read();
  uint8_t b3 = NEXTION.read();
  out = ((uint32_t)b0) | ((uint32_t)b1 << 8) | ((uint32_t)b2 << 16) | ((uint32_t)b3 << 24);
  return true;
}

void ProcessNextion()
{
  if (lowBatteryLatched)
  {
    uint32_t junk;
    while (ReadU32(junk)) {}
    return;
  }

  static bool waitingForSpeed        = false;
  static bool waitingForTargetFill   = false;
  static bool waitingForTargetDrain  = false;
  static bool waitingForTankVol      = false;
  static bool waitingForPumpSpd      = false;
  static bool waitingForSensor        = false;
  static bool waitingForOverflowPurge = false;
  static bool waitingForDrainSpeed    = false;
  static bool waitingForSupplyCap    = false;
  static bool waitingForSupplyLow    = false;
  static bool waitingForFlowDrop     = false;
  static bool waitingForFillCalVol   = false;
  static bool waitingForDrainCalVol  = false;

  uint32_t v;

  while (ReadU32(v))
  {
    // Always check waiting states first
    if (waitingForTankVol)
    {
      waitingForTankVol = false;
      models[previewModelIndex].tankVolumeMl = (int)constrain((int32_t)v, 0, 99999);
      CurrentPage = SETUPPAGE;
      NxGotoPage(PAGE_SETUP);
      ShowModelOnSetupPanel(previewModelIndex);
      continue;
    }

    if (waitingForPumpSpd)
    {
      waitingForPumpSpd = false;
      Serial.print("waitingForPumpSpd: v="); Serial.println(v);
      models[previewModelIndex].pumpSpeed = (int)constrain((int32_t)v, 0, 1000);  // ml/min
      CurrentPage = SETUPPAGE;
      NxGotoPage(PAGE_SETUP);
      ShowModelOnSetupPanel(previewModelIndex);
      continue;
    }

    if (waitingForSensor)
    {
      waitingForSensor = false;
      models[previewModelIndex].hasTankSensor = (v == 1);
      CurrentPage = SETUPPAGE;
      NxGotoPage(PAGE_SETUP);
      ShowModelOnSetupPanel(previewModelIndex);
      continue;
    }

    if (waitingForOverflowPurge)
    {
      waitingForOverflowPurge = false;
      models[previewModelIndex].overflowPurgeSecs = (int)constrain((int32_t)v, 0, 10);
      CurrentPage = SETUPPAGE;
      NxGotoPage(PAGE_SETUP);
      ShowModelOnSetupPanel(previewModelIndex);
      continue;
    }

    if (waitingForDrainSpeed)
    {
      waitingForDrainSpeed = false;
      models[previewModelIndex].drainSpeed = (int)constrain((int32_t)v, 0, 255);
      CurrentPage = SETUPPAGE;
      NxGotoPage(PAGE_SETUP);
      ShowModelOnSetupPanel(previewModelIndex);
      continue;
    }

    if (waitingForSupplyCap)
    {
      waitingForSupplyCap = false;
      // User types whole litres e.g. 20 for 20L
      int litres = (int)constrain((int32_t)v, 1, 100);
      supplyTankCapacityMl  = litres * 1000;
      supplyTankRemainingMl = constrain(supplyTankRemainingMl, 0, supplyTankCapacityMl);
      SaveStationToSD();
      UpdateStationPageValues();
      continue;
    }

    if (waitingForSupplyLow)
    {
      waitingForSupplyLow = false;
      int litres = (int)constrain((int32_t)v, 1, 99);
      supplyLowThresholdMl = litres * 1000;
      SaveStationToSD();
      UpdateStationPageValues();
      Serial.print("Supply low threshold: ");
      Serial.println(supplyLowThresholdMl);
      continue;
    }

    if (waitingForFlowDrop)
    {
      waitingForFlowDrop = false;
      tankEmptyFlowDropPct = (int)constrain((int32_t)v, 5, 90);
      SaveStationToSD();
      UpdateStationPageValues();
      Serial.print("Tank empty flow drop: ");
      Serial.print(tankEmptyFlowDropPct);
      Serial.println("%");
      continue;
    }

    if (waitingForFillCalVol)
    {
      waitingForFillCalVol = false;
      int actualMl = (int)constrain((int32_t)v, 1, 10000);

      noInterrupts();
      uint32_t calPulses = fillPulses;   // pulses were zeroed at cal start
      interrupts();

      if (actualMl > 0 && calPulses > 0)
      {
        fillPulsesPerLiter = (calPulses * 1000.0f) / (float)actualMl;

        // Calculate ml/min conversion factor from this calibration run
        float elapsedSecs = (millis() - fillCalStartMs) / 1000.0f;
        if (elapsedSecs > 0.5f)
        {
          float flowAtCalPwm = (actualMl / elapsedSecs) * 60.0f;  // ml/min at CAL_PWM
          int usablePwm = CAL_PWM - MIN_PWM;
          if (usablePwm > 0)
            mlPerMinPerPwm = flowAtCalPwm / (float)usablePwm;
          Serial.print("Fill cal: flow at CAL_PWM = ");
          Serial.print((double)flowAtCalPwm);
          Serial.print(" ml/min, mlPerMinPerPwm = ");
          Serial.println((double)mlPerMinPerPwm);
        }

        SaveStationToSD();
        char buf[64];
        snprintf(buf, sizeof(buf), "Done! Pulses/L: %.1f  (~%d ml/m)", (double)fillPulsesPerLiter, PwmToMlMin(CAL_PWM));
        NxSetText(NX_ST_FILL_STATUS, buf);
        UpdateStationPageValues();
      }
      else
      {
        NxSetText(NX_ST_FILL_STATUS, "Error - no pulses detected");
      }
      continue;
    }

    if (waitingForDrainCalVol)
    {
      waitingForDrainCalVol = false;
      int actualMl = (int)constrain((int32_t)v, 1, 10000);

      noInterrupts();
      uint32_t calPulses = drainPulses;  // pulses were zeroed at cal start
      interrupts();

      if (actualMl > 0 && calPulses > 0)
      {
        drainPulsesPerLiter = (calPulses * 1000.0f) / (float)actualMl;

        // Update ml/min conversion factor from drain calibration run
        float elapsedSecs = (millis() - drainCalStartMs) / 1000.0f;
        if (elapsedSecs > 0.5f)
        {
          float flowAtCalPwm = (actualMl / elapsedSecs) * 60.0f;  // ml/min at CAL_PWM
          int usablePwm = CAL_PWM - MIN_PWM;
          if (usablePwm > 0)
            mlPerMinPerPwm = flowAtCalPwm / (float)usablePwm;
          Serial.print("Drain cal: flow at CAL_PWM = ");
          Serial.print((double)flowAtCalPwm);
          Serial.print(" ml/min, mlPerMinPerPwm = ");
          Serial.println((double)mlPerMinPerPwm);
        }

        SaveStationToSD();
        char buf[64];
        snprintf(buf, sizeof(buf), "Done! Pulses/L: %.1f  (~%d ml/m)", (double)drainPulsesPerLiter, PwmToMlMin(CAL_PWM));
        NxSetText(NX_ST_DRAIN_STATUS, buf);
        UpdateStationPageValues();
      }
      else
      {
        NxSetText(NX_ST_DRAIN_STATUS, "Error - no pulses detected");
      }
      continue;
    }

    if (waitingForTargetFill)
    {
      waitingForTargetFill = false;
      targetFillMl = (int)constrain((int32_t)v, 0, 2000000);
      if (CurrentPage == FILLPAGE)
      {
        // Reset pulses so new target counts from zero
        noInterrupts(); fillPulses = 0; interrupts();
        lastFillVolumeMl = 0;
        supplyAtSessionStartMl = supplyTankRemainingMl;
        ResetFlowUi(gFillUi);

        NxSetVal(NX_TARGET_FILL_OBJ,   targetFillMl);
        NxSetVal(NX_PROGRESS_FILL_OBJ, 0);
        NxSetText(NX_PERCENT_FILL_OBJ, "0%");
        NxSetText(NX_STOP_REASON_OBJ,  "");
        stopReasonFlashActive = false;
        NxSetAttr("StopReason.pco", NX_COLOR_TXT_NORMAL);
        NxSetText(NX_ACTIVE_MODEL_OBJ, models[activeModelIndex].name);
        NxSetPic("mMainPic", models[activeModelIndex].picIndex);

        // Reset heli bar
        NxSetVal(NX_HELI_BAR_OBJ, 0);
        NxSetText(NX_HELI_PCT_OBJ, "0%");
        char buf[32];
        snprintf(buf, sizeof(buf), "0 / %dml", targetFillMl);
        NxSetText(NX_HELI_VOL_OBJ, buf);
        UpdateSupplyTankUI();
      }
      continue;
    }

    if (waitingForTargetDrain)
    {
      waitingForTargetDrain = false;
      targetDrainMl = (int)constrain((int32_t)v, 0, 2000000);
      if (CurrentPage == DRAINPAGE)
      {
        // Reset pulses so new target counts from zero
        noInterrupts(); drainPulses = 0; interrupts();
        lastDrainVolumeMl = 0;
        supplyAtSessionStartMl = supplyTankRemainingMl;
        ResetFlowUi(gDrainUi);

        NxSetVal(NX_TARGET_DRAIN_OBJ, targetDrainMl);
        NxSetText(NX_ACTIVE_MODEL_OBJ, models[activeModelIndex].name);
        NxSetPic("mMainPic", models[activeModelIndex].picIndex);

        // Reset heli bar to full for new drain segment
        NxSetVal(NX_HELI_BAR_OBJ, 100);
        NxSetText(NX_HELI_PCT_OBJ, "100%");
        char buf[32];
        snprintf(buf, sizeof(buf), "0 / %dml", targetDrainMl);
        NxSetText(NX_HELI_VOL_OBJ, buf);
        UpdateSupplyTankUI();
      }
      continue;
    }

    if (waitingForSpeed)
    {
      waitingForSpeed = false;
      int mlMin = (int)constrain((int32_t)v, 0, 2000);

      // Block speed commands during auto drain-then-fill sequence
      if (autoFillSequence != AF_NONE)
      {
        continue;
      }

      if (CurrentPage == FILLPAGE)
      {
        if (!PumpEnabled && mlMin > 0)
        {
          BeginFill(mlMin);
        }
        else if (PumpEnabled && closedLoopActive)
        {
          // Update closed loop setpoint
          closedLoopTargetMlMin = mlMin;
          closedLoopIntegral    = 0.0f;  // reset integral on setpoint change
        }
        // Update ml/min label
        char mlBuf[16];
        snprintf(mlBuf, sizeof(mlBuf), "%d ml/m", mlMin);
        NxSetText(NX_FILL_SPD_LABEL, mlBuf);
      }
      else if (CurrentPage == DRAINPAGE)
      {
        int spd = ApplyMinimumFloor(v);
        if (!PumpEnabled && spd > 0) BeginDrain(spd);
        else if (PumpEnabled)        SetTargetSpeed(-spd);
        // Update ml/min label
        char mlBuf[16];
        FormatMlMin(spd, mlBuf, sizeof(mlBuf));
        NxSetText(NX_DRAIN_SPD_LABEL, mlBuf);
      }
      continue;
    }

    // Page report codes
    if (v == NX_PAGE_REPORT_MAIN)   { CurrentPage = MAINPAGE;    continue; }
    if (v == NX_PAGE_REPORT_FILL)
    {
      CurrentPage = FILLPAGE;
      RefreshFillPage();
      continue;
    }
    if (v == NX_PAGE_REPORT_DRAIN)
    {
      CurrentPage = DRAINPAGE;
      RefreshDrainPage();
      continue;
    }
    if (v == NX_PAGE_REPORT_LOWBAT) { CurrentPage = LOWBATTPAGE; continue; }

    // Standard commands
    if (v == 1) { EnterFillPage();  continue; }
    if (v == 2) { EnterDrainPage(); continue; }
    if (v == 3)
    {
      autoFillSequence      = AF_NONE;
      autoFillTransitionMs  = 0;
      drainPeakFlowMlMin    = 0;
      tankEmptyCount        = 0;
      purgeStartMs          = 0;
      stopReasonFlashActive = false;
      if ((CurrentPage == DRAINPAGE || CurrentPage == FILLPAGE) && PumpEnabled)
        StopPumpInPlace();
      else
        StopPump();
      continue;
    }

    if (v == 11) { if (CurrentPage == FILLPAGE)  BeginFill(models[activeModelIndex].pumpSpeed);  continue; }
    if (v == 12) { if (CurrentPage == DRAINPAGE) BeginDrain(models[activeModelIndex].drainSpeed); continue; }

    if (v == 1000) { waitingForSpeed      = true; continue; }
    if (v == 2000) { waitingForTargetFill = true; continue; }
    if (v == 3000) { waitingForTargetDrain = true; continue; }

    // Setup page commands
    if (v == NX_CMD_SETUP_PAGE) { EnterSetupPage();         continue; }
    if (v == NX_CMD_MODEL1)     { ShowModelOnSetupPanel(0); continue; }
    if (v == NX_CMD_MODEL2)     { ShowModelOnSetupPanel(1); continue; }
    if (v == NX_CMD_MODEL3)     { ShowModelOnSetupPanel(2); continue; }
    if (v == NX_CMD_MODEL4)     { ShowModelOnSetupPanel(3); continue; }

    if (v == NX_CMD_SELECT)
    {
      activeModelIndex = previewModelIndex;
      ApplyActiveModel();
      Serial.print("Selected: ");
      Serial.println(models[activeModelIndex].name);
      continue;
    }

    if (v == NX_CMD_SAVE)  { SaveModelsToSD(); continue; }

    if (v == NX_CMD_BACK_SETUP)
    {
      activeModelIndex = previewModelIndex;
      ApplyActiveModel();
      SaveModelsToSD();

      CurrentPage = MAINPAGE;
      NxGotoPage(PAGE_MAIN);
      NxSetText("tVersion", FW_VERSION);
      NxSetText("tBattType", cellCount == 3 ? "3S Battery" : "2S Battery");
      UpdateMainPageModel();
      UpdateSupplyTankUI();
      continue;
    }

    if (v == NX_CMD_STATION) { EnterStationPage(); continue; }

    if (v == 6001) { waitingForTankVol  = true; continue; }
    if (v == 6002) { waitingForPumpSpd  = true; continue; }
    if (v == 6003) { waitingForSensor        = true; continue; }
    if (v == 6004) { waitingForOverflowPurge = true; continue; }
    if (v == 6005) { waitingForDrainSpeed    = true; continue; }

    // Station page commands
    if (v == NX_CMD_BACK_STATION)
    {
      fillCalActive  = false;
      drainCalActive = false;
      StopPumpInPlace();

      CurrentPage = SETUPPAGE;
      NxGotoPage(PAGE_SETUP);
      ShowModelOnSetupPanel(activeModelIndex);
      continue;
    }

    if (v == NX_CMD_RESET_FULL)
    {
      supplyTankRemainingMl = supplyTankCapacityMl;
      SaveStationToSD();
      UpdateStationPageValues();
      Serial.println("Supply tank reset to full");
      continue;
    }

    if (v == NX_CMD_SET_CAP) { waitingForSupplyCap = true; continue; }
    if (v == NX_CMD_SET_LOW)       { waitingForSupplyLow  = true; continue; }
    if (v == NX_CMD_SET_FLOW_DROP) { waitingForFlowDrop   = true; continue; }

    if (v == NX_CMD_FILL_CAL_START)
    {
      if (drainCalActive)
      {
        NxSetText(NX_ST_FILL_STATUS, "Stop drain cal first");
        continue;
      }
      noInterrupts(); fillPulses = 0; interrupts();
      fillCalActive  = true;
      fillCalStartMs = millis();

      PumpDriverEnable();
      PumpEnabled = true;
      digitalWrite(FILL_RELAY, HIGH);
      digitalWrite(DRAIN_RELAY, LOW);
      SetTargetSpeed(+CAL_PWM);

      NxSetText(NX_ST_FILL_STATUS, "Running - collect fuel then press Stop");
      Serial.println("Fill cal started");
      continue;
    }

    if (v == NX_CMD_FILL_CAL_STOP)
    {
      if (!fillCalActive) continue;
      StopPumpInPlace();
      fillCalActive = false;

      noInterrupts();
      uint32_t totalPulses = fillPulses;
      interrupts();

      char buf[64];
      snprintf(buf, sizeof(buf), "Stopped. %lu pulses - enter actual vol", (unsigned long)totalPulses);
      NxSetText(NX_ST_FILL_STATUS, buf);
      Serial.print("Fill cal stopped. Pulses: ");
      Serial.println(totalPulses);
      continue;
    }

    if (v == NX_CMD_DRAIN_CAL_START)
    {
      if (fillCalActive)
      {
        NxSetText(NX_ST_DRAIN_STATUS, "Stop fill cal first");
        continue;
      }
      noInterrupts(); drainPulses = 0; interrupts();
      drainCalActive  = true;
      drainCalStartMs = millis();

      PumpDriverEnable();
      PumpEnabled = true;
      digitalWrite(FILL_RELAY, LOW);
      digitalWrite(DRAIN_RELAY, HIGH);
      SetTargetSpeed(-CAL_PWM);

      NxSetText(NX_ST_DRAIN_STATUS, "Running - collect fuel then press Stop");
      Serial.println("Drain cal started");
      continue;
    }

    if (v == NX_CMD_DRAIN_CAL_STOP)
    {
      if (!drainCalActive) continue;
      StopPumpInPlace();
      drainCalActive = false;

      noInterrupts();
      uint32_t totalPulses = drainPulses;
      interrupts();

      char buf[64];
      snprintf(buf, sizeof(buf), "Stopped. %lu pulses - enter actual vol", (unsigned long)totalPulses);
      NxSetText(NX_ST_DRAIN_STATUS, buf);
      Serial.print("Drain cal stopped. Pulses: ");
      Serial.println(totalPulses);
      continue;
    }

    if (v == NX_CMD_FILL_CAL_VOL)  { waitingForFillCalVol  = true; continue; }
    if (v == NX_CMD_DRAIN_CAL_VOL) { waitingForDrainCalVol = true; continue; }
  }
}

// ===============================
// SETUP / LOOP
// ===============================
void setup()
{
  Serial.begin(115200);
  while (!Serial && millis() < 3000);  // wait up to 3s for serial monitor

  Serial.print("\nFuel Pump Controller ");
  Serial.print(FW_VERSION);
  Serial.print(" | Built ");
  Serial.print(FW_BUILD_DATE);
  Serial.print(" ");
  Serial.println(FW_BUILD_TIME);

  pinMode(FILL_RELAY, OUTPUT);
  pinMode(DRAIN_RELAY, OUTPUT);
  digitalWrite(FILL_RELAY, LOW);
  digitalWrite(DRAIN_RELAY, LOW);

  pinMode(TANK_FULL_PIN, INPUT_PULLUP);
  pinMode(PUMP_REN, OUTPUT);
  pinMode(PUMP_LEN, OUTPUT);
  pinMode(PUMP_RPWM, OUTPUT);
  pinMode(PUMP_LPWM, OUTPUT);

  analogWriteResolution(8);
  analogWriteFrequency(PUMP_RPWM, 2000);
  analogWriteFrequency(PUMP_LPWM, 2000);

  PumpDriverDisable();
  InitFlowSensors();

  Wire.begin();
  ina219.begin();

  LoadModelsFromSD();
  LoadStationFromSD();
  ApplyActiveModel();

  NEXTION.begin(NEXTION_BAUD);

  PumpEnabled        = false;
  currentSpeedSigned = 0;
  targetSpeedSigned  = 0;
  SetPumpOutput(0);

  lowBatteryLatched = false;
  CurrentPage = MAINPAGE;
  NxGotoPage(PAGE_MAIN);
  NxSetText("tVersion", FW_VERSION);
  UpdateMainPageModel();
  UpdateSupplyTankUI();

  NxSetVal(NX_VOLUME_MAIN_OBJ, 0);

  ResetFlowUi(gFillUi);
  ResetFlowUi(gDrainUi);

  filterInit = false;
  lowCount   = 0;
}

void loop()
{
  ProcessNextion();
  UpdateRamp();
  UpdateFlowDisplaysAutoStopAndProgress();
  UpdatePowerUIAndSafety();
}
