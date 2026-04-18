#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

// ── WiFi credentials ──────────────────────────────────────────────────────────
#define AP_SSID     "MCP-FuelStation"
#define AP_PASS     "fuelpump1"
// Home network for development — comment out for field use
#define HOME_SSID   "SilverLining"
#define HOME_PASS   "KLHj0b01"
#define USE_HOME_WIFI false   // set true to join home network instead of AP

// ── Serial to Teensy ──────────────────────────────────────────────────────────
#define TEENSY_SERIAL   Serial1
#define TEENSY_BAUD     115200
#define TEENSY_RX_PIN   16
#define TEENSY_TX_PIN   17

// ── Servers ───────────────────────────────────────────────────────────────────
WebServer         httpServer(80);
WebSocketsServer  wsServer(81);

// ── State ─────────────────────────────────────────────────────────────────────
String lastJsonState = "{}";
uint8_t connectedClients = 0;

// ── Forward declarations ──────────────────────────────────────────────────────
void handleRoot();
void handleNotFound();
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
void processTeensyLine(const String &line);
void broadcastState(const String &json);

// ── HTML page (served from PROGMEM) ───────────────────────────────────────────
// The full HTML is in data/index.html — served as a string here for simplicity
extern const char INDEX_HTML[] PROGMEM;

// =============================================================================
void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("\nMCP Fuel Station ESP32 Bridge starting...");

  // Teensy serial
  TEENSY_SERIAL.begin(TEENSY_BAUD, SERIAL_8N1, TEENSY_RX_PIN, TEENSY_TX_PIN);

  // WiFi
  if (USE_HOME_WIFI)
  {
    Serial.printf("Connecting to %s...\n", HOME_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(HOME_SSID, HOME_PASS);
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 30)
    {
      delay(500);
      Serial.print(".");
      retries++;
    }
    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
    }
    else
    {
      Serial.println("\nHome WiFi failed, falling back to AP mode");
      goto start_ap;
    }
  }
  else
  {
    start_ap:
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("AP started: %s  IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
  }

  // HTTP server
  httpServer.on("/", handleRoot);
  httpServer.onNotFound(handleNotFound);
  httpServer.begin();
  Serial.println("HTTP server started on port 80");

  // WebSocket server
  wsServer.begin();
  wsServer.onEvent(webSocketEvent);
  Serial.println("WebSocket server started on port 81");
}

// =============================================================================
void loop()
{
  httpServer.handleClient();
  wsServer.loop();

  // Read lines from Teensy
  static String teensyBuf = "";
  while (TEENSY_SERIAL.available())
  {
    char c = TEENSY_SERIAL.read();
    if (c == '\n')
    {
      teensyBuf.trim();
      if (teensyBuf.length() > 0)
        processTeensyLine(teensyBuf);
      teensyBuf = "";
    }
    else
    {
      teensyBuf += c;
    }
  }
}

// =============================================================================
void processTeensyLine(const String &line)
{
  // Expect lines starting with "WS:" followed by JSON
  if (line.startsWith("WS:"))
  {
    String json = line.substring(3);
    lastJsonState = json;
    broadcastState(json);
  }
}

// =============================================================================
void broadcastState(const String &json)
{
  wsServer.broadcastTXT(json.c_str());
}

// =============================================================================
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
  switch (type)
  {
    case WStype_CONNECTED:
      connectedClients++;
      Serial.printf("WS client %d connected\n", num);
      // Send current state to new client
      if (lastJsonState.length() > 2)
        wsServer.sendTXT(num, lastJsonState.c_str());
      break;

    case WStype_DISCONNECTED:
      if (connectedClients > 0) connectedClients--;
      Serial.printf("WS client %d disconnected\n", num);
      break;

    case WStype_TEXT:
    {
      // Command from browser → forward to Teensy
      String cmd = String((char*)payload).substring(0, length);
      Serial.printf("WS cmd from client %d: %s\n", num, cmd.c_str());
      // Forward as CMD:value\n to Teensy
      TEENSY_SERIAL.println(cmd);
      break;
    }

    default:
      break;
  }
}

// =============================================================================
void handleRoot()
{
  httpServer.send_P(200, "text/html", INDEX_HTML);
}

void handleNotFound()
{
  httpServer.send(404, "text/plain", "Not found");
}

