#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_INA219.h>

// ===============================
// PIN DEFINITIONS
// ===============================
#define FILL_RELAY 2
#define DRAIN_RELAY 3

#define FILL_FLOW_PIN  4
#define DRAIN_FLOW_PIN 5

// Tank full sensor (digital)
#define TANK_FULL_PIN 6
// Set to 1 if your sensor is active HIGH instead of active LOW
#define TANK_FULL_ACTIVE_HIGH 1

// ===============================
// PUMP DRIVER (BTS7960)
// ===============================
// RPWM/LPWM must be PWM-capable pins.
#define PUMP_RPWM 7     // forward PWM
#define PUMP_LPWM 10    // reverse PWM (extra PWM pin)
#define PUMP_REN  8     // enable
#define PUMP_LEN  9     // enable

// ===============================
// NEXTION
// ===============================
#define NEXTION Serial1
#define NEXTION_BAUD 921600

// ===============================
// PAGE NAMES (MUST match HMI exactly, no spaces)
// ===============================
#define PAGE_MAIN     "MainPage"
#define PAGE_FILL     "FillPage"
#define PAGE_DRAIN    "DrainPage"
#define PAGE_LOWBATT  "LowBattPage"

#define SLIDER_FILL  "PumpSpeedFill"
#define SLIDER_DRAIN "PumpSpeedDrain"

// Fill page display objects (Number/Text components)
#define NX_FLOW_RATE_FILL_OBJ "FlowFill"
#define NX_VOLUME_FILL_OBJ    "VolFill"
#define NX_TARGET_FILL_OBJ    "TgtFill"
#define NX_PROGRESS_FILL_OBJ  "ProgFill"
#define NX_PERCENT_FILL_OBJ   "PctFill"

// Drain page display objects (Number components)
#define NX_FLOW_RATE_DRAIN_OBJ "FlowDrain"
#define NX_VOLUME_DRAIN_OBJ    "VolDrain"
#define NX_TARGET_DRAIN_OBJ    "TgtDrain"

// Main page display object (Number component)
#define NX_VOLUME_MAIN_OBJ     "VolMain"

// Battery / Current UI objects (MUST exist on Main/Fill/Drain pages)
#define NX_BATT_BAR_OBJ  "BatBar"
#define NX_BATT_PCT_OBJ  "BatPct"
#define NX_CUR_BAR_OBJ   "CurBar"
#define NX_CUR_TXT_OBJ   "CurTxt"

// Low battery page objects (Text components) on LowBattPage
#define NX_LB_PACKV_TXT   "LbPackV"
#define NX_LB_CELLV_TXT   "LbCellV"
#define NX_LB_CELLS_TXT   "LbCells"

// ===============================
// STATE
// ===============================
#define MAINPAGE    0
#define FILLPAGE    1
#define DRAINPAGE   2
#define LOWBATTPAGE 3

uint8_t CurrentPage = MAINPAGE;
bool PumpEnabled = false;

// Last session volumes (for main page)
int lastFillVolumeMl  = 0;
int lastDrainVolumeMl = 0;

// Target volumes (mL). 0 = disabled.
int targetFillMl  = 0;
int targetDrainMl = 0;

// ===============================
// MINIMUM SPEED FLOOR (kept from your L298N tuning)
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
// FLOW SENSOR CALIBRATION (SEN-HZ06K)
// ===============================
#define HZ_PER_LPM 57.0f
#define PULSES_PER_LITER 1696.0f   // calibrate if needed

volatile uint32_t fillPulses = 0;
volatile uint32_t drainPulses = 0;

// ===============================
// INA219 / BATTERY
// ===============================
Adafruit_INA219 ina219;

#define CUTOFF_V_PER_CELL 3.82f
#define MAX_CURRENT_A     5.0f

int cellCount = 0;
bool lowBatteryLatched = false;

// ===============================
// VOLTAGE SAG FILTER SETTINGS
// ===============================
#define SAG_FILTER_ALPHA  0.20f
#define SAG_TRIP_COUNT    4
#define SAG_HYST_PER_CELL 0.05f

