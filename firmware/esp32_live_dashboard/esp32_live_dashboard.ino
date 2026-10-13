/*
  ESP32 + capacitive soil moisture sensor: LIVE dashboard hosted on the ESP32 itself.
  No PC, backend or internet needed. Open the page on a phone/laptop on the same Wi-Fi.

  Wiring:  sensor VCC -> 3V3,  GND -> GND,  AOUT -> GPIO34
  Board:   "ESP32 Dev Module" (Arduino IDE). No extra libraries needed.

  How to see the live data:
    1. Upload, open Serial Monitor at 115200 baud.
    2. It prints the address (e.g. http://192.168.1.23). Open it in a browser.
    3. If Wi-Fi fails, it makes its own hotspot "SoilMonitor" (password 12345678);
       join it and open http://192.168.4.1
*/
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Preferences.h>

// ---------- Edit these ----------
const char* WIFI_SSID  = "YOUR_WIFI_NAME";
const char* WIFI_PASS  = "YOUR_WIFI_PASSWORD";
const char* DEVICE_ID  = "esp32-01";
const char* LOCATION   = "Foundation trench";
// Optional: also send readings to the Node backend. Leave "" to disable.
const char* SERVER_URL = "";                       // e.g. "http://192.168.1.50:5000/api/readings"
const char* API_KEY    = "change-me";
// --------------------------------

const int SENSOR_PIN = 34;
const int HIST_N = 120;                // 120 points x 5 s = last 10 minutes

WebServer server(80);
Preferences prefs;

int   rawDry = 3200, rawWet = 1400;    // calibration (saved in flash)
float dryBelow = 15, wetAbove = 35;    // moisture bands in % (saved in flash)

int   rawNow = 0;
float moistNow = 0;
float minV = 100, maxV = 0;
double sumV = 0;
long  nV = 0;
float hist[HIST_N];
int   histHead = 0, histCount = 0;
unsigned long lastSample = 0, lastPost = 0;
int secCounter = 0;

