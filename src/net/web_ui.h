#pragma once
#include <pgmspace.h>

// Single-page config UI. Each field's data-k is its path in the /api/config
// JSON; data-t selects how it is read back (num | bool | str).
static const char kWebUiHtml[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>rhino-aprs</title>
<style>
:root{--bg:#111418;--card:#1b2027;--fg:#e6e6e6;--mut:#8a94a3;--acc:#3fa7ff;--bad:#ff6b6b;--ok:#4cd07d}
*{box-sizing:border-box}body{margin:0;font:15px system-ui,sans-serif;background:var(--bg);color:var(--fg)}
header{padding:12px 16px;background:var(--card);display:flex;justify-content:space-between;align-items:center}
header b{font-size:18px}#st{color:var(--mut);font-size:13px;text-align:right}
nav{display:flex;flex-wrap:wrap;gap:4px;padding:8px 12px}
nav button{background:none;border:1px solid #333;color:var(--fg);padding:6px 10px;border-radius:6px}
nav button.on{background:var(--acc);border-color:var(--acc);color:#000}
section{display:none;padding:0 12px 12px}section.on{display:block}
.card{background:var(--card);border-radius:8px;padding:12px;margin-bottom:12px}
label{display:block;margin:8px 0 2px;color:var(--mut);font-size:13px}
input,select{width:100%;padding:8px;border-radius:6px;border:1px solid #333;background:#0d1014;color:var(--fg);font-size:15px}
input[type=checkbox]{width:auto;margin-right:8px}.chk{display:flex;align-items:center;color:var(--fg);margin-top:10px}
.row{display:flex;gap:8px}.row>*{flex:1}
.btn{background:var(--acc);border:0;color:#000;padding:10px 14px;border-radius:6px;font-weight:600;margin-top:10px}
.btn.sec{background:#2a313b;color:var(--fg)}.btn.bad{background:var(--bad)}
#msg{position:fixed;bottom:12px;left:12px;right:12px;padding:10px;border-radius:6px;display:none}
.net{padding:8px;border-bottom:1px solid #2a313b;cursor:pointer}.net:hover{background:#232a33}
small{color:var(--mut)}
</style></head><body>
<header><b>rhino-aprs</b><div id="st">…</div></header>
<nav id="tabs"></nav>

<section data-tab="Station"><div class="card">
<div class="row"><div><label>Callsign</label><input data-k="station.callsign" maxlength="6"></div>
<div><label>SSID</label><select data-k="station.ssid" data-t="num"></select></div></div>
<div class="row"><div><label>Symbol table</label><input data-k="station.symbolTable" maxlength="1"></div>
<div><label>Symbol code</label><input data-k="station.symbolCode" maxlength="1"></div></div>
<small>Examples: / &gt; car, / [ jogger, / - house, / k truck, \ n overlay triangle</small>
<label>Comment</label><input data-k="station.comment" maxlength="43">
<label>Path</label><input data-k="station.path">
</div></section>

<section data-tab="Beacon"><div class="card">
<label>Position source</label><select data-k="beacon.mode"><option value="gps">GPS</option><option value="fixed">Fixed position</option><option value="off">Off</option></select>
<div class="row"><div><label>Fixed latitude</label><input data-k="beacon.lat" data-t="num" type="number" step="any"></div>
<div><label>Fixed longitude</label><input data-k="beacon.lon" data-t="num" type="number" step="any"></div></div>
<label>Fixed altitude (m)</label><input data-k="beacon.altM" data-t="num" type="number">
<label>Interval (s, used when SmartBeaconing is off or for fixed)</label><input data-k="beacon.intervalS" data-t="num" type="number">
</div><div class="card"><div class="chk"><input type="checkbox" data-k="beacon.smart.enabled" data-t="bool">SmartBeaconing</div>
<div class="row"><div><label>Slow rate (s)</label><input data-k="beacon.smart.slowRateS" data-t="num" type="number"></div>
<div><label>Fast rate (s)</label><input data-k="beacon.smart.fastRateS" data-t="num" type="number"></div></div>
<div class="row"><div><label>Slow speed (km/h)</label><input data-k="beacon.smart.slowSpeedKmh" data-t="num" type="number"></div>
<div><label>Fast speed (km/h)</label><input data-k="beacon.smart.fastSpeedKmh" data-t="num" type="number"></div></div>
<div class="row"><div><label>Min turn (°)</label><input data-k="beacon.smart.minTurnDeg" data-t="num" type="number"></div>
<div><label>Turn slope</label><input data-k="beacon.smart.turnSlope" data-t="num" type="number"></div>
<div><label>Min turn time (s)</label><input data-k="beacon.smart.minTurnTimeS" data-t="num" type="number"></div></div>
</div></section>

<section data-tab="APRS-IS"><div class="card">
<div class="chk"><input type="checkbox" data-k="aprsis.enabled" data-t="bool">Connect to APRS-IS when on Wi-Fi</div>
<div class="row"><div><label>Server</label><input data-k="aprsis.server"></div>
<div><label>Port</label><input data-k="aprsis.port" data-t="num" type="number"></div></div>
<label>Passcode (-1 = auto: <span id="cpc"></span>)</label><input data-k="aprsis.passcode" data-t="num" type="number">
<label>Filter</label><input data-k="aprsis.filter">
<div class="chk"><input type="checkbox" data-k="aprsis.rfToIs" data-t="bool">iGate: gate RF packets to APRS-IS</div>
<div class="chk"><input type="checkbox" data-k="aprsis.isToRfMsgs" data-t="bool">iGate: gate APRS-IS messages to RF</div>
<div class="chk"><input type="checkbox" data-k="digi.enabled" data-t="bool">Digipeater (WIDE1-1 fill-in)</div>
</div></section>

<section data-tab="TNC"><div class="card">
<label>Device name</label><input data-k="tnc.name">
<label>BLE address</label><input data-k="tnc.address" placeholder="aa:bb:cc:dd:ee:ff">
<div class="chk"><input type="checkbox" data-k="tnc.autoConnect" data-t="bool">Auto-connect</div>
<small>Defaults work for Mobilinkd TNC3/TNC4 and Vero VR-N76 / VR-N7600.</small>
<label>KISS service UUID</label><input data-k="tnc.serviceUuid">
<label>Write characteristic UUID</label><input data-k="tnc.writeUuid">
<label>Notify characteristic UUID</label><input data-k="tnc.notifyUuid">
</div></section>

<section data-tab="Wi-Fi"><div class="card">
<div id="wst"></div>
<button class="btn sec" onclick="scan(1)">Scan networks</button><div id="nets"></div>
<label>Network (SSID)</label><input data-k="wifi.ssid" id="wssid">
<label>Password <small id="wpw"></small></label><input type="password" id="wpass" autocomplete="new-password">
<label>Hostname</label><input data-k="wifi.hostname">
<small>Once the device joins your network its setup access point shuts down. Reconnect to your network and open http://<span class="hn"></span>.local. If the network is unreachable for 60 s, the setup AP comes back.</small>
</div></section>

<section data-tab="Device"><div class="card">
<label>Units</label><select data-k="device.units"><option value="imperial">Imperial</option><option value="metric">Metric</option></select>
<div class="chk"><input type="checkbox" data-k="device.buzzer" data-t="bool">Buzzer alerts</div>
<label>Brightness (0-255)</label><input data-k="device.brightness" data-t="num" type="number" min="10" max="255">
<label>Timezone (POSIX TZ)</label><input data-k="device.timezone">
<label>Admin password, username "admin" <small id="apw"></small></label><input type="password" id="apass" autocomplete="new-password">
<div class="chk"><input type="checkbox" id="aclr">Remove admin password</div>
</div><div class="card">
<button class="btn sec" onclick="post('/api/reboot','Rebooting…')">Reboot</button>
<button class="btn bad" onclick="confirm('Erase all settings?')&&post('/api/factory-reset','Resetting…')">Factory reset</button>
</div></section>

<div style="padding:0 12px 60px"><button class="btn" onclick="save()">Save</button></div>
<div id="msg"></div>
<script>
const $=s=>document.querySelector(s),$$=s=>[...document.querySelectorAll(s)];
let cfg={};
const secs=$$('section');secs.forEach((s,i)=>{const b=document.createElement('button');b.textContent=s.dataset.tab;
b.onclick=()=>{secs.forEach(x=>x.classList.remove('on'));$$('nav button').forEach(x=>x.classList.remove('on'));s.classList.add('on');b.classList.add('on')};
$('#tabs').appendChild(b);if(!i)b.click()});
for(let i=0;i<16;i++)$('[data-k="station.ssid"]').add(new Option(i?'-'+i:'(none)',i));
const get=(o,p)=>p.split('.').reduce((a,k)=>a==null?a:a[k],o);
const set=(o,p,v)=>{const ks=p.split('.');let a=o;ks.slice(0,-1).forEach(k=>a=a[k]=a[k]||{});a[ks.at(-1)]=v};
function toast(t,bad){const m=$('#msg');m.textContent=t;m.style.background=bad?'var(--bad)':'var(--ok)';m.style.color='#000';m.style.display='block';clearTimeout(m.t);m.t=setTimeout(()=>m.style.display='none',4000)}
async function load(){cfg=await (await fetch('/api/config')).json();
$$('[data-k]').forEach(e=>{const v=get(cfg,e.dataset.k);if(e.type=='checkbox')e.checked=!!v;else e.value=v??''});
$('#cpc').textContent=cfg.aprsis.computedPasscode;$('#wpw').textContent=cfg.wifi.passwordSet?'(saved; leave blank to keep)':'';
$('#apw').textContent=cfg.device.adminPasswordSet?'(set; leave blank to keep)':'(none)';$$('.hn').forEach(e=>e.textContent=cfg.wifi.hostname)}
async function save(){const o={};$$('[data-k]').forEach(e=>{let v=e.type=='checkbox'?e.checked:e.value;
if(e.dataset.t=='num')v=Number(v);set(o,e.dataset.k,v)});
if($('#wpass').value)set(o,'wifi.password',$('#wpass').value);
if($('#apass').value)set(o,'device.adminPassword',$('#apass').value);
if($('#aclr').checked)set(o,'device.clearAdminPassword',true);
const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)});
const j=await r.json();if(!j.ok)return toast(j.error||'Save failed',1);
toast(j.wifiChanged?'Saved. Connecting to Wi-Fi…':'Saved');$('#wpass').value='';$('#apass').value='';$('#aclr').checked=false;load()}
async function post(u,t){toast(t);await fetch(u,{method:'POST'})}
async function scan(start){const j=await (await fetch('/api/wifi/scan'+(start?'?start=1':''))).json();
if(j.scanning){$('#nets').innerHTML='<small>Scanning…</small>';return setTimeout(()=>scan(0),1500)}
$('#nets').innerHTML='';(j.networks||[]).sort((a,b)=>b.rssi-a.rssi).forEach(n=>{const d=document.createElement('div');d.className='net';
d.textContent=n.ssid+'  '+n.rssi+' dBm'+(n.secure?' 🔒':'');d.onclick=()=>{$('#wssid').value=n.ssid;$('#wpass').focus()};$('#nets').appendChild(d)})}
async function status(){try{const s=await (await fetch('/api/status')).json(),w=s.wifi;
$('#st').innerHTML=s.callsign+' · v'+s.version+'<br>'+(w.staConnected?'Wi-Fi '+w.staSsid+' '+w.staIp:'Wi-Fi offline')+(w.apActive?' · AP on':'');
$('#wst').innerHTML=w.staConnected?'<p style="color:var(--ok)">Connected to <b>'+w.staSsid+'</b> at <b>'+w.staIp+'</b> ('+w.rssi+' dBm).'+(w.apActive?' The setup AP will shut down shortly; reconnect to '+w.staSsid+' and open <b>http://'+w.staIp+'</b> or <b>http://'+w.hostname+'.local</b>.':'')+'</p>'
:(w.staConfigured?'<p style="color:var(--bad)">Not connected to '+w.staSsid+' yet…</p>':'<p>No network configured.</p>')}catch(e){$('#st').textContent='offline'}}
load();status();setInterval(status,3000);
</script></body></html>)HTML";