static float filteredPackV = 0.0f;
static bool  filterInit = false;
static uint8_t lowCount = 0;

// ===============================
// FORWARD DECLARATIONS (needed in .cpp)
// ===============================
static void UpdatePowerUIAndSafety();
void ProcessNextion();
void SetPumpOutput(int signedSpeed);
void SetTargetSpeed(int signedSpeed);
void UpdateRamp();
void StopPump();
void EnterFillPage();
void EnterDrainPage();
void BeginFill(int pwm);
void BeginDrain(int pwm);
void EnterLowBatteryPage(float packV, float vPerCell);
void UpdateFlowDisplaysAutoStopAndProgress();
void InitFlowSensors();

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

// ===============================
// FLOW ISRs
// ===============================
void FillFlowISR()  { fillPulses++; }
void DrainFlowISR() { drainPulses++; }

void InitFlowSensors()
{
  pinMode(FILL_FLOW_PIN, INPUT_PULLUP);
  pinMode(DRAIN_FLOW_PIN, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(FILL_FLOW_PIN),  FillFlowISR,  RISING);
  attachInterrupt(digitalPinToInterrupt(DRAIN_FLOW_PIN), DrainFlowISR, RISING);
}

// ===============================
// PUMP DRIVER ENABLE/DISABLE (failsafe)
// ===============================
static inline void PumpDriverEnable()
{
  digitalWrite(PUMP_REN, HIGH);
  digitalWrite(PUMP_LEN, HIGH);
}

static inline void PumpDriverDisable()
{
  // Ensure outputs are off before disabling
  analogWrite(PUMP_RPWM, 0);
  analogWrite(PUMP_LPWM, 0);
  digitalWrite(PUMP_REN, LOW);
  digitalWrite(PUMP_LEN, LOW);
}

// ===============================
// PUMP OUTPUT (raw set, no ramp)
// ===============================
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

// Target speed setter
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

  // Prevent instant direction flip; ramp down to 0 first
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

