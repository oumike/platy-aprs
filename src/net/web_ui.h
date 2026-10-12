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
.mono{font:12px ui-monospace,Menlo,monospace;word-break:break-all}
.pk{padding:4px 0;border-bottom:1px solid #2a313b}.pk .t{color:var(--mut);margin-right:6px}
.tx{color:var(--acc)}table{width:100%;border-collapse:collapse;font-size:13px}
td,th{padding:5px 4px;border-bottom:1px solid #2a313b;text-align:left;vertical-align:top}th{color:var(--mut);font-weight:500}
.fl{display:flex;align-items:center;gap:8px;padding:6px 0;border-bottom:1px solid #2a313b}.fl span{flex:1}
.fl .btn{margin:0;padding:6px 10px}
.bub{max-width:80%;padding:6px 10px;border-radius:10px;margin:6px 0;background:#2a313b}.bub.o{margin-left:auto;background:#1d4a73}
.bub small{display:block;font-size:11px}.ur{background:var(--acc);color:#000;border-radius:9px;padding:0 6px;font-size:12px;margin-left:6px}
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
<div id="bst"><small>…</small></div>
<button class="btn sec" onclick="post('/api/beacon','Sending beacon…')">Beacon now</button>
<div class="chk"><input type="checkbox" data-k="gps.enabled" data-t="bool">GPS receiver on</div>
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
<div id="ist"></div>
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
<div id="tst">…</div>
<button class="btn sec" onclick="tscan(1)">Scan for TNCs</button>
<button class="btn sec" onclick="post('/api/tnc/connect','Connecting…')">Connect</button>
<button class="btn sec" onclick="post('/api/tnc/disconnect','Disconnecting…')">Disconnect</button>
<div id="tnets"></div>
<small>Turn on the TNC's Bluetooth and keep it near the device. Devices that advertise the KISS service are listed first.</small>
</div><div class="card">
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
<label>Home screen panel</label><select data-k="device.homePanel"><option value="lastHeard">Last heard station</option><option value="lastMessage">Last received message</option><option value="lastPacket">Last received packet</option><option value="none">Nothing</option></select>
<div class="chk"><input type="checkbox" data-k="device.buzzer" data-t="bool">Buzzer alerts</div>
<label>Brightness (0-255)</label><input data-k="device.brightness" data-t="num" type="number" min="10" max="255">
<label>Timezone (POSIX TZ)</label><input data-k="device.timezone">
<label>Admin password, username "admin" <small id="apw"></small></label><input type="password" id="apass" autocomplete="new-password">
<div class="chk"><input type="checkbox" id="aclr">Remove admin password</div>
</div><div class="card"><b>Backup &amp; restore</b>
<div class="chk"><input type="checkbox" id="bsec">Include passwords (Wi-Fi, admin) in the backup</div>
<button class="btn sec" onclick="bdl()">Download backup</button>
<button class="btn sec" onclick="bsd()">Save backup to SD card</button>
<label>Restore from a backup file</label><input type="file" id="bfile" accept=".json,application/json">
<button class="btn sec" onclick="bup()">Restore from file</button>
<label>Backups on SD card <small id="sdst"></small></label><div id="sdl"></div>
<small>Restoring replaces all settings and reboots. Passwords missing from a backup keep their current values.</small>
</div><div class="card"><b>Packet log</b>
<div class="chk"><input type="checkbox" data-k="log.enabled" data-t="bool">Log packets to the SD card (one file per UTC day)</div>
<div class="chk"><input type="checkbox" data-k="log.includeIs" data-t="bool">Also log packets received from APRS-IS</div>
<div id="lgl"></div>
</div><div class="card">
<button class="btn sec" onclick="post('/api/reboot','Rebooting…')">Reboot</button>
<button class="btn bad" onclick="confirm('Erase all settings?')&&post('/api/factory-reset','Resetting…')">Factory reset</button>
</div></section>

<section data-tab="Messages" data-nosave><div class="card">
<div class="row"><div style="flex:0 0 32%"><label>To</label><input id="mto" maxlength="9" autocapitalize="characters"></div>
<div><label>Message <small id="mlen"></small></label><input id="mtx" maxlength="67"></div></div>
<button class="btn" onclick="msend()">Send</button>
</div><div class="card" id="mconv" style="display:none"><div class="fl"><span><b id="mpeer"></b></span><button class="btn bad" onclick="mdel()">Delete conversation</button></div><div id="mlist"></div></div>
<div class="card"><b>Conversations</b><div id="mth"><small>No messages yet.</small></div></div></section>

<section data-tab="Monitor" data-nosave><div class="card"><div id="mon" class="mono"><small>No packets yet.</small></div></div></section>

<section data-tab="Stations" data-nosave><div class="card"><table><thead><tr><th>Station</th><th>Heard</th><th>Dist</th><th>Info</th></tr></thead><tbody id="stl"></tbody></table></div></section>

<div style="padding:0 12px 60px" id="savebar"><button class="btn" onclick="save()">Save</button></div>
<div id="msg"></div>
<script>
const $=s=>document.querySelector(s),$$=s=>[...document.querySelectorAll(s)];
let cfg={},tab='';
const esc=t=>String(t??'').replace(/[&<>"']/g,c=>'&#'+c.charCodeAt(0)+';');
const age=s=>s<60?s+'s':s<3600?(s/60|0)+'m':s<86400?(s/3600|0)+'h':(s/86400|0)+'d';
const secs=$$('section');secs.forEach((s,i)=>{const b=document.createElement('button');b.textContent=s.dataset.tab;
b.onclick=()=>{secs.forEach(x=>x.classList.remove('on'));$$('nav button').forEach(x=>x.classList.remove('on'));s.classList.add('on');b.classList.add('on');
$('#savebar').style.display=s.hasAttribute('data-nosave')?'none':'';tab=s.dataset.tab;poll()};
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
async function post(u,t){toast(t);const r=await fetch(u,{method:'POST'});const j=await r.json().catch(()=>({}));if(j.ok===false)toast(j.error,1);setTimeout(status,500)}
async function tscan(start){const j=await (await fetch('/api/tnc/scan'+(start?'?start=1':''))).json();const d=$('#tnets');d.innerHTML='';
(j.devices||[]).forEach(n=>{const e=document.createElement('div');e.className='net';e.innerHTML=esc(n.name)+(n.kiss?' <b>KISS</b>':'')+'<br><small>'+esc(n.address)+' · '+n.rssi+' dBm</small>';
e.onclick=async()=>{toast('Selecting '+n.name+'…');const r=await (await fetch('/api/tnc/select',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(n)})).json();
r.ok?(toast('Selected '+n.name+'; connecting'),load()):toast(r.error,1)};d.appendChild(e)});
if(j.scanning){d.insertAdjacentHTML('beforeend','<small>Scanning…</small>');setTimeout(()=>tscan(0),1500)}else if(!(j.devices||[]).length)d.innerHTML='<small>No devices found.</small>'}
let lastSeq=0;
async function monitor(){const j=await (await fetch('/api/packets?since='+lastSeq)).json();if(!j.packets.length)return;
if(!lastSeq)$('#mon').innerHTML='';lastSeq=j.last;
const html=j.packets.map(p=>'<div class="pk'+(p.tx?' tx':'')+'"><span class="t">'+(p.epoch?new Date(p.epoch*1000).toLocaleTimeString():'-'+age(p.ageS))+(p.tx?' TX':'')+(p.is?' IS':'')+'</span>'+esc(p.raw)+'</div>').join('');
$('#mon').insertAdjacentHTML('afterbegin',html);const k=$('#mon').children;while(k.length>200)k[k.length-1].remove()}
async function stations(){const j=await (await fetch('/api/stations')).json();
$('#stl').innerHTML=j.stations.map(s=>'<tr><td><b>'+esc(s.call)+'</b>'+(s.owner?'<br><small>by '+esc(s.owner)+'</small>':'')+'</td><td>'+age(s.ageS)+'<br><small>'+s.packets+' pkt</small></td><td>'+(s.distance?esc(s.distance)+'<br><small>'+s.bearing+'°</small>':'')+'</td><td>'+
[s.speed,s.altitude,s.status,s.comment].filter(x=>x).map(esc).join(' · ')+(s.lat!=null?'<br><small>'+s.lat.toFixed(4)+', '+s.lon.toFixed(4)+(s.path?' via '+esc(s.path):'')+'</small>':'')+'</td></tr>').join('')||'<tr><td colspan=4><small>No stations heard yet.</small></td></tr>'}
let mpeer='',mgen=-1;
const when=m=>m.epoch?new Date(m.epoch*1000).toLocaleString():m.ageS!=null?age(m.ageS)+' ago':'';
const stTxt={pending:'sending…',acked:'✓ delivered',rejected:'✗ rejected',failed:'✗ not delivered (click to retry)'};
async function messages(force){const j=await (await fetch('/api/messages'+(mpeer?'?peer='+encodeURIComponent(mpeer):''))).json();
if(!force&&j.gen==mgen)return;mgen=j.gen;
$('#mth').innerHTML=j.threads.map(t=>'<div class="net" data-p="'+esc(t.peer)+'"><b>'+esc(t.peer)+'</b>'+(t.unread?'<span class="ur">'+t.unread+'</span>':'')+'<br><small>'+(t.lastOut?'You: ':'')+esc(t.lastText)+' · '+when(t)+'</small></div>').join('')||'<small>No messages yet.</small>';
$$('#mth .net').forEach(e=>e.onclick=()=>mopen(e.dataset.p));
$('#mconv').style.display=mpeer?'':'none';$('#mpeer').textContent=mpeer;
if(mpeer){$('#mlist').innerHTML=j.messages.map(m=>'<div class="bub'+(m.out?' o':'')+'" data-s="'+m.seq+'" data-st="'+m.state+'">'+esc(m.text)+'<small>'+when(m)+(m.out?' · '+(stTxt[m.state]||'')+(m.state=='pending'&&m.tries?' (try '+m.tries+')':''):'')+'</small></div>').join('');
$$('#mlist [data-st=failed]').forEach(e=>e.onclick=()=>post('/api/messages/retry?seq='+e.dataset.s,'Retrying…'));
if(j.messages.some(m=>!m.out)&&j.threads.some(t=>t.peer==mpeer&&t.unread))fetch('/api/messages/read?peer='+encodeURIComponent(mpeer),{method:'POST'})}}
function mopen(p){mpeer=p;$('#mto').value=p;messages(1);$('#mtx').focus()}
async function msend(){const to=$('#mto').value.trim().toUpperCase(),text=$('#mtx').value;if(!to||!text)return toast('Enter a callsign and a message',1);
const j=await (await fetch('/api/messages',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({to,text})})).json();
if(!j.ok)return toast(j.error,1);$('#mtx').value='';mpeer=to;messages(1)}
async function mdel(){if(!confirm('Delete the conversation with '+mpeer+'?'))return;await fetch('/api/messages/delete?peer='+encodeURIComponent(mpeer),{method:'POST'});mpeer='';messages(1)}
$('#mtx').addEventListener('input',()=>$('#mlen').textContent=$('#mtx').value.length+'/67');
$('#mtx').addEventListener('keydown',e=>{if(e.key=='Enter')msend()});
async function poll(){try{if(tab=='Messages')await messages();else if(tab=='Monitor')await monitor();else if(tab=='Stations')await stations();else if(tab=='Device'){await sdlist();await loglist()}}catch(e){}}
setInterval(()=>{if(tab=='Monitor'||tab=='Stations'||tab=='Messages')poll()},3000);
const bs=()=>$('#bsec').checked?'1':'0';
function bdl(){location.href='/api/backup?secrets='+bs()}
async function bsd(){toast('Saving to SD…');const j=await (await fetch('/api/backup/sd?secrets='+bs(),{method:'POST'})).json();j.ok?toast('Saved '+j.name):toast(j.error,1);sdlist()}
async function restoreRes(r){const j=await r.json().catch(()=>({ok:false,error:'Restore failed'}));if(!j.ok)return toast(j.error,1);toast('Restored. Rebooting…');setTimeout(()=>location.reload(),8000)}
async function bup(){const f=$('#bfile').files[0];if(!f)return toast('Choose a backup file first',1);
if(!confirm('Replace all settings with '+f.name+' and reboot?'))return;
restoreRes(await fetch('/api/restore',{method:'POST',headers:{'Content-Type':'application/json'},body:await f.text()}))}
async function loglist(){const j=await (await fetch('/api/logs')).json();
$('#lgl').innerHTML=j.files.map(f=>'<div class="fl"><span class="mono">'+esc(f.name)+' <small>'+(f.size/1024).toFixed(1)+' KB</small></span><button class="btn sec" data-a="d">Download</button><button class="btn bad" data-a="x">Delete</button></div>').join('')||'<small>'+(j.mounted?'No logs yet.':'No SD card.')+'</small>';
[...$('#lgl').children].forEach((row,i)=>{const n=encodeURIComponent(j.files[i].name);row.querySelectorAll('button').forEach(b=>b.onclick=async()=>{
if(b.dataset.a=='d')location.href='/api/logs/download?name='+n;
else if(confirm('Delete '+j.files[i].name+'?')){await fetch('/api/logs/delete?name='+n,{method:'POST'});loglist()}})})}
async function sdlist(){const j=await (await fetch('/api/backup/sd')).json();$('#sdst').textContent=j.mounted?'':'(no card)';
$('#sdl').innerHTML=j.files.map(f=>'<div class="fl"><span class="mono">'+esc(f.name)+'</span><button class="btn sec" data-a="r">Restore</button><button class="btn sec" data-a="d">Download</button><button class="btn bad" data-a="x">Delete</button></div>').join('')||(j.mounted?'<small>None yet.</small>':'');
[...$('#sdl').children].forEach((row,i)=>{const n=encodeURIComponent(j.files[i].name);row.querySelectorAll('button').forEach(b=>b.onclick=async()=>{
if(b.dataset.a=='d')location.href='/api/backup/sd/download?name='+n;
else if(b.dataset.a=='r'){if(confirm('Restore '+j.files[i].name+' and reboot?'))restoreRes(await fetch('/api/backup/sd/restore?name='+n,{method:'POST'}))}
else if(confirm('Delete '+j.files[i].name+'?')){await fetch('/api/backup/sd/delete?name='+n,{method:'POST'});sdlist()}})})}
async function scan(start){const j=await (await fetch('/api/wifi/scan'+(start?'?start=1':''))).json();
if(j.scanning){$('#nets').innerHTML='<small>Scanning…</small>';return setTimeout(()=>scan(0),1500)}
$('#nets').innerHTML='';(j.networks||[]).sort((a,b)=>b.rssi-a.rssi).forEach(n=>{const d=document.createElement('div');d.className='net';
d.textContent=n.ssid+'  '+n.rssi+' dBm'+(n.secure?' 🔒':'');d.onclick=()=>{$('#wssid').value=n.ssid;$('#wpass').focus()};$('#nets').appendChild(d)})}
async function status(){try{const s=await (await fetch('/api/status')).json(),w=s.wifi,t=s.tnc,g=s.gps,b=s.beacon,i=s.aprsis;
$('#ist').innerHTML='<p>APRS-IS: <b style="color:'+(i.verified?'var(--ok)':i.connected?'var(--bad)':'var(--mut)')+'">'+esc(i.state)+(i.connected&&!i.verified?' (not verified)':'')+'</b>'+(i.server?' · '+esc(i.server):'')+(i.connected?'<br><small>up '+age(i.connectedForS)+' · RX '+i.rxLines+' · TX '+i.txLines+' · gated RF→IS '+i.gatedRfToIs+' · IS→RF '+i.gatedIsToRf+'</small>':'')+(i.lastError?'<br><small style="color:var(--bad)">'+esc(i.lastError)+'</small>':'')+'</p>';
$$('nav button').forEach(x=>{if(x.textContent.startsWith('Messages'))x.textContent='Messages'+(s.unreadMessages?' ('+s.unreadMessages+')':'')});
$('#bst').innerHTML='<p>GPS: '+(!g.enabled?'off':!g.alive?'<span style="color:var(--bad)">no data</span>':g.fix?'<b style="color:var(--ok)">fix</b>, '+g.sats+' sats · '+g.lat.toFixed(5)+', '+g.lon.toFixed(5)+(g.altM!=null?' · '+Math.round(g.altM)+' m':'')+' · '+Math.round(g.speedKmh)+' km/h':'searching, '+g.sats+' sats')+
'<br>Beacon: <b>'+b.mode+'</b>'+(b.lastAgoS!=null?' · last '+age(b.lastAgoS)+' ago':'')+(b.nextInS!=null?' · next in '+age(b.nextInS):'')+(b.waitingFor?' · <span style="color:var(--mut)">waiting for '+esc(b.waitingFor)+'</span>':'')+(b.lastError?'<br><small style="color:var(--bad)">'+esc(b.lastError)+'</small>':'')+'</p>';
$('#tst').innerHTML='<p>TNC: <b style="color:'+(t.connected?'var(--ok)':'var(--mut)')+'">'+esc(t.state)+'</b>'+(t.name||t.address?' · '+esc(t.name||'')+' <small>'+esc(t.address)+'</small>':'')+
(t.connected?'<br><small>MTU '+t.mtu+' · RX '+t.rxFrames+' · TX '+t.txFrames+'</small>':'')+(t.lastError&&!t.connected?'<br><small>'+esc(t.lastError)+(t.retryInS?', retry in '+t.retryInS+'s':'')+'</small>':'')+'</p>';
$('#st').innerHTML=s.callsign+' · v'+s.version+'<br>'+(w.staConnected?'Wi-Fi '+w.staSsid+' '+w.staIp:'Wi-Fi offline')+(w.apActive?' · AP on':'');
$('#wst').innerHTML=w.staConnected?'<p style="color:var(--ok)">Connected to <b>'+w.staSsid+'</b> at <b>'+w.staIp+'</b> ('+w.rssi+' dBm).'+(w.apActive?' The setup AP will shut down shortly; reconnect to '+w.staSsid+' and open <b>http://'+w.staIp+'</b> or <b>http://'+w.hostname+'.local</b>.':'')+'</p>'
:(w.staConfigured?'<p style="color:var(--bad)">Not connected to '+w.staSsid+' yet…</p>':'<p>No network configured.</p>')}catch(e){$('#st').textContent='offline'}}
load();status();setInterval(status,3000);
</script></body></html>)HTML";
