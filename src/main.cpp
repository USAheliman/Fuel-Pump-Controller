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
#define STATIONPAGE 9

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
#define NX_CMD_FILL_CAL_VOL    7020
#define NX_CMD_DRAIN_CAL_VOL   7021

// Calibration pump speed (fixed)
#define CAL_PWM 150

// ===============================
// MODEL CONFIGURATION
// ===============================
#define NUM_MODELS 4

struct ModelConfig
{
  char    name[24];
  int     tankVolumeMl;
  bool    hasTankSensor;
  int     pumpSpeed;
  uint8_t picIndex;
};

ModelConfig models[NUM_MODELS] = {
  { "BO 105",   2000, true,  127, 3 },
  { "Whiplash", 1000, false, 127, 5 },
  { "Alouette", 1500, true,  127, 2 },
  { "EC 145",   3000, true,  127, 4 },
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

// ===============================
// SESSION STATE
// ===============================
int lastFillVolumeMl  = 0;
int lastDrainVolumeMl = 0;
int targetFillMl      = 0;
int targetDrainMl     = 0;

// ===============================
// MINIMUM SPEED FLOOR
// ===============================
#define MIN_PWM 140
#define MAX_PWM 255

int ApplyMinimumFloor(uint32_t raw)
{
  int v = constrain((int)raw, 0, 255);
  if (v == 0) return 0;
  return map(v, 1, 255, MIN_PWM, MAX_PWM);
}

// ===============================
// RAMP SETTINGS
// ===============================
#define RAMP_INTERVAL_MS 10
#define RAMP_STEP        3

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
#define FLOW_SMOOTH_SAMPLES 6

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
void EnterSetupPage();
void EnterStationPage();
void BeginFill(int pwm);
void BeginDrain(int pwm);
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

  // Apply low warning colour immediately on update
  bool isLow = (supplyTankRemainingMl <= supplyLowThresholdMl);
  NxSetAttr("tSupVol.pco", isLow ? NX_COLOR_RED : NX_COLOR_TXT_NORMAL);
  if (!isLow)
    NxSetAttr("ProgSup.pco", NX_COLOR_GREEN_BAR);
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
    f.println(models[i].picIndex);
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

    if (c1 < 0 || c2 < 0 || c3 < 0 || c4 < 0) continue;

    String name = line.substring(0, c1);
    name.toCharArray(models[i].name, sizeof(models[i].name));
    models[i].tankVolumeMl  = line.substring(c1+1, c2).toInt();
    models[i].hasTankSensor = line.substring(c2+1, c3).toInt() == 1;
    models[i].pumpSpeed     = line.substring(c3+1, c4).toInt();
    models[i].picIndex      = (uint8_t)line.substring(c4+1).toInt();
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

  NxSetText(NX_S_NAME,    m.name);
  NxSetVal(NX_S_TANK_VOL, m.tankVolumeMl);
  NxSetText(NX_S_SENSOR,  m.hasTankSensor ? "YES" : "NO");
  NxSetVal(NX_S_PUMP_SPD, m.pumpSpeed);
  NxSetPic(NX_S_PIC,      m.picIndex);
}

// ===============================
// PUMP STOP IN PLACE
// ===============================
static void StopPumpInPlace()
{
  PumpEnabled = false;
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
}

void EnterFillPage()
{
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
  NxSetText(NX_STOP_REASON_OBJ,   "");

  NxSetVal(NX_HELI_BAR_OBJ, 0);
  NxSetText(NX_HELI_PCT_OBJ, "0%");
  char buf[32];
  snprintf(buf, sizeof(buf), "0 / %dml", targetFillMl);
  NxSetText(NX_HELI_VOL_OBJ, buf);

  UpdateSupplyTankUI();
}