// Tank full sensor read (with simple debounce using consecutive samples)
static bool IsTankFull()
{
  int raw = digitalRead(TANK_FULL_PIN);
  bool active = (TANK_FULL_ACTIVE_HIGH ? (raw == HIGH) : (raw == LOW));

  // Debounce: require 2 consecutive "active" samples (Fill updates run ~500ms)
  static uint8_t activeCount = 0;
  if (active)
  {
    if (activeCount < 255) activeCount++;
  }
  else
  {
    activeCount = 0;
  }
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
    100,   95,    90,    85,    80,    75,    70,    60,
    50,    45,    40,    30,    20,    15,    10,     5,     0
  };
  const int N = (int)(sizeof(P) / sizeof(P[0]));

  if (vPerCell >= V[0]) return 100;
  if (vPerCell <= V[N - 1]) return 0;

  for (int i = 0; i < N - 1; i++)
  {
    if (vPerCell <= V[i] && vPerCell >= V[i + 1])
    {
      float t = (vPerCell - V[i + 1]) / (V[i] - V[i + 1]);
      float pct = (float)P[i + 1] + t * (float)(P[i] - P[i + 1]);
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

// ===============================
// LOW BATTERY LATCH ACTION
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

  snprintf(buf, sizeof(buf), "Pack: %.2f V", (double)packV);
  NxSetText(NX_LB_PACKV_TXT, buf);

  snprintf(buf, sizeof(buf), "Cell: %.2f V", (double)vPerCell);
  NxSetText(NX_LB_CELLV_TXT, buf);

  if (cellCount == 2) NxSetText(NX_LB_CELLS_TXT, "Cells: 2S");
  else if (cellCount == 3) NxSetText(NX_LB_CELLS_TXT, "Cells: 3S");
  else NxSetText(NX_LB_CELLS_TXT, "Cells: ?");

  NxSetVal(SLIDER_FILL, 0);
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

  CurrentPage = MAINPAGE;
  NxGotoPage(PAGE_MAIN);

  NxSetVal(SLIDER_FILL, 0);
  NxSetVal(SLIDER_DRAIN, 0);

  int showVol = 0;
  if (wasPage == FILLPAGE)  showVol = lastFillVolumeMl;
  if (wasPage == DRAINPAGE) showVol = lastDrainVolumeMl;

  NxSetVal(NX_VOLUME_MAIN_OBJ, showVol);
}

void EnterFillPage()
{
  noInterrupts(); fillPulses = 0; interrupts();
  lastFillVolumeMl = 0;

  PumpEnabled = false;
  SetTargetSpeed(0);

  digitalWrite(FILL_RELAY, LOW);
  digitalWrite(DRAIN_RELAY, LOW);

  CurrentPage = FILLPAGE;
  NxGotoPage(PAGE_FILL);

  NxSetVal(NX_TARGET_FILL_OBJ, targetFillMl);
  NxSetVal(NX_FLOW_RATE_FILL_OBJ, 0);
  NxSetVal(NX_VOLUME_FILL_OBJ, 0);
  NxSetVal(NX_PROGRESS_FILL_OBJ, 0);
  NxSetText(NX_PERCENT_FILL_OBJ, "0%");
  NxSetVal(SLIDER_FILL, 0);
}

void EnterDrainPage()
{
  noInterrupts(); drainPulses = 0; interrupts();
  lastDrainVolumeMl = 0;

  PumpEnabled = false;
  SetTargetSpeed(0);

  digitalWrite(FILL_RELAY, LOW);
  digitalWrite(DRAIN_RELAY, LOW);

  CurrentPage = DRAINPAGE;
  NxGotoPage(PAGE_DRAIN);

  NxSetVal(NX_TARGET_DRAIN_OBJ, targetDrainMl);
  NxSetVal(NX_FLOW_RATE_DRAIN_OBJ, 0);
  NxSetVal(NX_VOLUME_DRAIN_OBJ, 0);
  NxSetVal(SLIDER_DRAIN, 0);
}

void BeginFill(int pwm)
{
  if (lowBatteryLatched) return;

  // If tank is already full, refuse to run
  if (IsTankFull()) { StopPump(); return; }

  pwm = constrain(pwm, 0, 255);
  if (pwm == 0) pwm = MIN_PWM;

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
  if (pwm == 0) pwm = MIN_PWM;

  PumpDriverEnable();
  PumpEnabled = true;

  digitalWrite(FILL_RELAY, LOW);
  digitalWrite(DRAIN_RELAY, HIGH);

  NxSetVal(SLIDER_DRAIN, pwm);
  SetTargetSpeed(-pwm);
}

// ===============================
// FLOW + VOLUME DISPLAY + AUTO-STOP + PROGRESS
// ===============================
static void UpdateFlowAndVolume(uint32_t pulseCount,
                                uint32_t &lastPulseCount,
                                uint32_t nowMs,
                                uint32_t &lastMs,
                                const char* flowObj,
                                const char* volObj,
                                int &lastVolumeStore)
{
  if (nowMs - lastMs < 500) return;
  float dt = (nowMs - lastMs) / 1000.0f;
  lastMs = nowMs;

  uint32_t dp = pulseCount - lastPulseCount;
  lastPulseCount = pulseCount;

  float hz = (dt > 0.0f) ? (dp / dt) : 0.0f;
  float q_lpm = hz / HZ_PER_LPM;

  int flow_ml_min = (int)(q_lpm * 1000.0f + 0.5f);
  float liters = pulseCount / PULSES_PER_LITER;
  int volume_ml = (int)(liters * 1000.0f + 0.5f);

  lastVolumeStore = volume_ml;

  NxSetVal(flowObj, flow_ml_min);
  NxSetVal(volObj, volume_ml);
}

void UpdateFlowDisplaysAutoStopAndProgress()
{
  uint32_t now = millis();

  // Fill page
  if (CurrentPage == FILLPAGE)
  {
    static uint32_t lastMsFill = 0;
    static uint32_t lastFill = 0;

    static int lastSentFlow = -1;
    static int lastSentVol  = -1;
    static int lastSentPct  = -1;

    noInterrupts(); uint32_t p = fillPulses; interrupts();

    if (now - lastMsFill < 500) return;
    float dt = (now - lastMsFill) / 1000.0f;
    lastMsFill = now;

    uint32_t dp = p - lastFill;
    lastFill = p;

    float hz = (dt > 0.0f) ? (dp / dt) : 0.0f;
    float q_lpm = hz / HZ_PER_LPM;

    int flow_ml_min = (int)(q_lpm * 1000.0f + 0.5f);
    float liters = p / PULSES_PER_LITER;
    int volume_ml = (int)(liters * 1000.0f + 0.5f);

    lastFillVolumeMl = volume_ml;

    int pct = 0;
    if (targetFillMl > 0)
    {
      pct = (int)((100.0f * (float)lastFillVolumeMl) / (float)targetFillMl + 0.5f);
      pct = constrain(pct, 0, 100);
    }

    if (flow_ml_min != lastSentFlow) { lastSentFlow = flow_ml_min; NxSetVal(NX_FLOW_RATE_FILL_OBJ, flow_ml_min); }
    if (volume_ml   != lastSentVol)  { lastSentVol  = volume_ml;   NxSetVal(NX_VOLUME_FILL_OBJ, volume_ml); }

    if (pct != lastSentPct)
    {
      lastSentPct = pct;
      NxSetVal(NX_PROGRESS_FILL_OBJ, pct);

      char pctStr[10];
      snprintf(pctStr, sizeof(pctStr), "%d%%", pct);
      NxSetText(NX_PERCENT_FILL_OBJ, pctStr);
    }

    // Stop if setpoint reached
    if (PumpEnabled && targetFillMl > 0 && lastFillVolumeMl >= targetFillMl)
      StopPump();

    // Stop if tank full sensor triggers (regardless of setpoint)
    if (PumpEnabled && IsTankFull())
      StopPump();
  }

  // Drain page
  if (CurrentPage == DRAINPAGE)
  {
    static uint32_t lastMsDrain = 0;
    static uint32_t lastDrain = 0;

    noInterrupts(); uint32_t p = drainPulses; interrupts();

    UpdateFlowAndVolume(p, lastDrain, now, lastMsDrain,
                        NX_FLOW_RATE_DRAIN_OBJ, NX_VOLUME_DRAIN_OBJ,
                        lastDrainVolumeMl);

    if (PumpEnabled && targetDrainMl > 0 && lastDrainVolumeMl >= targetDrainMl)
      StopPump();
  }
}

// ===============================
// POWER UI + CUTOFF (with sag filter)
// ===============================
static void UpdatePowerUIAndSafety()
{
  static uint32_t lastMs = 0;
  uint32_t now = millis();
  if (now - lastMs < 500) return;
  lastMs = now;

  float busV = ina219.getBusVoltage_V();
  float shuntmV = ina219.getShuntVoltage_mV();
  float packV_raw = busV + (shuntmV / 1000.0f);
  float currentA = fabs(ina219.getCurrent_mA() / 1000.0f);

  if (!filterInit)
  {
    filteredPackV = packV_raw;
    filterInit = true;
  }
  else
  {
    filteredPackV = filteredPackV + SAG_FILTER_ALPHA * (packV_raw - filteredPackV);
  }

  DetectCellCount(filteredPackV);

  float vPerCell_raw = (cellCount > 0) ? (packV_raw / (float)cellCount) : packV_raw;
  float vPerCell_f   = (cellCount > 0) ? (filteredPackV / (float)cellCount) : filteredPackV;

  if (lowBatteryLatched && CurrentPage == LOWBATTPAGE)
  {
    char buf[32];
    snprintf(buf, sizeof(buf), "Pack: %.2f V", (double)packV_raw);
    NxSetText(NX_LB_PACKV_TXT, buf);

    snprintf(buf, sizeof(buf), "Cell: %.2f V", (double)vPerCell_raw);
    NxSetText(NX_LB_CELLV_TXT, buf);
    return;
  }

  if (CurrentPage != LOWBATTPAGE)
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
    float cutoffCell = CUTOFF_V_PER_CELL;
    float hystCell   = SAG_HYST_PER_CELL;

    if (vPerCell_f <= cutoffCell)
    {
      if (lowCount < 255) lowCount++;
    }
    else if (vPerCell_f >= (cutoffCell + hystCell))
    {
      lowCount = 0;
    }

    if (lowCount >= SAG_TRIP_COUNT)
    {
      EnterLowBatteryPage(packV_raw, vPerCell_raw);
    }
  }
}

