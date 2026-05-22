#include "webserver.h"
#include "settings.h"
#include "detector.h"
#include "renderer.h"
#include "morse.h"
#include "config.h"
#include <WiFi.h>
#include <WebServer.h>

static WebServer server(80);

// ── HTML page (served from flash) ────────────────────────────────────────────

static const char HTML_PAGE[] PROGMEM = R"WEBPAGE(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Attack Warning Light</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:monospace;background:#0d0d1a;color:#ddd;max-width:460px;margin:0 auto;padding:14px}
h1{color:#ff4444;font-size:1em;margin-bottom:14px;text-transform:uppercase;letter-spacing:3px}
.card{background:#151524;border:1px solid #2a2a3a;border-radius:6px;padding:14px;margin-bottom:10px}
h2{font-size:.7em;color:#666;text-transform:uppercase;letter-spacing:1px;margin-bottom:10px}
.sstate{font-size:1.1em;font-weight:bold}
.ok{color:#44cc66}.alrt{color:#ff4444}
.counts{display:flex;gap:10px;margin-top:10px}
.cnt{flex:1;background:#0d0d1a;padding:8px 4px;border-radius:4px;text-align:center}
.cnt .n{font-size:1.3em;color:#ffaa00}
.cnt .lbl{font-size:.6em;color:#666;display:block;margin-top:2px}
.banner{background:#ff8800;color:#000;font-size:.75em;padding:7px 10px;border-radius:4px;margin-bottom:10px}
label{display:block;font-size:.7em;color:#888;margin:10px 0 3px}
.val{color:#ffaa00;float:right}
input[type=range]{width:100%;accent-color:#ff4444;display:block}
input[type=number],input[type=text],select{width:100%;background:#0d0d1a;color:#ddd;border:1px solid #333;border-radius:4px;padding:5px 8px;font:inherit;font-size:.85em}
input[type=color]{width:100%;height:42px;padding:3px 4px;border:1px solid #333;border-radius:4px;background:#0d0d1a;cursor:pointer}
.row{display:flex;gap:8px;margin-top:14px}
button{flex:1;padding:9px;border:none;border-radius:4px;font:inherit;cursor:pointer}
.save{background:#cc2222;color:#fff}.rst{background:#222;color:#888}.sos{background:#004488;color:#66aaff}
.hint{font-size:.65em;color:#444;margin-top:10px;padding-top:8px;border-top:1px solid #1e1e2e}
.rl{display:flex;gap:14px;margin:10px 0;font-size:.8em;color:#aaa}
.rl label{display:flex;align-items:center;gap:5px;cursor:pointer;margin:0}
.pkt{border-top:1px solid #1e1e2e;padding:7px 0;font-size:.7em}
.pkt:first-child{border-top:none;padding-top:0}
.pt{margin-bottom:3px}.pt.de{color:#ff6666}.pt.di{color:#ff9933}
.pm{color:#666}
</style>
</head>
<body>
<h1>&#9888; Attack Warning Light</h1>
<div id="hbanner" class="banner" style="display:none">Channel hopping paused &mdash; monitoring fixed channel only while connected</div>
<div class="card">
  <h2>Live Status</h2>
  <div class="sstate ok" id="st">&#8212;</div>
  <div style="font-size:.75em;color:#666;margin-top:3px">Mode: <span id="mn">&#8212;</span></div>
  <div class="counts">
    <div class="cnt"><span class="n" id="cd">0</span><span class="lbl">Deauth</span></div>
    <div class="cnt"><span class="n" id="cb">0</span><span class="lbl">Beacon</span></div>
    <div class="cnt"><span class="n" id="cp">0</span><span class="lbl">Probe</span></div>
  </div>
</div>
<div class="card">
  <h2>Packet Log <span id="log_hdr" style="font-weight:normal;color:#444;letter-spacing:0">&mdash;</span></h2>
  <div id="pkt_log" style="min-height:24px"><div style="color:#444;font-size:.7em">No frames captured yet</div></div>
</div>
<div class="card">
  <h2>Display</h2>
  <label>Brightness <span class="val" id="bv">80</span></label>
  <input type="range" id="brightness" min="10" max="255" oninput="bv.textContent=this.value">
  <label>Ambient Mode</label>
  <select id="mode" onchange="onModeChange()">
    <option value="0">Candle</option><option value="1">Rainbow</option>
    <option value="2">Breathe</option><option value="3">Forest</option>
    <option value="4">Ocean</option><option value="5">KITT</option>
    <option value="6">Radial Breathe</option><option value="7">Plasma</option>
    <option value="8">Electric Arc</option><option value="9">Fire</option>
    <option value="10">Solid Colour</option>
  </select>
  <div id="cc_row" style="display:none">
    <label>Colour</label>
    <input type="color" id="custom_col" value="#ffffd0">
  </div>
  <h2 style="margin-top:14px">Alert Thresholds <span style="font-weight:normal;color:#444">(frames / window)</span></h2>
  <label>Deauth <span class="val" id="dv">10</span></label>
  <input type="range" id="deauth_t" min="1" max="200" oninput="dv.textContent=this.value">
  <label>Beacon <span class="val" id="bkv">50</span></label>
  <input type="range" id="beacon_t" min="5" max="500" oninput="bkv.textContent=this.value">
  <label>Probe <span class="val" id="pv">15</span></label>
  <input type="range" id="probe_t" min="1" max="200" oninput="pv.textContent=this.value">
  <h2 style="margin-top:14px">Timing</h2>
  <label>Alert Cooldown (ms)</label>
  <input type="number" id="cooldown" min="1000" max="60000" step="1000">
  <label>Detection Window (ms)</label>
  <input type="number" id="det_win" min="500" max="10000" step="500">
  <label>Channel Hop Interval (ms)</label>
  <input type="number" id="hop_ms" min="50" max="2000" step="50">
  <div class="row">
    <button class="save" onclick="saveSettings()">Save</button>
    <button class="rst" onclick="resetSettings()">Reset Defaults</button>
  </div>
  <div class="hint" id="hw">LED_COUNT=? &nbsp; LED_PIN=? &nbsp; (compile-time only, reflash to change)</div>
</div>
<div class="card">
  <h2>Auto Cycle</h2>
  <label style="display:flex;align-items:center;gap:8px;cursor:pointer;font-size:.8em;color:#aaa;margin:0 0 10px">
    <input type="checkbox" id="rc_en" onchange="applyRandom()" style="width:16px;height:16px;accent-color:#ff4444">
    Cycle through ambient modes randomly
  </label>
  <label>Dwell time <span class="val" id="dwv">30</span> s</label>
  <input type="range" id="rc_dwell" min="5" max="120" value="30" oninput="g('dwv').textContent=this.value" onchange="applyRandom()">
</div>
<div class="card">
  <h2>Morse Code</h2>
  <label>Message (A&#8211;Z, 0&#8211;9, spaces)</label>
  <input type="text" id="morse_txt" maxlength="40" placeholder="e.g. HELLO WORLD">
  <div class="rl">
    <label><input type="radio" name="morse_loop" value="0" checked style="accent-color:#ff4444"> Once</label>
    <label><input type="radio" name="morse_loop" value="1" style="accent-color:#ff4444"> Loop</label>
  </div>
  <div class="row">
    <button class="save" onclick="txMorse()">Send</button>
    <button class="sos" onclick="txSOS()">SOS</button>
    <button class="rst" onclick="stopMorse()">Stop</button>
  </div>
  <div id="morse_status" style="font-size:.7em;color:#666;margin-top:8px"></div>
</div>
<script>
var bv=document.getElementById('bv'),
    dv=document.getElementById('dv'),
    bkv=document.getElementById('bkv'),
    pv=document.getElementById('pv');
function g(id){return document.getElementById(id)}
function hexToRgb(h){return{r:parseInt(h.slice(1,3),16),g:parseInt(h.slice(3,5),16),b:parseInt(h.slice(5,7),16)}}
function rgbToHex(r,gv,b){return'#'+[r,gv,b].map(function(v){return('0'+v.toString(16)).slice(-2)}).join('')}
function onModeChange(){g('cc_row').style.display=g('mode').value=='10'?'block':'none'}
function loadSettings(){
  fetch('/settings').then(r=>r.json()).then(d=>{
    g('brightness').value=d.brightness; bv.textContent=d.brightness;
    g('mode').value=d.mode;
    g('deauth_t').value=d.deauth_t; dv.textContent=d.deauth_t;
    g('beacon_t').value=d.beacon_t; bkv.textContent=d.beacon_t;
    g('probe_t').value=d.probe_t; pv.textContent=d.probe_t;
    g('cooldown').value=d.cooldown;
    g('det_win').value=d.det_win;
    g('hop_ms').value=d.hop_ms;
    g('rc_dwell').value=d.dwell_s; g('dwv').textContent=d.dwell_s;
    g('custom_col').value=rgbToHex(d.custom_r,d.custom_g,d.custom_b);
    g('cc_row').style.display=d.mode==10?'block':'none';
    g('hw').textContent='LED_COUNT='+d.led_count+'  LED_PIN='+d.led_pin+'  (compile-time only, reflash to change)';
  }).catch(function(){});
}
function pollStatus(){
  fetch('/status').then(r=>r.json()).then(d=>{
    var el=g('st');
    el.textContent=d.state;
    el.className='sstate '+(d.state.indexOf('Alert')>=0?'alrt':'ok');
    g('mn').textContent=d.mode_name;
    g('cd').textContent=d.deauth;
    g('cb').textContent=d.beacon;
    g('cp').textContent=d.probe;
    g('hbanner').style.display=d.hopping?'none':'block';
    g('rc_en').checked=d.auto_cycle;
    if(!d.morse_active) g('morse_status').textContent='';
  }).catch(function(){});
}
function fetchLog(){
  fetch('/log').then(r=>r.json()).then(function(d){
    g('log_hdr').textContent=d.count?'('+d.count+' frames)':'';
    var el=g('pkt_log');
    if(!d.count){el.innerHTML='<div style="color:#444;font-size:.7em">No frames captured yet</div>';return;}
    el.innerHTML=d.entries.map(function(e){
      var age=Math.round(e.t/1000);
      var isde=e.sub===0x0C;
      var lbl=isde?'Deauth':'Disassoc';
      return '<div class="pkt">'
           + '<div class="pt '+(isde?'de':'di')+'">'+lbl+'</div>'
           + '<div class="pm">T+'+age+'s &nbsp; ch'+e.ch+' &nbsp; '+e.rssi+'dBm</div>'
           + '<div class="pm">SA:&nbsp;&nbsp; '+e.sa+'</div>'
           + '<div class="pm">BSSID: '+e.bssid+'</div>'
           + '</div>';
    }).join('');
  }).catch(function(){});
}
function saveSettings(){
  var rgb=hexToRgb(g('custom_col').value);
  var p=new URLSearchParams({
    brightness:g('brightness').value,
    mode:g('mode').value,
    deauth_t:g('deauth_t').value,
    beacon_t:g('beacon_t').value,
    probe_t:g('probe_t').value,
    cooldown:g('cooldown').value,
    det_win:g('det_win').value,
    hop_ms:g('hop_ms').value,
    custom_r:rgb.r,custom_g:rgb.g,custom_b:rgb.b
  });
  fetch('/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})
    .then(function(){loadSettings();}).catch(function(){});
}
function resetSettings(){
  fetch('/reset',{method:'POST'}).then(function(){loadSettings();}).catch(function(){});
}
function applyRandom(){
  var p=new URLSearchParams({enabled:g('rc_en').checked?1:0,dwell_s:g('rc_dwell').value});
  fetch('/random',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()}).catch(function(){});
}
function txMorse(){
  var txt=g('morse_txt').value.trim();
  if(!txt)return;
  var lp=document.querySelector('input[name=morse_loop]:checked').value;
  var p=new URLSearchParams({text:txt,loop:lp});
  fetch('/morse',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})
    .then(function(){g('morse_status').textContent='Sending…';}).catch(function(){});
}
function txSOS(){
  fetch('/sos',{method:'POST'})
    .then(function(){g('morse_status').textContent='SOS looping…';}).catch(function(){});
}
function stopMorse(){
  fetch('/morse/stop',{method:'POST'})
    .then(function(){g('morse_status').textContent='';}).catch(function(){});
}
loadSettings();
pollStatus();
fetchLog();
setInterval(pollStatus,1000);
setInterval(fetchLog,5000);
</script>
</body>
</html>
)WEBPAGE";

// ── String helpers ────────────────────────────────────────────────────────────

static const char* state_to_str(DeviceState s) {
    switch (s) {
        case STATE_ALERT_DEAUTH:  return "Deauth Alert";
        case STATE_ALERT_BEACON:  return "Beacon Alert";
        case STATE_ALERT_PROBE:   return "Probe Alert";
        case STATE_ALERT_MULTI:   return "Multi Alert";
        case STATE_TRANSITIONING: return "Transitioning";
        default:                  return "Ambient";
    }
}

static const char* mode_to_str(AmbientMode m) {
    switch (m) {
        case AMBIENT_CANDLE:  return "Candle";
        case AMBIENT_RAINBOW: return "Rainbow";
        case AMBIENT_BREATHE: return "Breathe";
        case AMBIENT_FOREST:  return "Forest";
        case AMBIENT_OCEAN:   return "Ocean";
        case AMBIENT_KITT:    return "KITT";
        case AMBIENT_RADIAL:  return "Radial Breathe";
        case AMBIENT_PLASMA:  return "Plasma";
        case AMBIENT_ARC:     return "Electric Arc";
        case AMBIENT_FIRE:    return "Fire";
        case AMBIENT_SOLID:   return "Solid Colour";
        default:              return "Unknown";
    }
}

// ── Route handlers ────────────────────────────────────────────────────────────

static void handle_root() {
    server.send_P(200, "text/html", HTML_PAGE);
}

static void handle_status() {
    DeviceState state;
    AmbientMode mode;
    if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(20))) {
        state = g_device_state;
        mode  = g_ambient_mode;
        xSemaphoreGive(g_state_mutex);
    } else {
        server.send(503, "application/json", "{}");
        return;
    }

    char json[320];
    snprintf(json, sizeof(json),
        "{\"state\":\"%s\",\"mode\":%d,\"mode_name\":\"%s\","
        "\"deauth\":%lu,\"beacon\":%lu,\"probe\":%lu,"
        "\"hopping\":%d,\"morse_active\":%d,\"auto_cycle\":%d}",
        state_to_str(state),
        (int)mode,
        mode_to_str(mode),
        (unsigned long)g_last_deauth,
        (unsigned long)g_last_beacon,
        (unsigned long)g_last_probe,
        (WiFi.softAPgetStationNum() == 0) ? 1 : 0,
        g_morse_active ? 1 : 0,
        g_random_cycle ? 1 : 0
    );
    server.send(200, "application/json", json);
}

static void handle_settings_get() {
    AmbientMode mode;
    if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(20))) {
        mode = g_ambient_mode;
        xSemaphoreGive(g_state_mutex);
    } else {
        server.send(503, "application/json", "{}");
        return;
    }

    char json[384];
    snprintf(json, sizeof(json),
        "{\"brightness\":%d,\"mode\":%d,"
        "\"deauth_t\":%lu,\"beacon_t\":%lu,\"probe_t\":%lu,"
        "\"cooldown\":%lu,\"det_win\":%lu,\"hop_ms\":%lu,"
        "\"dwell_s\":%lu,\"custom_r\":%d,\"custom_g\":%d,\"custom_b\":%d,"
        "\"led_count\":%d,\"led_pin\":%d}",
        (int)g_brightness,
        (int)mode,
        (unsigned long)g_deauth_thresh,
        (unsigned long)g_beacon_thresh,
        (unsigned long)g_probe_thresh,
        (unsigned long)g_alert_cooldown,
        (unsigned long)g_detect_window,
        (unsigned long)g_channel_hop_ms,
        (unsigned long)(g_random_dwell_ms / 1000),
        (int)g_custom_r,
        (int)g_custom_g,
        (int)g_custom_b,
        LED_COUNT,
        LED_PIN
    );
    server.send(200, "application/json", json);
}

static void handle_settings_post() {
    if (server.hasArg("brightness")) {
        int v = server.arg("brightness").toInt();
        if (v >= 10 && v <= 255) g_brightness = (uint8_t)v;
    }
    if (server.hasArg("custom_r")) {
        int v = server.arg("custom_r").toInt();
        if (v >= 0 && v <= 255) g_custom_r = (uint8_t)v;
    }
    if (server.hasArg("custom_g")) {
        int v = server.arg("custom_g").toInt();
        if (v >= 0 && v <= 255) g_custom_g = (uint8_t)v;
    }
    if (server.hasArg("custom_b")) {
        int v = server.arg("custom_b").toInt();
        if (v >= 0 && v <= 255) g_custom_b = (uint8_t)v;
    }
    if (server.hasArg("deauth_t")) {
        int v = server.arg("deauth_t").toInt();
        if (v >= 1 && v <= 1000) g_deauth_thresh = (uint32_t)v;
    }
    if (server.hasArg("beacon_t")) {
        int v = server.arg("beacon_t").toInt();
        if (v >= 1 && v <= 1000) g_beacon_thresh = (uint32_t)v;
    }
    if (server.hasArg("probe_t")) {
        int v = server.arg("probe_t").toInt();
        if (v >= 1 && v <= 1000) g_probe_thresh = (uint32_t)v;
    }
    if (server.hasArg("cooldown")) {
        int v = server.arg("cooldown").toInt();
        if (v >= 1000 && v <= 120000) g_alert_cooldown = (uint32_t)v;
    }
    if (server.hasArg("det_win")) {
        int v = server.arg("det_win").toInt();
        if (v >= 500 && v <= 30000) g_detect_window = (uint32_t)v;
    }
    if (server.hasArg("hop_ms")) {
        int v = server.arg("hop_ms").toInt();
        if (v >= 50 && v <= 5000) g_channel_hop_ms = (uint32_t)v;
    }
    if (server.hasArg("mode")) {
        int v = server.arg("mode").toInt();
        if (v >= 0 && v < (int)AMBIENT_MODE_COUNT) {
            if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(100))) {
                g_ambient_mode = (AmbientMode)v;
                xSemaphoreGive(g_state_mutex);
            }
            g_web_mode_changed = true;
            settings_save_mode((uint8_t)v);
        }
    }

    settings_save();
    server.send(200, "text/plain", "OK");
}

static void handle_reset() {
    settings_reset_defaults();

    if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(100))) {
        g_ambient_mode = AMBIENT_CANDLE;
        xSemaphoreGive(g_state_mutex);
    }
    g_web_mode_changed = true;
    settings_save_mode(AMBIENT_CANDLE);

    server.send(200, "text/plain", "OK");
}

static void handle_random() {
    if (server.hasArg("enabled")) {
        g_random_cycle = server.arg("enabled").toInt() != 0;
    }
    if (server.hasArg("dwell_s")) {
        int v = server.arg("dwell_s").toInt();
        if (v >= 5 && v <= 120) {
            g_random_dwell_ms = (uint32_t)v * 1000;
            settings_save();
        }
    }
    server.send(200, "text/plain", "OK");
}

static void handle_morse_post() {
    if (!server.hasArg("text") || !server.hasArg("loop")) {
        server.send(400, "text/plain", "Bad Request");
        return;
    }
    String txt = server.arg("text");
    txt.trim();
    if (txt.length() == 0 || txt.length() > 127) {
        server.send(400, "text/plain", "Bad Request");
        return;
    }
    g_morse_active       = false;
    g_morse_pending_loop = server.arg("loop").toInt() != 0;
    strncpy(g_morse_pending_text, txt.c_str(), sizeof(g_morse_pending_text) - 1);
    g_morse_pending_text[sizeof(g_morse_pending_text) - 1] = '\0';
    g_morse_pending      = true;
    server.send(200, "text/plain", "OK");
}

static void handle_sos() {
    g_morse_active       = false;
    g_morse_pending_loop = true;
    strncpy(g_morse_pending_text, "SOS", sizeof(g_morse_pending_text) - 1);
    g_morse_pending_text[sizeof(g_morse_pending_text) - 1] = '\0';
    g_morse_pending      = true;
    server.send(200, "text/plain", "OK");
}

static void handle_morse_stop() {
    g_morse_pending = false;
    g_morse_active  = false;
    server.send(200, "text/plain", "OK");
}

static void handle_log() {
    static char json[2500];
    uint8_t  count = g_pkt_log_count;
    uint8_t  head  = g_pkt_log_head;

    int pos = snprintf(json, sizeof(json),
        "{\"count\":%d,\"entries\":[", (int)count);

    for (int i = 0; i < (int)count && pos < (int)sizeof(json) - 120; i++) {
        // Newest first: walk backwards from head
        int idx = ((int)head - 1 - i + PKT_LOG_SIZE) % PKT_LOG_SIZE;
        const PacketLogEntry& e = g_pkt_log[idx];
        if (i > 0) json[pos++] = ',';
        pos += snprintf(json + pos, sizeof(json) - pos,
            "{\"t\":%lu,"
            "\"sa\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
            "\"bssid\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
            "\"rssi\":%d,\"ch\":%d,\"sub\":%d}",
            (unsigned long)e.timestamp_ms,
            e.sa[0],    e.sa[1],    e.sa[2],    e.sa[3],    e.sa[4],    e.sa[5],
            e.bssid[0], e.bssid[1], e.bssid[2], e.bssid[3], e.bssid[4], e.bssid[5],
            (int)e.rssi, (int)e.channel, (int)e.subtype
        );
    }

    snprintf(json + pos, sizeof(json) - pos, "]}");
    server.send(200, "application/json", json);
}

// ── Web server task ───────────────────────────────────────────────────────────

static void webserver_task(void* pvParameters) {
    for (;;) {
        server.handleClient();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ── Init ──────────────────────────────────────────────────────────────────────

void webserver_init() {
    WiFi.softAP("AttackLight", nullptr, DEFAULT_WIFI_CHANNEL);

    server.on("/",          HTTP_GET,  handle_root);
    server.on("/status",    HTTP_GET,  handle_status);
    server.on("/settings",  HTTP_GET,  handle_settings_get);
    server.on("/settings",  HTTP_POST, handle_settings_post);
    server.on("/reset",     HTTP_POST, handle_reset);
    server.on("/random",    HTTP_POST, handle_random);
    server.on("/morse",     HTTP_POST, handle_morse_post);
    server.on("/sos",       HTTP_POST, handle_sos);
    server.on("/morse/stop",HTTP_POST, handle_morse_stop);
    server.on("/log",       HTTP_GET,  handle_log);
    server.begin();

    xTaskCreate(webserver_task, "webserver", 8192, NULL, 3, NULL);
}