// =============================================================================
// Embedded HTML — full web app
// =============================================================================
const char INDEX_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
<title>MCP Auto Fill Station</title>
<style>
  :root {
    --bg: #0a0f1e;
    --panel: #111827;
    --panel2: #1a2236;
    --border: #1e3a5f;
    --accent: #00d4ff;
    --accent2: #0088aa;
    --green: #00e676;
    --red: #ff3d3d;
    --yellow: #ffd600;
    --orange: #ff9100;
    --text: #e0f0ff;
    --text2: #7090b0;
    --radius: 12px;
    --radius-sm: 8px;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; -webkit-tap-highlight-color: transparent; }
  body {
    background: var(--bg);
    color: var(--text);
    font-family: 'Segoe UI', system-ui, sans-serif;
    min-height: 100vh;
    overflow-x: hidden;
  }

  /* ── Header ── */
  #header {
    background: linear-gradient(135deg, #0d1f3c, #1a3a6c);
    border-bottom: 2px solid var(--accent);
    padding: 12px 16px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    box-shadow: 0 4px 20px rgba(0,212,255,0.15);
  }
  #header h1 {
    font-size: 18px;
    font-weight: 700;
    letter-spacing: 1px;
    color: var(--accent);
    text-shadow: 0 0 20px rgba(0,212,255,0.5);
  }
  #header .version { font-size: 11px; color: var(--text2); }
  #ws-status {
    width: 10px; height: 10px;
    border-radius: 50%;
    background: var(--red);
    box-shadow: 0 0 8px var(--red);
    transition: all 0.3s;
  }
  #ws-status.connected { background: var(--green); box-shadow: 0 0 8px var(--green); }

  /* ── Pages ── */
  .page { display: none; padding: 12px; }
  .page.active { display: block; }

  /* ── Nav tabs ── */
  #nav {
    display: flex;
    background: var(--panel);
    border-bottom: 1px solid var(--border);
    overflow-x: auto;
    scrollbar-width: none;
  }
  #nav::-webkit-scrollbar { display: none; }
  .nav-btn {
    flex: 0 0 auto;
    padding: 10px 16px;
    font-size: 12px;
    font-weight: 600;
    letter-spacing: 0.5px;
    color: var(--text2);
    border: none;
    background: none;
    cursor: pointer;
    border-bottom: 2px solid transparent;
    transition: all 0.2s;
    white-space: nowrap;
  }
  .nav-btn.active { color: var(--accent); border-bottom-color: var(--accent); }
  .nav-btn:active { background: rgba(0,212,255,0.05); }

  /* ── Cards ── */
  .card {
    background: var(--panel);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 14px;
    margin-bottom: 12px;
  }
  .card-title {
    font-size: 11px;
    font-weight: 700;
    letter-spacing: 1.5px;
    color: var(--accent);
    text-transform: uppercase;
    margin-bottom: 10px;
  }

  /* ── Model image ── */
  #model-image {
    width: 100%;
    max-width: 320px;
    height: 180px;
    object-fit: cover;
    border-radius: var(--radius-sm);
    border: 1px solid var(--border);
    background: var(--panel2);
    display: block;
    margin: 0 auto 10px;
  }

  /* ── Value rows ── */
  .val-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 6px 0;
    border-bottom: 1px solid rgba(30,58,95,0.5);
  }
  .val-row:last-child { border-bottom: none; }
  .val-label { font-size: 12px; color: var(--text2); }
  .val-value { font-size: 14px; font-weight: 600; color: var(--text); }
  .val-value.accent { color: var(--accent); }
  .val-value.green { color: var(--green); }
  .val-value.red { color: var(--red); }
  .val-value.yellow { color: var(--yellow); }

  /* ── Progress bars ── */
  .progress-wrap { margin: 8px 0; }
  .progress-label {
    display: flex;
    justify-content: space-between;
    font-size: 11px;
    color: var(--text2);
    margin-bottom: 4px;
  }
  .progress-bar {
    width: 100%;
    height: 12px;
    background: var(--panel2);
    border-radius: 6px;
    overflow: hidden;
    border: 1px solid var(--border);
  }
  .progress-fill {
    height: 100%;
    border-radius: 6px;
    transition: width 0.5s ease;
    background: linear-gradient(90deg, var(--accent2), var(--accent));
  }
  .progress-fill.green { background: linear-gradient(90deg, #00a040, var(--green)); }
  .progress-fill.red   { background: linear-gradient(90deg, #a00000, var(--red)); }
  .progress-fill.orange { background: linear-gradient(90deg, #c06000, var(--orange)); }

  /* ── Message box ── */
  #main-message, #fill-message, #drain-message {
    text-align: center;
    padding: 8px;
    border-radius: var(--radius-sm);
    font-size: 13px;
    font-weight: 600;
    letter-spacing: 0.5px;
    margin-bottom: 10px;
    min-height: 36px;
    display: flex;
    align-items: center;
    justify-content: center;
  }
  .msg-blue  { background: rgba(0,150,200,0.15); color: #00d4ff; border: 1px solid rgba(0,212,255,0.3); }
  .msg-red   { background: rgba(200,0,0,0.15);   color: #ff6060; border: 1px solid rgba(255,60,60,0.3); }
  .msg-green { background: rgba(0,180,80,0.15);  color: #00e676; border: 1px solid rgba(0,230,118,0.3); }
  .msg-white { background: rgba(255,255,255,0.05); color: #ccc;  border: 1px solid rgba(255,255,255,0.1); }

  /* ── Buttons ── */
  .btn-row { display: flex; gap: 10px; margin-top: 10px; }
  .btn {
    flex: 1;
    padding: 14px 10px;
    border: none;
    border-radius: var(--radius-sm);
    font-size: 14px;
    font-weight: 700;
    letter-spacing: 1px;
    cursor: pointer;
    transition: all 0.15s;
    text-transform: uppercase;
  }
  .btn:active { transform: scale(0.96); }
  .btn-start  { background: linear-gradient(135deg, #006030, var(--green)); color: #000; }
  .btn-stop   { background: linear-gradient(135deg, #800000, var(--red));   color: #fff; }
  .btn-back   { background: linear-gradient(135deg, #5a4000, var(--yellow)); color: #000; }
  .btn-fill   { background: linear-gradient(135deg, #004070, var(--accent)); color: #000; }
  .btn-drain  { background: linear-gradient(135deg, #400060, #b060ff);       color: #fff; }
  .btn-setup  { background: linear-gradient(135deg, #303030, #707070);       color: #fff; }
  .btn-station { background: linear-gradient(135deg, #402000, var(--orange)); color: #000; }
  .btn:disabled { opacity: 0.3; pointer-events: none; }

  /* ── Supply tank ── */
  .supply-bar { height: 20px; border-radius: 10px; margin: 6px 0; }

  /* ── Grid layout for params ── */
  .params-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 8px;
  }
  .param-box {
    background: var(--panel2);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    padding: 8px;
    text-align: center;
  }
  .param-box .label { font-size: 10px; color: var(--text2); margin-bottom: 2px; }
  .param-box .value { font-size: 14px; font-weight: 700; color: var(--text); }

  /* ── Low battery page ── */
  #lowbat-icon { font-size: 64px; text-align: center; margin: 20px 0; }

  /* ── Spinner ── */
  #loading {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    height: 200px;
    gap: 16px;
    color: var(--text2);
  }
  .spinner {
    width: 40px; height: 40px;
    border: 3px solid var(--border);
    border-top-color: var(--accent);
    border-radius: 50%;
    animation: spin 0.8s linear infinite;
  }
  @keyframes spin { to { transform: rotate(360deg); } }

  /* ── Slider ── */
  .slider-wrap { margin: 10px 0; }
  .slider-label { font-size: 11px; color: var(--text2); margin-bottom: 4px; }
  input[type=range] {
    width: 100%;
    accent-color: var(--accent);
    height: 6px;
  }

  /* ── Setup model list ── */
  .model-item {
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 10px;
    border-radius: var(--radius-sm);
    border: 1px solid var(--border);
    margin-bottom: 8px;
    cursor: pointer;
    transition: all 0.2s;
  }
  .model-item.active-model { border-color: var(--accent); background: rgba(0,212,255,0.05); }
  .model-item:active { background: rgba(0,212,255,0.1); }
  .model-dot {
    width: 10px; height: 10px;
    border-radius: 50%;
    background: var(--text2);
    flex-shrink: 0;
  }
  .model-dot.active { background: var(--accent); box-shadow: 0 0 8px var(--accent); }
  .model-name { font-size: 14px; font-weight: 600; flex: 1; }
  .model-stats { font-size: 11px; color: var(--text2); }
</style>
</head>
<body>

<div id="header">
  <div>
    <h1>⛽ MCP AUTO FILL</h1>
    <div class="version" id="hdr-version">v--</div>
  </div>
  <div id="ws-status" title="WebSocket"></div>
</div>

<div id="nav">
  <button class="nav-btn active" onclick="showPage('main')">MAIN</button>
  <button class="nav-btn" onclick="showPage('fill')">FILL</button>
  <button class="nav-btn" onclick="showPage('drain')">DRAIN</button>
  <button class="nav-btn" onclick="showPage('setup')">SETUP</button>
  <button class="nav-btn" onclick="showPage('station')">STATION</button>
  <button class="nav-btn" id="nav-lowbat" style="display:none" onclick="showPage('lowbat')">⚠ BATT</button>
</div>

<!-- ═══════════════════════════════════ MAIN PAGE ═══════════════════════════ -->
<div id="page-main" class="page active">
  <div id="main-message" class="msg-blue">MCP Auto Fill Station</div>

  <div class="card">
    <img id="model-image-main" src="" alt="Model" style="width:100%;height:160px;object-fit:cover;border-radius:8px;background:#111;">
    <div style="text-align:center;margin-top:8px;font-size:18px;font-weight:700;" id="main-model-name">—</div>
  </div>

  <div class="card">
    <div class="card-title">Supply Tank</div>
    <div class="progress-wrap">
      <div class="progress-label">
        <span id="main-supply-vol">—</span>
        <span id="main-supply-pct">—</span>
      </div>
      <div class="progress-bar">
        <div class="progress-fill green" id="main-supply-bar" style="width:0%"></div>
      </div>
    </div>
    <div class="val-row">
      <span class="val-label">Battery</span>
      <span class="val-value" id="main-batt-type">—</span>
    </div>
    <div class="val-row">
      <span class="val-label">Pack voltage</span>
      <span class="val-value accent" id="main-packv">—</span>
    </div>
    <div class="val-row">
      <span class="val-label">Current</span>
      <span class="val-value accent" id="main-current">—</span>
    </div>
  </div>

  <div class="card">
    <div class="card-title">Model Parameters</div>
    <div class="params-grid">
      <div class="param-box"><div class="label">Tank Vol</div><div class="value" id="main-tank-vol">—</div></div>
      <div class="param-box"><div class="label">Sensor</div><div class="value" id="main-sensor">—</div></div>
      <div class="param-box"><div class="label">Fill Speed</div><div class="value" id="main-fill-spd">—</div></div>
      <div class="param-box"><div class="label">Drain Speed</div><div class="value" id="main-drain-spd">—</div></div>
    </div>
  </div>

  <div class="btn-row">
    <button class="btn btn-fill"   onclick="sendCmd(1)">FILL</button>
    <button class="btn btn-drain"  onclick="sendCmd(2)">DRAIN</button>
  </div>
  <div class="btn-row">
    <button class="btn btn-setup"   onclick="sendCmd(4000)">SETUP</button>
    <button class="btn btn-station" onclick="sendCmd(4030)">STATION</button>
  </div>
</div>

<!-- ═══════════════════════════════════ FILL PAGE ════════════════════════════ -->
<div id="page-fill" class="page">
  <div id="fill-message" class="msg-blue">Fill Mode</div>

  <div class="card">
    <div class="card-title">Fill Progress</div>
    <div class="progress-wrap">
      <div class="progress-label">
        <span>Fill volume</span>
        <span id="fill-vol-txt">0 / 0ml</span>
      </div>
      <div class="progress-bar">
        <div class="progress-fill" id="fill-progress-bar" style="width:0%"></div>
      </div>
    </div>
    <div class="progress-wrap">
      <div class="progress-label">
        <span>Flow rate</span>
        <span id="fill-flow-txt">0 ml/m</span>
      </div>
      <div class="progress-bar">
        <div class="progress-fill orange" id="fill-flow-bar" style="width:0%"></div>
      </div>
    </div>
    <div class="val-row">
      <span class="val-label">Helicopter fill</span>
      <span class="val-value" id="fill-heli-vol">—</span>
    </div>
  </div>

  <div class="card">
    <div class="card-title">Supply Tank</div>
    <div class="progress-wrap">
      <div class="progress-label">
        <span id="fill-supply-vol">—</span>
        <span id="fill-supply-pct">—</span>
      </div>
      <div class="progress-bar">
        <div class="progress-fill green" id="fill-supply-bar" style="width:0%"></div>
      </div>
    </div>
    <div class="val-row">
      <span class="val-label">Battery</span>
      <span class="val-value" id="fill-batt-type">—</span>
    </div>
    <div class="val-row">
      <span class="val-label">Pack voltage</span>
      <span class="val-value accent" id="fill-packv">—</span>
    </div>
    <div class="val-row">
      <span class="val-label">Current</span>
      <span class="val-value accent" id="fill-current">—</span>
    </div>
  </div>

  <div class="card">
    <div class="card-title">Speed</div>
    <div class="slider-wrap">
      <div class="slider-label">Fill speed: <span id="fill-spd-label">500 ml/m</span></div>
      <input type="range" min="0" max="1000" value="500" id="fill-speed-slider"
             oninput="onFillSlider(this.value)" onchange="onFillSlider(this.value)">
    </div>
  </div>

  <div class="btn-row">
    <button class="btn btn-start" id="btn-fill-start" onclick="sendCmd(11)">START</button>
    <button class="btn btn-stop"  id="btn-fill-stop"  onclick="sendCmd(3)" style="display:none">STOP</button>
    <button class="btn btn-back"  id="btn-fill-back"  onclick="sendCmd(3)">BACK</button>
  </div>
</div>

<!-- ═══════════════════════════════════ DRAIN PAGE ═══════════════════════════ -->
<div id="page-drain" class="page">
  <div id="drain-message" class="msg-blue">Drain Mode</div>

  <div class="card">
    <div class="card-title">Drain Progress</div>
    <div class="progress-wrap">
      <div class="progress-label">
        <span>Drain volume</span>
        <span id="drain-vol-txt">0ml</span>
      </div>
      <div class="progress-bar">
        <div class="progress-fill orange" id="drain-progress-bar" style="width:100%"></div>
      </div>
    </div>
    <div class="progress-wrap">
      <div class="progress-label">
        <span>Flow rate</span>
        <span id="drain-flow-txt">0 ml/m</span>
      </div>
      <div class="progress-bar">
        <div class="progress-fill orange" id="drain-flow-bar" style="width:0%"></div>
      </div>
    </div>
  </div>

  <div class="card">
    <div class="card-title">Supply Tank</div>
    <div class="progress-wrap">
      <div class="progress-label">
        <span id="drain-supply-vol">—</span>
        <span id="drain-supply-pct">—</span>
      </div>
      <div class="progress-bar">
        <div class="progress-fill green" id="drain-supply-bar" style="width:0%"></div>
      </div>
    </div>
    <div class="val-row">
      <span class="val-label">Battery</span>
      <span class="val-value" id="drain-batt-type">—</span>
    </div>
    <div class="val-row">
      <span class="val-label">Pack voltage</span>
      <span class="val-value accent" id="drain-packv">—</span>
    </div>
    <div class="val-row">
      <span class="val-label">Current</span>
      <span class="val-value accent" id="drain-current">—</span>
    </div>
  </div>

  <div class="card">
    <div class="card-title">Speed</div>
    <div class="slider-wrap">
      <div class="slider-label">Drain speed: <span id="drain-spd-label">500 ml/m</span></div>
      <input type="range" min="0" max="1000" value="500" id="drain-speed-slider"
             oninput="onDrainSlider(this.value)" onchange="onDrainSlider(this.value)">
    </div>
  </div>

  <div class="btn-row">
    <button class="btn btn-start" id="btn-drain-start" onclick="sendCmd(12)">START</button>
    <button class="btn btn-stop"  id="btn-drain-stop"  onclick="sendCmd(3)" style="display:none">STOP</button>
    <button class="btn btn-back"  id="btn-drain-back"  onclick="sendCmd(3)">BACK</button>
  </div>
</div>

<!-- ═══════════════════════════════════ SETUP PAGE ═══════════════════════════ -->
<div id="page-setup" class="page">
  <div class="card">
    <div class="card-title">Select Model</div>
    <div id="model-list">
      <div id="loading"><div class="spinner"></div><span>Loading models...</span></div>
    </div>
  </div>

  <div class="card" id="setup-params-card" style="display:none">
    <div class="card-title">Model Parameters — <span id="setup-model-name">—</span></div>
    <div class="val-row"><span class="val-label">Tank Volume</span><span class="val-value accent" id="setup-tank-vol">—</span></div>
    <div class="val-row"><span class="val-label">Fill Speed</span><span class="val-value" id="setup-fill-spd">—</span></div>
    <div class="val-row"><span class="val-label">Drain Speed</span><span class="val-value" id="setup-drain-spd">—</span></div>
    <div class="val-row"><span class="val-label">Tank Sensor</span><span class="val-value" id="setup-sensor">—</span></div>
    <div class="val-row"><span class="val-label">Purge Time</span><span class="val-value" id="setup-purge">—</span></div>
    <div class="val-row"><span class="val-label">Total Fills</span><span class="val-value" id="setup-fills">—</span></div>
    <div class="val-row"><span class="val-label">Total Drains</span><span class="val-value" id="setup-drains">—</span></div>
    <div class="val-row"><span class="val-label">Fill Volume</span><span class="val-value" id="setup-fill-vol">—</span></div>
    <div class="val-row"><span class="val-label">Drain Volume</span><span class="val-value" id="setup-drain-vol">—</span></div>
    <div class="val-row"><span class="val-label">Fuel Used</span><span class="val-value green" id="setup-total-vol">—</span></div>
  </div>

  <div class="btn-row">
    <button class="btn btn-back" onclick="sendCmd(4020)">BACK</button>
    <button class="btn btn-station" onclick="sendCmd(4030)">STATION</button>
  </div>
</div>

<!-- ═══════════════════════════════════ STATION PAGE ════════════════════════ -->
<div id="page-station" class="page">
  <div class="card">
    <div class="card-title">Supply Tank</div>
    <div class="val-row"><span class="val-label">Capacity</span><span class="val-value accent" id="st-cap">—</span></div>
    <div class="val-row"><span class="val-label">Remaining</span><span class="val-value green" id="st-rem">—</span></div>
    <div class="val-row"><span class="val-label">Low threshold</span><span class="val-value yellow" id="st-low">—</span></div>
  </div>

  <div class="card">
    <div class="card-title">Station Statistics</div>
    <div class="val-row"><span class="val-label">Total Fills</span><span class="val-value" id="st-total-fills">—</span></div>
    <div class="val-row"><span class="val-label">Total Drains</span><span class="val-value" id="st-total-drains">—</span></div>
    <div class="val-row"><span class="val-label">Fill Volume</span><span class="val-value" id="st-fill-vol">—</span></div>
    <div class="val-row"><span class="val-label">Drain Volume</span><span class="val-value" id="st-drain-vol">—</span></div>
    <div class="val-row"><span class="val-label">Fuel Used</span><span class="val-value green" id="st-net-vol">—</span></div>
  </div>

  <div class="card">
    <div class="card-title">Calibration</div>
    <div class="val-row"><span class="val-label">Fill pulses/L</span><span class="val-value" id="st-fill-cal">—</span></div>
    <div class="val-row"><span class="val-label">Drain pulses/L</span><span class="val-value" id="st-drain-cal">—</span></div>
    <div class="val-row"><span class="val-label">Volume</span><span class="val-value accent" id="st-volume">—</span></div>
  </div>

  <div class="btn-row">
    <button class="btn btn-back" onclick="sendCmd(4020)">BACK</button>
  </div>
</div>

<!-- ═══════════════════════════════════ LOW BAT PAGE ════════════════════════ -->
<div id="page-lowbat" class="page">
  <div id="lowbat-icon">🔋</div>
  <div class="card">
    <div class="card-title" style="color:var(--red)">LOW BATTERY</div>
    <div class="val-row"><span class="val-label">Pack voltage</span><span class="val-value red" id="lb-packv">—</span></div>
    <div class="val-row"><span class="val-label">Cell voltage</span><span class="val-value red" id="lb-cellv">—</span></div>
    <div class="val-row"><span class="val-label">Cell count</span><span class="val-value" id="lb-cells">—</span></div>
  </div>
</div>

<script>
// ── WebSocket ─────────────────────────────────────────────────────────────────
let ws = null;
let wsConnected = false;
let reconnectTimer = null;
let sliderDebounce = null;

function connectWS() {
  const host = window.location.hostname;
  ws = new WebSocket('ws://' + host + ':81');

  ws.onopen = () => {
    wsConnected = true;
    document.getElementById('ws-status').className = 'connected';
    console.log('WS connected');
    clearTimeout(reconnectTimer);
  };

  ws.onclose = () => {
    wsConnected = false;
    document.getElementById('ws-status').className = '';
    console.log('WS disconnected, reconnecting...');
    reconnectTimer = setTimeout(connectWS, 2000);
  };

  ws.onerror = () => { ws.close(); };

  ws.onmessage = (evt) => {
    try {
      const s = JSON.parse(evt.data);
      updateUI(s);
    } catch(e) { console.warn('Bad JSON:', evt.data); }
  };
}

function sendCmd(cmd) {
  if (ws && wsConnected) {
    ws.send('CMD:' + cmd);
  }
}

// ── Sliders ───────────────────────────────────────────────────────────────────
function onFillSlider(val) {
  document.getElementById('fill-spd-label').textContent = val + ' ml/m';
  clearTimeout(sliderDebounce);
  sliderDebounce = setTimeout(() => sendCmd('1000:' + val), 200);
}
function onDrainSlider(val) {
  document.getElementById('drain-spd-label').textContent = val + ' ml/m';
  clearTimeout(sliderDebounce);
  sliderDebounce = setTimeout(() => sendCmd('1000:' + val), 200);
}

// ── Page navigation ───────────────────────────────────────────────────────────
let currentPage = 'main';
function showPage(name) {
  document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
  document.querySelectorAll('.nav-btn').forEach(b => b.classList.remove('active'));
  document.getElementById('page-' + name).classList.add('active');
  const btns = document.querySelectorAll('.nav-btn');
  btns.forEach(b => { if (b.textContent.toLowerCase().includes(name.substring(0,4))) b.classList.add('active'); });
  currentPage = name;
}

// ── Map Teensy page numbers to our page names ─────────────────────────────────
const PAGE_MAP = {
  0: 'main',   // SplashPage — treat as main
  1: 'main',
  2: 'fill',
  3: 'drain',
  4: 'lowbat',
  5: 'setup',
  6: 'station',
  7: 'main'    // keybdB — stay on main
};

// ── Update UI from state JSON ─────────────────────────────────────────────────
function updateUI(s) {
  // Auto-navigate to correct page
  if (s.page !== undefined && PAGE_MAP[s.page]) {
    const pageName = PAGE_MAP[s.page];
    if (pageName !== currentPage) showPage(pageName);
  }

  // Version
  if (s.version) document.getElementById('hdr-version').textContent = s.version;

  // ── Main page ──
  if (s.modelName) {
    document.getElementById('main-model-name').textContent = s.modelName;
  }
  if (s.modelImage) {
    document.getElementById('model-image-main').src = 'data:image/jpeg;base64,' + s.modelImage;
  }
  if (s.supplyPct !== undefined) {
    set('main-supply-pct', s.supplyPct + '%');
    set('fill-supply-pct', s.supplyPct + '%');
    set('drain-supply-pct', s.supplyPct + '%');
    setBar('main-supply-bar',   s.supplyPct, s.supplyLow);
    setBar('fill-supply-bar',   s.supplyPct, s.supplyLow);
    setBar('drain-supply-bar',  s.supplyPct, s.supplyLow);
  }
  if (s.supplyMl !== undefined) {
    const supTxt = (s.supplyMl / 1000).toFixed(1) + 'L / ' + (s.supplyCapMl / 1000).toFixed(1) + 'L';
    set('main-supply-vol', supTxt);
    set('fill-supply-vol', supTxt);
    set('drain-supply-vol', supTxt);
  }
  if (s.battType) {
    set('main-batt-type', s.battType);
    set('fill-batt-type', s.battType);
    set('drain-batt-type', s.battType);
  }
  if (s.packV) {
    set('main-packv', s.packV + 'V');
    set('fill-packv', s.packV + 'V');
    set('drain-packv', s.packV + 'V');
  }
  if (s.currentA) {
    set('main-current', s.currentA + 'A');
    set('fill-current', s.currentA + 'A');
    set('drain-current', s.currentA + 'A');
  }

  // Model params on main page
  if (s.tankVol)    set('main-tank-vol',   s.tankVol + 'ml');
  if (s.sensor)     set('main-sensor',     s.sensor);
  if (s.fillSpd)    set('main-fill-spd',   s.fillSpd);
  if (s.drainSpd)   set('main-drain-spd',  s.drainSpd);

  // ── Message ──
  if (s.message !== undefined) {
    const msgEl = document.getElementById(currentPage + '-message');
    if (msgEl) {
      msgEl.textContent = s.message;
      msgEl.className = 'msg-' + (s.msgColor || 'white');
    }
  }

  // ── Fill page ──
  if (s.fillFlow !== undefined) {
    set('fill-flow-txt', s.fillFlow + ' ml/m');
    setBar('fill-flow-bar', Math.min(100, s.fillFlow / 10), false);
  }
  if (s.fillVol !== undefined && s.fillTarget !== undefined) {
    set('fill-vol-txt', s.fillVol + ' / ' + s.fillTarget + 'ml');
    setBar('fill-progress-bar', s.fillPct || 0, false);
  }
  if (s.heliVol)  set('fill-heli-vol', s.heliVol);
  if (s.fillSpeedSlider !== undefined) {
    document.getElementById('fill-speed-slider').value = s.fillSpeedSlider;
    set('fill-spd-label', s.fillSpeedSlider + ' ml/m');
  }

  // ── Drain page ──
  if (s.drainFlow !== undefined) {
    set('drain-flow-txt', s.drainFlow + ' ml/m');
    setBar('drain-flow-bar', Math.min(100, s.drainFlow / 10), false);
  }
  if (s.drainVol !== undefined) {
    set('drain-vol-txt', s.drainVol + 'ml');
    setBar('drain-progress-bar', 100 - (s.drainPct || 0), false);
  }
  if (s.drainSpeedSlider !== undefined) {
    document.getElementById('drain-speed-slider').value = s.drainSpeedSlider;
    set('drain-spd-label', s.drainSpeedSlider + ' ml/m');
  }

  // ── Pump state — show/hide start/stop/back ──
  if (s.pumpOn !== undefined) {
    const fillStart = document.getElementById('btn-fill-start');
    const fillStop  = document.getElementById('btn-fill-stop');
    const fillBack  = document.getElementById('btn-fill-back');
    const drainStart = document.getElementById('btn-drain-start');
    const drainStop  = document.getElementById('btn-drain-stop');
    const drainBack  = document.getElementById('btn-drain-back');

    if (s.page === 2) { // fill page
      fillStart.style.display = s.pumpOn ? 'none' : '';
      fillStop.style.display  = s.pumpOn ? '' : 'none';
      fillBack.style.display  = s.pumpOn ? 'none' : '';
    }
    if (s.page === 3) { // drain page
      drainStart.style.display = s.pumpOn ? 'none' : '';
      drainStop.style.display  = s.pumpOn ? '' : 'none';
      drainBack.style.display  = s.pumpOn ? 'none' : '';
    }
  }

  // ── Setup page — use active model data from JSON ──
  if (s.models) {
    renderModelList(s.models, s.activeModel, s.previewModel);
  }
  // Show setup params from main JSON fields (active/preview model)
  if (s.modelName && currentPage === 'setup') {
    document.getElementById('setup-params-card').style.display = '';
    set('setup-model-name', s.modelName);
    set('setup-tank-vol',   s.tankVol + 'ml');
    set('setup-fill-spd',   s.fillSpd);
    set('setup-drain-spd',  s.drainSpd);
    set('setup-sensor',     s.sensor);
  }
  if (s.setupStats) {
    document.getElementById('setup-params-card').style.display = '';
    set('setup-purge',      s.setupStats.purge + 's');
    set('setup-fills',      s.setupStats.totalFills);
    set('setup-drains',     s.setupStats.totalDrains);
    set('setup-fill-vol',   s.setupStats.totalFillVol + 'L');
    set('setup-drain-vol',  s.setupStats.totalDrainVol + 'L');
    set('setup-total-vol',  s.setupStats.totalVol + 'L');
  }

  // ── Station page ──
  if (s.station) {
    set('st-cap',          s.station.cap);
    set('st-rem',          s.station.rem);
    set('st-low',          s.station.low);
    set('st-fill-cal',     s.station.fillCal);
    set('st-drain-cal',    s.station.drainCal);
    set('st-volume',       s.station.volume);
    set('st-total-fills',  s.station.totalFills);
    set('st-total-drains', s.station.totalDrains);
    set('st-fill-vol',     s.station.fillVol);
    set('st-drain-vol',    s.station.drainVol);
    set('st-net-vol',      s.station.netVol);
  }

  // ── Low battery page ──
  if (s.lowBat) {
    set('lb-packv', s.lowBat.packV + 'V');
    set('lb-cellv', s.lowBat.cellV + 'V');
    set('lb-cells', s.lowBat.cells);
    document.getElementById('nav-lowbat').style.display = '';
  }
}

function set(id, val) {
  const el = document.getElementById(id);
  if (el) el.textContent = val;
}

function setBar(id, pct, isLow) {
  const el = document.getElementById(id);
  if (!el) return;
  el.style.width = Math.max(0, Math.min(100, pct)) + '%';
  el.className = 'progress-fill ' + (isLow ? 'red' : pct < 20 ? 'orange' : 'green');
}

function renderModelList(models, activeIdx, previewIdx) {
  const list = document.getElementById('model-list');
  list.innerHTML = '';
  models.forEach((m, i) => {
    const div = document.createElement('div');
    div.className = 'model-item' + (i === activeIdx ? ' active-model' : '');
    div.innerHTML = `
      <div class="model-dot ${i === activeIdx ? 'active' : ''}"></div>
      <div class="model-name">${m.name}</div>
      <div class="model-stats">${m.totalFills} fills</div>
    `;
    div.onclick = () => {
      sendCmd('8020');
      setTimeout(() => sendCmd(i), 100);
    };
    list.appendChild(div);
  });
}

// ── Start ─────────────────────────────────────────────────────────────────────
connectWS();
</script>
</body>
</html>
)rawhtml";