// ===============================
// NEXTION RX (numeric protocol)
// ===============================
static bool ReadU32(uint32_t &out)
{
  if (NEXTION.available() < 4) return false;

  uint8_t b0 = NEXTION.read();
  uint8_t b1 = NEXTION.read();
  uint8_t b2 = NEXTION.read();
  uint8_t b3 = NEXTION.read();

  out = ((uint32_t)b0) |
        ((uint32_t)b1 << 8) |
        ((uint32_t)b2 << 16) |
        ((uint32_t)b3 << 24);

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

  static bool waitingForSpeed = false;
  static bool waitingForTargetFill = false;
  static bool waitingForTargetDrain = false;

  uint32_t v;

  while (ReadU32(v))
  {
    if (waitingForTargetFill)
    {
      waitingForTargetFill = false;
      targetFillMl = (int)constrain((int32_t)v, 0, 2000000);
      if (CurrentPage == FILLPAGE) NxSetVal(NX_TARGET_FILL_OBJ, targetFillMl);
      if (CurrentPage == FILLPAGE) { NxSetVal(NX_PROGRESS_FILL_OBJ, 0); NxSetText(NX_PERCENT_FILL_OBJ, "0%"); }
      continue;
    }
    if (waitingForTargetDrain)
    {
      waitingForTargetDrain = false;
      targetDrainMl = (int)constrain((int32_t)v, 0, 2000000);
      if (CurrentPage == DRAINPAGE) NxSetVal(NX_TARGET_DRAIN_OBJ, targetDrainMl);
      continue;
    }

    if (waitingForSpeed)
    {
      waitingForSpeed = false;

      int spd = ApplyMinimumFloor(v);

      if (CurrentPage == FILLPAGE)
      {
        if (!PumpEnabled && spd > 0) BeginFill(spd);
        else if (PumpEnabled) SetTargetSpeed(+spd);
      }
      else if (CurrentPage == DRAINPAGE)
      {
        if (!PumpEnabled && spd > 0) BeginDrain(spd);
        else if (PumpEnabled) SetTargetSpeed(-spd);
      }
      continue;
    }

    if (v == 1) { EnterFillPage();  continue; }
    if (v == 2) { EnterDrainPage(); continue; }
    if (v == 3) { StopPump();       continue; }

    if (v == 11) { if (CurrentPage == FILLPAGE)  BeginFill(MIN_PWM);  continue; }
    if (v == 12) { if (CurrentPage == DRAINPAGE) BeginDrain(MIN_PWM); continue; }

    if (v == 1000) { waitingForSpeed = true; continue; }

    if (v == 2000) { waitingForTargetFill = true; continue; }
    if (v == 3000) { waitingForTargetDrain = true; continue; }
  }
}

// ===============================
// SETUP / LOOP
// ===============================
void setup()
{
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

  // Default safe state
  PumpDriverDisable();

  InitFlowSensors();

  Wire.begin();
  ina219.begin();

  NEXTION.begin(NEXTION_BAUD);

  PumpEnabled = false;
  currentSpeedSigned = 0;
  targetSpeedSigned  = 0;
  SetPumpOutput(0);

  lowBatteryLatched = false;
  CurrentPage = MAINPAGE;
  NxGotoPage(PAGE_MAIN);

  NxSetVal(NX_VOLUME_MAIN_OBJ, 0);

  // reset sag filter state
  filterInit = false;
  lowCount = 0;

  StopPump();
}

void loop()
{
  ProcessNextion();
  UpdateRamp();
  UpdateFlowDisplaysAutoStopAndProgress();
  UpdatePowerUIAndSafety();
}
