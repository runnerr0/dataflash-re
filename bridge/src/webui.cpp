// webui.cpp — async web server: status, config, output test. Page embedded in flash.
#include "webui.h"
#include "config.h"
#include "inputs.h"
#include "dataflash_tx.h"
#include "net.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

static AsyncWebServer server(80);

static const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Dataflash Bridge</title><style>
body{font:14px system-ui,sans-serif;margin:0;background:#0e1116;color:#e6edf3}
header{padding:14px 18px;background:#161b22;border-bottom:1px solid #30363d}
h1{font-size:16px;margin:0}main{max-width:680px;margin:0 auto;padding:18px}
.card{background:#161b22;border:1px solid #30363d;border-radius:10px;padding:14px;margin:0 0 14px}
.row{display:flex;justify-content:space-between;padding:3px 0;border-bottom:1px solid #21262d}
label{display:block;margin:8px 0 3px;color:#9da7b3}input,select{width:100%;padding:7px;border-radius:6px;border:1px solid #30363d;background:#0d1117;color:#e6edf3;box-sizing:border-box}
button{margin-top:10px;padding:8px 14px;border:0;border-radius:6px;background:#238636;color:#fff;cursor:pointer}
.k{color:#9da7b3}.v{font-weight:600}small{color:#6e7681}
</style></head><body>
<header><h1>Dataflash Bridge &mdash; Art-Net / sACN &rarr; RS-485</h1></header><main>
<div class=card><b>Status</b><div id=st></div></div>
<div class=card><b>Wi-Fi</b> <small>(join your network; AP stays as a config portal)</small>
  <div id=wst></div>
  <label>Network (SSID)</label>
  <input id=wssid list=nets placeholder="e.g. Warp Core"><datalist id=nets></datalist>
  <label>Password <small>(blank = keep saved)</small></label>
  <input id=wpass type=password placeholder="(unchanged)">
  <button onclick=scanWifi()>Scan</button>
  <button onclick=saveWifi()>Connect</button>
</div>
<div class=card><b>Output test</b>
  <label>Mode</label><select id=tmode><option value=0>Live (network)</option><option value=1>All on</option><option value=2>Chase</option><option value=3>Single fixture</option></select>
  <label>Level (0-15)</label><input id=tlevel type=number min=0 max=15>
  <label>Fixture index</label><input id=tindex type=number min=0 max=255>
  <label><input type=checkbox id=oen style=width:auto> output enable</label>
  <button onclick=saveTest()>Apply test</button>
</div>
<div class=card><b>Config</b> <small>(network changes reboot)</small>
  <label>Universe</label><input id=uni type=number min=0 max=32767>
  <label>Start channel (1-based)</label><input id=start type=number min=1 max=512>
  <label>Fixture count (1-256)</label><input id=nfix type=number min=1 max=256>
  <label>Refresh Hz</label><input id=rhz type=number min=1 max=60>
  <label><input type=checkbox id=nswap style=width:auto> nibble swap</label>
  <label><input type=checkbox id=htp style=width:auto> HTP merge</label>
  <button onclick=saveCfg()>Save &amp; apply</button>
</div></main><script>
async function poll(){let s=await (await fetch('/api/status')).json();
 document.getElementById('st').innerHTML=
  row('Network',s.net+' '+s.ip)+row('Active source',s.src)+
  row('Art-Net pkts',s.art)+row('sACN pkts',s.sacn)+
  row('TX refreshes',s.tx)+row('Heartbeats',s.hb)+
  row('Universe',s.uni)+row('Fixtures',s.nfix)+row('Output',s.oen?'ON':'off');}
function row(k,v){return '<div class=row><span class=k>'+k+'</span><span class=v>'+v+'</span></div>'}
async function loadCfg(){let c=await (await fetch('/api/config')).json();
 uni.value=c.uni;start.value=c.start;nfix.value=c.nfix;rhz.value=c.rhz;
 nswap.checked=c.nswap;htp.checked=c.htp;oen.checked=c.oen;tmode.value=c.tmode;tlevel.value=c.tlevel;tindex.value=c.tindex;}
async function saveTest(){let b=new URLSearchParams({tmode:tmode.value,tlevel:tlevel.value,tindex:tindex.value,oen:oen.checked?1:0});
 await fetch('/api/test',{method:'POST',body:b});}
async function saveCfg(){let b=new URLSearchParams({uni:uni.value,start:start.value,nfix:nfix.value,rhz:rhz.value,nswap:nswap.checked?1:0,htp:htp.checked?1:0});
 await fetch('/api/config',{method:'POST',body:b});alert('saved');}
async function loadWifi(){let w=await (await fetch('/api/wifi')).json();
 document.getElementById('wst').innerHTML=row('Mode',w.mode)+row('IP',w.ip)+row('Saved SSID',w.ssid||'(none)');
 if(!wssid.value&&w.ssid)wssid.value=w.ssid;}
async function scanWifi(){let d=document.getElementById('nets');
 let r=await (await fetch('/api/wifi/scan')).json();
 if(r.scanning){setTimeout(scanWifi,1500);return;}
 d.innerHTML='';r.nets.forEach(n=>{let o=document.createElement('option');o.value=n;d.appendChild(o)});}
async function saveWifi(){let b=new URLSearchParams({ssid:wssid.value,pass:wpass.value});
 await fetch('/api/wifi',{method:'POST',body:b});
 alert('Connecting to "'+wssid.value+'". On success the bridge moves to that network — reach it at its new IP (see serial log or your router). The AP returns if it fails.');}
loadCfg();loadWifi();poll();setInterval(()=>{poll();loadWifi()},2000);
</script></body></html>
)HTML";

static String statusJson() {
  char b[420];
  snprintf(b, sizeof(b),
    "{\"net\":\"%s\",\"ip\":\"%s\",\"src\":\"%s\",\"art\":%u,\"sacn\":%u,"
    "\"tx\":%u,\"hb\":%u,\"uni\":%u,\"nfix\":%u,\"oen\":%s}",
    net_mode(), net_ip().toString().c_str(), g_dmx.activeSource(),
    (unsigned)g_artPkts, (unsigned)g_sacnPkts, (unsigned)g_tx.packets,
    (unsigned)g_tx.heartbeats, g_cfg.universe, g_cfg.fixtureCount,
    g_cfg.outputEnable ? "true" : "false");
  return String(b);
}

static String configJson() {
  char b[300];
  snprintf(b, sizeof(b),
    "{\"uni\":%u,\"start\":%u,\"nfix\":%u,\"rhz\":%u,\"nswap\":%s,\"htp\":%s,"
    "\"oen\":%s,\"tmode\":%u,\"tlevel\":%u,\"tindex\":%u}",
    g_cfg.universe, g_cfg.startChannel, g_cfg.fixtureCount, g_cfg.refreshHz,
    g_cfg.nibbleSwap?"true":"false", g_cfg.htpMerge?"true":"false",
    g_cfg.outputEnable?"true":"false", g_cfg.testMode, g_cfg.testLevel, g_cfg.testIndex);
  return String(b);
}

static int ip(AsyncWebServerRequest* r, const char* k, int def) {
  if (r->hasParam(k, true)) return r->getParam(k, true)->value().toInt();
  return def;
}

void webui_begin() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r){ r->send_P(200, "text/html", PAGE); });
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* r){ r->send(200, "application/json", statusJson()); });
  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* r){ r->send(200, "application/json", configJson()); });

  // --- Wi-Fi: status, scan, connect ---
  server.on("/api/wifi", HTTP_GET, [](AsyncWebServerRequest* r){
    char b[220];
    snprintf(b, sizeof(b),
      "{\"ssid\":\"%s\",\"mode\":\"%s\",\"ip\":\"%s\",\"sta\":%s}",
      g_cfg.wifiSsid, net_mode(), net_ip().toString().c_str(),
      net_staUp() ? "true" : "false");
    r->send(200, "application/json", String(b));
  });

  server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest* r){
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_FAILED) {            // -2: none started -> kick one off
      WiFi.scanNetworks(true /*async*/);
      r->send(200, "application/json", "{\"scanning\":true,\"nets\":[]}");
      return;
    }
    if (n == WIFI_SCAN_RUNNING) {           // -1: still scanning
      r->send(200, "application/json", "{\"scanning\":true,\"nets\":[]}");
      return;
    }
    String j = "{\"scanning\":false,\"nets\":[";
    for (int i = 0; i < n; i++) {
      if (i) j += ",";
      String s = WiFi.SSID(i); s.replace("\\", "\\\\"); s.replace("\"", "\\\"");
      j += "\"" + s + "\"";
    }
    j += "]}";
    WiFi.scanDelete();
    r->send(200, "application/json", j);
  });

  server.on("/api/wifi", HTTP_POST, [](AsyncWebServerRequest* r){
    if (r->hasParam("ssid", true))
      strlcpy(g_cfg.wifiSsid, r->getParam("ssid", true)->value().c_str(), sizeof(g_cfg.wifiSsid));
    if (r->hasParam("pass", true)) {        // blank password = keep the saved one
      String p = r->getParam("pass", true)->value();
      if (p.length()) strlcpy(g_cfg.wifiPass, p.c_str(), sizeof(g_cfg.wifiPass));
    }
    g_cfg.save();
    r->send(200, "application/json", "{\"ok\":true}");
    net_wifiConnect(g_cfg.wifiSsid, g_cfg.wifiPass);
  });

  server.on("/api/test", HTTP_POST, [](AsyncWebServerRequest* r){
    g_cfg.testMode  = ip(r, "tmode",  g_cfg.testMode);
    g_cfg.testLevel = ip(r, "tlevel", g_cfg.testLevel) & 0x0F;
    g_cfg.testIndex = ip(r, "tindex", g_cfg.testIndex);
    g_cfg.outputEnable = ip(r, "oen", g_cfg.outputEnable);
    r->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest* r){
    g_cfg.universe     = ip(r, "uni",   g_cfg.universe);
    g_cfg.startChannel = ip(r, "start", g_cfg.startChannel);
    g_cfg.fixtureCount = constrain(ip(r, "nfix", g_cfg.fixtureCount), 1, 256);
    g_cfg.refreshHz    = constrain(ip(r, "rhz",  g_cfg.refreshHz), 1, 60);
    g_cfg.nibbleSwap   = ip(r, "nswap", g_cfg.nibbleSwap);
    g_cfg.htpMerge     = ip(r, "htp",   g_cfg.htpMerge);
    g_cfg.save();
    r->send(200, "application/json", "{\"ok\":true}");
    // NOTE: universe change should re-join sACN multicast; simplest is a reboot.
  });

  server.begin();
}
