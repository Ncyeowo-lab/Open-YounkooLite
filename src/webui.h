#pragma once

#include <string>

namespace oy {

inline const std::string& webIndex() {
    static const std::string html = R"OYHTML(<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>openyounkoolite</title>
<style>
:root{color-scheme:dark;--bg:#101114;--surface:#1b1f23;--surface-high:#252a30;--surface-top:#2d333a;--line:#3c4652;--text:#edf0f3;--muted:#aeb7c0;--primary:#d0bcff;--on-primary:#241444;--accent:#78d6c6;--danger:#ffb4ab}
*{box-sizing:border-box}
body{margin:0;font:14px/1.5 "Segoe UI",Arial,sans-serif;background:linear-gradient(180deg,#0e1013,#14171b);color:var(--text)}
header{position:sticky;top:0;z-index:5;display:flex;align-items:center;gap:14px;padding:14px 20px;background:rgba(20,23,27,.92);border-bottom:1px solid var(--line);backdrop-filter:blur(6px)}
header h1{font-size:17px;margin:0;font-weight:650}
.badge{padding:3px 10px;border-radius:999px;font-size:12px;border:1px solid var(--line);background:var(--surface-high);color:var(--muted)}
.badge.ok{color:var(--accent);border-color:rgba(120,214,198,.4)}
main{max-width:1080px;margin:0 auto;padding:20px;display:grid;gap:16px}
.card{background:var(--surface);border:1px solid var(--line);border-radius:10px;padding:16px}
.card h2{margin:0 0 12px;font-size:14px;text-transform:uppercase;letter-spacing:.06em;color:var(--muted)}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(280px,1fr));gap:12px}
.mod{background:var(--surface-high);border:1px solid var(--line);border-radius:9px;padding:12px}
.mod .top{display:flex;align-items:center;justify-content:space-between;gap:8px}
.mod .name{font-weight:600}
.mod .src{font-size:11px;color:var(--muted)}
.switch{position:relative;width:42px;height:22px;flex:0 0 auto}
.switch input{opacity:0;width:0;height:0}
.slider{position:absolute;inset:0;border-radius:999px;background:#39414c;cursor:pointer;transition:.15s}
.slider:before{content:"";position:absolute;width:16px;height:16px;left:3px;top:3px;border-radius:50%;background:#cfd6dd;transition:.15s}
input:checked+.slider{background:var(--primary)}
input:checked+.slider:before{transform:translateX(20px);background:#1c1330}
.vals{margin-top:10px;display:grid;gap:6px;font-size:12px;color:var(--muted)}
button{height:32px;padding:0 14px;border:1px solid var(--primary);border-radius:8px;background:var(--primary);color:var(--on-primary);font-weight:650;cursor:pointer}
button.ghost{background:transparent;color:var(--text);border-color:var(--line)}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
pre{margin:0;max-height:260px;overflow:auto;background:#0d1013;border:1px solid var(--line);border-radius:8px;padding:10px;font-size:12px;color:var(--muted)}
table{width:100%;border-collapse:collapse;font-size:13px}
td,th{padding:6px 8px;border-bottom:1px solid var(--line);text-align:left}
th{color:var(--muted);font-weight:600}
.muted{color:var(--muted)}
</style>
</head>
<body>
<header>
  <h1>openyounkoolite</h1>
  <span class="badge ok">no card-code required</span>
  <span class="badge" id="runtime">runtime: ...</span>
  <span style="flex:1"></span>
  <span class="muted" id="port"></span>
</header>
<main>
  <section class="card">
    <h2>Target</h2>
    <div class="row">
      <button onclick="refreshProcs()">扫描进程</button>
      <button class="ghost" onclick="detach()">断开</button>
      <span class="muted" id="procMsg"></span>
    </div>
    <table id="procs" style="margin-top:10px"></table>
  </section>

  <section class="card">
    <h2>Modules</h2>
    <div class="grid" id="modules"></div>
  </section>

  <section class="card">
    <h2>Lua Scripts</h2>
    <div class="row">
      <button onclick="reloadScripts()">重载脚本</button>
      <span class="muted" id="scriptMsg"></span>
    </div>
  </section>

  <section class="card">
    <h2>Log</h2>
    <pre id="log"></pre>
  </section>
</main>
<script>
const $ = id => document.getElementById(id);
const api = async (p, body) => {
  const o = body === undefined ? {} : {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)};
  const r = await fetch(p, o);
  return r.json();
};

async function refreshProcs(){
  const ps = await (await fetch('/api/processes')).json();
  let h = '<tr><th>PID</th><th>Name</th><th>Title</th><th></th></tr>';
  for (const p of (ps.processes||[])) {
    h += `<tr><td>${p.pid}</td><td>${p.name}</td><td>${esc(p.title)}</td>
          <td><button class="ghost" onclick="attach(${p.pid})">附加</button></td></tr>`;
  }
  $('procs').innerHTML = h;
  $('procMsg').textContent = (ps.processes||[]).length ? '' : '没找到 Minecraft / java 窗口';
}

async function attach(pid){
  const r = await api('/api/attach', {pid});
  $('procMsg').textContent = r.ok ? '已附加 pid ' + pid : (r.reason || '失败');
  refresh();
}
async function detach(){ await api('/api/attach', {pid:0}); refresh(); }

async function reloadScripts(){
  const r = await api('/api/scripts/reload', {});
  $('scriptMsg').textContent = (r.status && r.status.message) || 'done';
  refresh();
}

function esc(s){ return String(s||'').replace(/[&<>"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c])); }

async function toggle(id, on){
  await api('/api/modules/' + encodeURIComponent(id) + '/enabled', {enabled: on});
  refresh();
}

async function refresh(){
  const s = await (await fetch('/api/state')).json();
  const rt = s.runtime || {};
  $('runtime').textContent = 'runtime: ' + (rt.state || '?') + (rt.pid ? ' (pid ' + rt.pid + ')' : '');
  $('port').textContent = 'port ' + (s.port || '');

  let h = '';
  for (const m of (s.modules || [])) {
    h += `<div class="mod">
      <div class="top">
        <div><div class="name">${esc(m.name)}</div><div class="src">${esc(m.id)} · ${esc(m.source)}</div></div>
        <label class="switch"><input type="checkbox" ${m.enabled?'checked':''} onchange="toggle('${m.id}', this.checked)"><span class="slider"></span></label>
      </div><div class="vals">`;
    const vals = m.values || {};
    for (const k of Object.keys(vals)) h += `<div>${esc(k)}: <b>${esc(String(vals[k]))}</b></div>`;
    h += `</div></div>`;
  }
  $('modules').innerHTML = h;

  const sc = s.scripts || {};
  $('scriptMsg').textContent = sc.message || (sc.scriptCount + ' script(s)');

  const logs = (s.logs || []).slice(-40);
  $('log').textContent = logs.join('\n');
}

refresh();
refreshProcs();
setInterval(refresh, 2000);
</script>
</body>
</html>)OYHTML";
    return html;
}

inline const std::string& webRadar() {
    static const std::string html = R"OYHTML(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><title>radar</title>
<style>body{margin:0;background:#0e1013;color:#edf0f3;font:14px/1.5 "Segoe UI",Arial}
canvas{display:block;margin:0 auto}#h{padding:12px 18px;border-bottom:1px solid #3c4652}</style>
</head><body><div id="h">radar · <span class="muted" id="s">loading</span></div>
<canvas id="c" width="600" height="600"></canvas>
<script>
const c=document.getElementById('c'),x=c.getContext('2d');
async function loop(){
  const r=await fetch('/api/radar').then(r=>r.json()).catch(()=>({}));
  const cam=(r.camera)||{}; const ps=(r.players)||[];
  document.getElementById('s').textContent = cam.valid? 'attached':'detached';
  x.clearRect(0,0,600,600); x.fillStyle='#14171b'; x.fillRect(0,0,600,600);
  x.strokeStyle='#3c4652'; for(let i=1;i<4;i++){x.beginPath();x.arc(300,300,i*90,0,7);x.stroke();}
  const yaw=(cam.yaw||0)*Math.PI/180;
  for(const p of ps){
    const dx=(p.x||0)-(cam.x||0), dz=(p.z||0)-(cam.z||0);
    const rx=dx*Math.cos(yaw)-dz*Math.sin(yaw), rz=dx*Math.sin(yaw)+dz*Math.cos(yaw);
    const px=300+rx*6, py=300+rz*6;
    x.fillStyle='#78d6c6'; x.beginPath(); x.arc(px,py,4,0,7); x.fill();
    x.fillStyle='#aeb7c0'; x.fillText(p.name||'', px+7, py+4);
  }
  setTimeout(loop,200);
}
loop();
</script></body></html>)OYHTML";
    return html;
}

}