const char PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Soil Moisture - Live</title>
<style>
:root{--bg:#e4e6e3;--card:#f2f3f1;--ink:#1d2a30;--mut:#5d6b71;--line:#c7cbc7;--dry:#c79a52;--ok:#3b7a57;--wet:#1b6a8a;--bad:#a8402c}
*{box-sizing:border-box}
body{margin:0;font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;background:var(--bg);color:var(--ink)}
header{background:var(--ink);color:#f2f3f1;padding:16px 20px;display:flex;justify-content:space-between;flex-wrap:wrap;gap:6px}
header h1{margin:0;font-size:1.25rem}
#conn{font-size:.9rem;color:#b9c4c8}#conn.on::before{content:"";display:inline-block;width:9px;height:9px;border-radius:50%;background:#5fbf86;margin-right:7px}
main{max-width:760px;margin:0 auto;padding:16px}
.card{background:var(--card);border:1px solid var(--line);border-radius:6px;padding:18px;margin-bottom:14px}
.loc{margin:0;color:var(--mut)}
#val{font-size:clamp(4rem,18vw,6.5rem);font-weight:700;line-height:1;margin:4px 0}
#val small{font-size:.4em;color:var(--mut);margin-left:4px}
.dry{color:#8a6218}.workable{color:var(--ok)}.wet{color:var(--bad)}.fault{color:var(--bad)}
.msg{margin:0 0 14px;font-size:1.1rem;font-weight:600}
.barwrap{position:relative;padding-top:8px}
#bar{height:22px;border-radius:3px}
#mk{position:absolute;top:0;width:3px;height:38px;background:var(--ink);transform:translateX(-1px);transition:left .5s}
.lab{display:flex;justify-content:space-between;font-size:.8rem;color:var(--mut);margin-top:4px}
.stats{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;text-align:center}
.stats div{background:#fff;border:1px solid var(--line);border-radius:4px;padding:8px 2px}
.stats b{display:block;font-size:1.2rem}.stats span{font-size:.78rem;color:var(--mut)}
h2{margin:0 0 8px;font-size:1.05rem}
svg{width:100%;height:auto;display:block}
.hint{font-size:.88rem;color:var(--mut);margin:6px 0}
button{font:inherit;padding:10px 14px;margin:4px 6px 4px 0;border:1px solid var(--ink);background:var(--ink);color:#fff;border-radius:4px;cursor:pointer}
button.alt{background:#fff;color:var(--ink)}
input{font:inherit;width:70px;padding:6px;border:1px solid var(--line);border-radius:4px}
#note{font-size:.9rem;min-height:1.2em;margin:6px 0 0;color:var(--ok)}
footer{font-size:.82rem;color:var(--mut);text-align:center;padding-bottom:20px}
</style></head><body>
<header><h1>Soil moisture - live</h1><span id="conn">Connecting...</span></header>
<main>
<section class="card">
  <p class="loc" id="loc">-</p>
  <div id="val">--<small>%</small></div>
  <p class="msg" id="msg">Waiting for sensor...</p>
  <div class="barwrap"><div id="bar"></div><div id="mk" style="left:0"></div></div>
  <div class="lab"><span id="l1"></span><span id="l2"></span><span id="l3"></span></div>
</section>

<section class="card">
  <div class="stats">
    <div><b id="trend">-</b><span>Last minute</span></div>
    <div><b id="min">-</b><span>Lowest %</span></div>
    <div><b id="avg">-</b><span>Average %</span></div>
    <div><b id="max">-</b><span>Highest %</span></div>
  </div>
  <button class="alt" onclick="act('/reset')" style="margin-top:10px">Reset min / avg / max</button>
</section>

<section class="card">
  <h2>Last 10 minutes</h2>
  <svg viewBox="0 0 600 110" preserveAspectRatio="none">
    <rect id="band" x="0" width="600" fill="rgba(59,122,87,.14)"/>
    <polyline id="line" fill="none" stroke="#1b6a8a" stroke-width="2.5" stroke-linejoin="round" points=""/>
  </svg>
  <div class="lab"><span>10 min ago</span><span>now</span></div>
</section>

<section class="card">
  <h2>Calibrate for your soil</h2>
  <p class="hint">The sensor gives a raw number; calibration turns it into %. Do both steps once.</p>
  <p class="hint"><b>1.</b> Hold the sensor in open air (clean and dry), wait for the raw value to settle, then press Dry.<br>
  <b>2.</b> Put it in water up to the line (or in fully saturated soil of the same type), wait, then press Wet.</p>
  <button onclick="act('/cal?type=dry')">Set DRY now</button>
  <button onclick="act('/cal?type=wet')">Set WET now</button>
  <h2 style="margin-top:14px">Moisture limits (from your Proctor test)</h2>
  <p class="hint">Below the dry limit: too dry to compact. Above the wet limit: too wet.</p>
  Dry below <input id="inDry" type="number" step="0.5"> % &nbsp; Wet above <input id="inWet" type="number" step="0.5"> %
  <button class="alt" onclick="saveBands()">Save limits</button>
  <p id="note"></p>
</section>
<footer id="foot"></footer>
</main>
<script>
const $=id=>document.getElementById(id);
const MSG={dry:"Too dry for compaction. Add water before placing fill.",
workable:"In the workable range. Suitable for compaction.",
wet:"Too wet. Let it drain or dry before placing fill.",
fault:"No valid sensor signal. Check wiring: AOUT to GPIO34, VCC to 3V3, GND to GND."};
const TREND={rising:"Rising",falling:"Falling",steady:"Steady",wait:"Collecting"};
let filled=false;
function note(t,bad){const n=$('note');n.textContent=t;n.style.color=bad?'#a8402c':'#3b7a57';}
async function act(url){
  try{const r=await fetch(url,{method:'POST'});const t=await r.text();note(t,!r.ok);tick();}
  catch(e){note('Could not reach the ESP32.',true);}
}
function saveBands(){act('/bands?dry='+$('inDry').value+'&wet='+$('inWet').value);}
function render(d){
  const f=d.status==='fault';
  $('loc').textContent=d.location+'  ('+d.device+')';
  $('val').className=d.status;
  $('val').innerHTML=(f?'--':d.moisture.toFixed(1))+'<small>%</small>';
  $('msg').textContent=MSG[d.status];$('msg').className='msg '+d.status;
  $('bar').style.background='linear-gradient(90deg,#c79a52 0 '+d.dryBelow+'%,#3b7a57 '+d.dryBelow+'% '+d.wetAbove+'%,#1b6a8a '+d.wetAbove+'% 100%)';
  $('mk').style.left=(f?0:d.moisture)+'%';
  $('l1').textContent='Dry < '+d.dryBelow+'%';
  $('l2').textContent='Workable '+d.dryBelow+'-'+d.wetAbove+'%';
  $('l3').textContent='Wet > '+d.wetAbove+'%';
  $('trend').textContent=TREND[d.trend];
  $('min').textContent=d.min.toFixed(1);$('avg').textContent=d.avg.toFixed(1);$('max').textContent=d.max.toFixed(1);
  const h=d.history;
  if(h.length>1){
    const pts=h.map((v,i)=>(i/(HN-1)*600+(600-(h.length-1)/(HN-1)*600)).toFixed(1)+','+(110-v/100*110).toFixed(1));
    $('line').setAttribute('points',pts.join(' '));
  }
  $('band').setAttribute('y',110-d.wetAbove/100*110);
  $('band').setAttribute('height',(d.wetAbove-d.dryBelow)/100*110);
  if(!filled){$('inDry').value=d.dryBelow;$('inWet').value=d.wetAbove;filled=true;}
  const m=Math.floor(d.uptime/60);
  $('foot').textContent='Raw sensor value '+d.raw+'  |  Calibration dry '+d.dry+' / wet '+d.wetRaw+'  |  Wi-Fi '+(d.rssi?d.rssi+' dBm':'hotspot')+'  |  Running '+m+' min';
}
const HN=120;
async function tick(){
  try{
    const r=await fetch('/data',{cache:'no-store'});
    render(await r.json());
    $('conn').textContent='Live from ESP32';$('conn').className='on';
  }catch(e){$('conn').textContent='Lost connection to ESP32';$('conn').className='';}
}
tick();setInterval(tick,2000);
</script></body></html>)rawliteral";

// ---------- sensor logic ----------
int readRawAveraged() {
  long sum = 0;
  for (int i = 0; i < 16; i++) { sum += analogRead(SENSOR_PIN); delay(2); }
  return sum / 16;
}

float toPercent(int raw) {
  float pct = (float)(rawDry - raw) * 100.0f / (float)(rawDry - rawWet);
  return constrain(pct, 0.0f, 100.0f);
}

bool sensorFault() { return rawNow < 50 || rawNow > 4090; }

const char* statusNow() {
  if (sensorFault()) return "fault";
  if (moistNow < dryBelow) return "dry";
  if (moistNow > wetAbove) return "wet";
  return "workable";
}

const char* trendNow() {
  if (histCount < 13) return "wait";
  int idxNow = (histHead - 1 + HIST_N) % HIST_N;
  int idxOld = (histHead - 13 + HIST_N) % HIST_N;   // about 60 s earlier
  float diff = hist[idxNow] - hist[idxOld];
  if (diff > 1.5f) return "rising";
  if (diff < -1.5f) return "falling";
  return "steady";
}

void resetStats() { minV = 100; maxV = 0; sumV = 0; nV = 0; }

void sample() {
  rawNow = readRawAveraged();
  moistNow = toPercent(rawNow);
  if (!sensorFault()) {
    if (moistNow < minV) minV = moistNow;
    if (moistNow > maxV) maxV = moistNow;
    sumV += moistNow; nV++;
  }
  if (++secCounter % 5 == 0) {
    hist[histHead] = moistNow;
    histHead = (histHead + 1) % HIST_N;
    if (histCount < HIST_N) histCount++;
  }
  Serial.printf("raw=%d  moisture=%.1f%%  status=%s\n", rawNow, moistNow, statusNow());
}

// ---------- storage ----------
void loadSettings() {
  prefs.begin("soil", false);
  rawDry   = prefs.getInt("dry", 3200);
  rawWet   = prefs.getInt("wet", 1400);
  dryBelow = prefs.getFloat("dryB", 15);
  wetAbove = prefs.getFloat("wetA", 35);
  prefs.end();
}

void saveSettings() {
  prefs.begin("soil", false);
  prefs.putInt("dry", rawDry);
  prefs.putInt("wet", rawWet);
  prefs.putFloat("dryB", dryBelow);
  prefs.putFloat("wetA", wetAbove);
  prefs.end();
}

// ---------- web handlers ----------
void handleRoot() { server.send_P(200, "text/html", PAGE); }

void handleData() {
  String s;
  s.reserve(1500);
  s += "{\"raw\":" + String(rawNow);
  s += ",\"moisture\":" + String(moistNow, 1);
  s += ",\"status\":\"" + String(statusNow()) + "\"";
  s += ",\"trend\":\"" + String(trendNow()) + "\"";
  s += ",\"min\":" + String(nV ? minV : 0.0f, 1);
  s += ",\"max\":" + String(nV ? maxV : 0.0f, 1);
  s += ",\"avg\":" + String(nV ? (float)(sumV / nV) : 0.0f, 1);
  s += ",\"dry\":" + String(rawDry) + ",\"wetRaw\":" + String(rawWet);
  s += ",\"dryBelow\":" + String(dryBelow, 1) + ",\"wetAbove\":" + String(wetAbove, 1);
  s += ",\"uptime\":" + String(millis() / 1000);
  s += ",\"rssi\":" + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
  s += ",\"device\":\"" + String(DEVICE_ID) + "\",\"location\":\"" + String(LOCATION) + "\"";
  s += ",\"history\":[";
  for (int i = 0; i < histCount; i++) {
    int idx = (histHead - histCount + i + HIST_N) % HIST_N;
    if (i) s += ",";
    s += String(hist[idx], 1);
  }
  s += "]}";
  server.send(200, "application/json", s);
}

void handleCal() {
  String t = server.arg("type");
  int oldDry = rawDry, oldWet = rawWet;
  if (t == "dry") rawDry = rawNow;
  else if (t == "wet") rawWet = rawNow;
  else { server.send(400, "text/plain", "type must be dry or wet"); return; }
  if (rawDry - rawWet < 200) {
    rawDry = oldDry; rawWet = oldWet;
    server.send(400, "text/plain", "Not saved: the dry reading must be at least 200 higher than the wet reading. Check the sensor condition and try again.");
    return;
  }
  saveSettings();
  moistNow = toPercent(rawNow);
  resetStats();
  server.send(200, "text/plain", String("Saved ") + t + " calibration at raw " + rawNow + ".");
}

void handleBands() {
  float d = server.arg("dry").toFloat();
  float w = server.arg("wet").toFloat();
  if (d <= 0 || w >= 100 || d >= w) {
    server.send(400, "text/plain", "Not saved: use 0 < dry limit < wet limit < 100.");
    return;
  }
  dryBelow = d; wetAbove = w;
  saveSettings();
  server.send(200, "text/plain", "Moisture limits saved.");
}

void handleReset() { resetStats(); server.send(200, "text/plain", "Min / average / max reset."); }

// ---------- network ----------
void startNetwork() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to Wi-Fi");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) { delay(500); Serial.print("."); }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("\nOpen this in your browser:  http://");
    Serial.println(WiFi.localIP());
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("SoilMonitor", "12345678");
    Serial.println("\nWi-Fi failed. Hotspot 'SoilMonitor' (password 12345678) started.");
    Serial.print("Join it and open  http://");
    Serial.println(WiFi.softAPIP());
  }
  if (MDNS.begin("soilmonitor")) Serial.println("Also try: http://soilmonitor.local");
}

void postReading() {
  if (sensorFault()) return;
  char body[220];
  snprintf(body, sizeof(body),
           "{\"deviceId\":\"%s\",\"location\":\"%s\",\"raw\":%d,\"moisture\":%.1f}",
           DEVICE_ID, LOCATION, rawNow, moistNow);
  HTTPClient http;
  http.setTimeout(2000);
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-api-key", API_KEY);
  int code = http.POST(body);
  Serial.printf("Backend POST -> %d\n", code);
  http.end();
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  loadSettings();
  sample();
  startNetwork();
  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.on("/cal", HTTP_POST, handleCal);
  server.on("/bands", HTTP_POST, handleBands);
  server.on("/reset", HTTP_POST, handleReset);
  server.begin();
}

void loop() {
  server.handleClient();
  unsigned long now = millis();
  if (now - lastSample >= 1000) { lastSample = now; sample(); }
  if (SERVER_URL[0] && WiFi.status() == WL_CONNECTED && now - lastPost >= 30000) {
    lastPost = now;
    postReading();
  }
}