void EnterDrainPage()
{
  noInterrupts(); drainPulses = 0; interrupts();
  lastDrainVolumeMl      = 0;
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
  NxSetVal(SLIDER_DRAIN,            models[activeModelIndex].pumpSpeed);

  NxSetVal(NX_HELI_BAR_OBJ, 100);
  NxSetText(NX_HELI_PCT_OBJ, "100%");
  char buf[32];
  snprintf(buf, sizeof(buf), "0 / %dml", models[activeModelIndex].tankVolumeMl);
  NxSetText(NX_HELI_VOL_OBJ, buf);

  UpdateSupplyTankUI();
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

void BeginFill(int pwm)
{
  if (lowBatteryLatched) return;

  if (models[activeModelIndex].hasTankSensor && IsTankFull())
  {
    StopPump();
    return;
  }

  pwm = constrain(pwm, 0, 255);
  if (pwm == 0) pwm = models[activeModelIndex].pumpSpeed;
  if (pwm < MIN_PWM) pwm = MIN_PWM;

  PumpDriverEnable();
  PumpEnabled = true;

  digitalWrite(FILL_RELAY, HIGH);
  digitalWrite(DRAIN_RELAY, LOW);

  NxSetVal(SLIDER_FILL, pwm);
  SetTargetSpeed(+pwm);
}

void BeginDrain(int pwm)
{
  if (lowBatteryLatched) return;

  pwm = constrain(pwm, 0, 255);
  if (pwm == 0) pwm = models[activeModelIndex].pumpSpeed;
  if (pwm < MIN_PWM) pwm = MIN_PWM;

  PumpDriverEnable();
  PumpEnabled = true;

  digitalWrite(FILL_RELAY, LOW);
  digitalWrite(DRAIN_RELAY, HIGH);

  NxSetVal(SLIDER_DRAIN, pwm);
  SetTargetSpeed(-pwm);
}

// ===============================
// FLOW + VOLUME + AUTO-STOP
// ===============================
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
  }

  if (PumpEnabled)
  {
    if (targetFillMl > 0 && lastFillVolumeMl >= targetFillMl)
    {
      NxSetText(NX_STOP_REASON_OBJ, "Stopped: Target volume reached");
      StopPumpInPlace();
      return;
    }
    if (models[activeModelIndex].hasTankSensor && IsTankFull())
    {
      NxSetText(NX_STOP_REASON_OBJ, "Stopped: Tank full sensor");
      StopPumpInPlace();
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
  }

  if (PumpEnabled && targetDrainMl > 0 && lastDrainVolumeMl >= targetDrainMl)
  {
    NxSetText(NX_STOP_REASON_OBJ, "Stopped: Target volume reached");
    StopPumpInPlace();
    return;
  }
}

void UpdateFlowDisplaysAutoStopAndProgress()
{
  uint32_t now = millis();
  UpdateFillUiAndStops(now);
  UpdateDrainUiAndStops(now);
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
  static bool waitingForSupplyCap    = false;
  static bool waitingForSupplyLow    = false;
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
      models[previewModelIndex].pumpSpeed = (int)constrain((int32_t)v, 0, 255);
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
        SaveStationToSD();
        char buf[64];
        snprintf(buf, sizeof(buf), "Done! New Pulses/L: %.1f", (double)fillPulsesPerLiter);
        NxSetText(NX_ST_FILL_STATUS, buf);
        UpdateStationPageValues();
        Serial.print("Fill cal done. Pulses/L: ");
        Serial.println((double)fillPulsesPerLiter);
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
        SaveStationToSD();
        char buf[64];
        snprintf(buf, sizeof(buf), "Done! New Pulses/L: %.1f", (double)drainPulsesPerLiter);
        NxSetText(NX_ST_DRAIN_STATUS, buf);
        UpdateStationPageValues();
        Serial.print("Drain cal done. Pulses/L: ");
        Serial.println((double)drainPulsesPerLiter);
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
      int spd = ApplyMinimumFloor(v);

      if (CurrentPage == FILLPAGE)
      {
        if (!PumpEnabled && spd > 0) BeginFill(spd);
        else if (PumpEnabled)        SetTargetSpeed(+spd);
      }
      else if (CurrentPage == DRAINPAGE)
      {
        if (!PumpEnabled && spd > 0) BeginDrain(spd);
        else if (PumpEnabled)        SetTargetSpeed(-spd);
      }
      continue;
    }

    // Page report codes
    if (v == NX_PAGE_REPORT_MAIN)   { CurrentPage = MAINPAGE;    continue; }
    if (v == NX_PAGE_REPORT_FILL)   { CurrentPage = FILLPAGE;    continue; }
    if (v == NX_PAGE_REPORT_DRAIN)  { CurrentPage = DRAINPAGE;   continue; }
    if (v == NX_PAGE_REPORT_LOWBAT) { CurrentPage = LOWBATTPAGE; continue; }

    // Standard commands
    if (v == 1) { EnterFillPage();  continue; }
    if (v == 2) { EnterDrainPage(); continue; }
    if (v == 3)
    {
      if (CurrentPage == DRAINPAGE && PumpEnabled)
        StopPumpInPlace();
      else
        StopPump();
      continue;
    }

    if (v == 11) { if (CurrentPage == FILLPAGE)  BeginFill(MIN_PWM);  continue; }
    if (v == 12) { if (CurrentPage == DRAINPAGE) BeginDrain(MIN_PWM); continue; }

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

    if (v == 6001) { waitingForTankVol = true; continue; }
    if (v == 6002) { waitingForPumpSpd = true; continue; }

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
    if (v == NX_CMD_SET_LOW) { waitingForSupplyLow = true; continue; }

    if (v == NX_CMD_FILL_CAL_START)
    {
      if (drainCalActive)
      {
        NxSetText(NX_ST_FILL_STATUS, "Stop drain cal first");
        continue;
      }
      noInterrupts(); fillPulses = 0; interrupts();
      fillCalActive = true;

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
      drainCalActive = true;

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
  delay(200);

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
