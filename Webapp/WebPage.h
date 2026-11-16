/*
 * Project : EscapeRoom-HALLtoRelay (simplified version)
 * File    : WebPage.h - the single-page webapp served by the AtomS3 Lite (Phase 2)
 *
 * One self-contained HTML document (inline CSS and JS, no external resources: the AP has no
 * internet). Kept in flash (PROGMEM) and sent with WebServer::send_P().
 * Layout and style follow the EscapeRoom audio device webapp.
 * UI text is in Portuguese (pt-PT); code comments are in English.
 *
 * API used (all JSON): GET /api/status (includes ap.ssid), GET /api/live,
 *                      POST /api/settings, POST /api/factory, POST /api/open,
 *                      POST /api/relaymode (relay installation: local / remote, reboots)
 */
#pragma once

#include <Arduino.h>

const char WEB_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="pt-PT">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>RSwitchE-Lock</title>
<style>
:root{--bg:#10151c;--card:#1a222d;--line:#2b3644;--txt:#e6edf3;--dim:#8b98a8;--ok:#3fb950;--warn:#d29922;--err:#f85149;--acc:#58a6ff}
*{box-sizing:border-box}
body{margin:0;font:16px/1.4 system-ui,sans-serif;background:var(--bg);color:var(--txt)}
header{display:flex;align-items:center;justify-content:space-between;padding:10px 14px;background:var(--card);border-bottom:1px solid var(--line)}
header h1{font-size:1rem;margin:0}
#conn{font-size:.85rem;color:var(--dim)}
#conn::before{content:"";display:inline-block;width:.7em;height:.7em;border-radius:50%;margin-right:.4em;background:var(--err)}
#conn.on::before{background:var(--ok)}
nav{display:flex;border-bottom:1px solid var(--line)}
nav button{flex:1;padding:12px 4px;background:none;border:0;border-bottom:3px solid transparent;color:var(--dim);font:inherit;cursor:pointer}
nav button.sel{color:var(--txt);border-bottom-color:var(--acc)}
main{padding:12px;max-width:760px;margin:0 auto}
section{display:none}
section.sel{display:block}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px;margin-bottom:12px}
.card h2{font-size:.95rem;margin:0 0 8px;color:var(--dim);font-weight:600}
.row{display:flex;justify-content:space-between;gap:8px;padding:3px 0;border-bottom:1px dashed var(--line)}
.row:last-child{border-bottom:0}
.row span:first-child{color:var(--dim)}
.on-v{color:var(--ok);font-weight:700}
.off-v{color:var(--dim);font-weight:700}
label{display:block;margin:10px 0 2px}
label small{color:var(--dim)}
label.chk{display:flex;align-items:center;gap:10px;margin:14px 0 4px}
label.chk input{width:22px;height:22px}
select,input[type=number]{width:100%;padding:10px;background:var(--bg);color:var(--txt);border:1px solid var(--line);border-radius:6px;font:inherit}
button.act{padding:10px 16px;border:0;border-radius:6px;background:var(--acc);color:#04111f;font:inherit;font-weight:600;cursor:pointer;margin:8px 8px 0 0}
button.act.red{background:var(--err);color:#fff}
button.act:disabled{opacity:.4}
.msg{margin-top:8px;min-height:1.2em}
.msg.err{color:var(--err)}.msg.ok{color:var(--ok)}
.big{font-size:2.6rem;font-weight:700;text-align:center;color:var(--dim)}
.big.on{color:var(--ok)}
.bar{position:relative;height:22px;background:var(--bg);border:1px solid var(--line);border-radius:6px;margin:10px 0;overflow:hidden}
.fill{position:absolute;left:0;top:0;bottom:0;background:var(--acc);width:0}
</style>
</head>
<body>
<header><h1 id="name">RSwitchE-Lock</h1><span id="conn">sem ligação</span></header>
<nav>
<button data-tab="set" class="sel">Ajustes</button>
<button data-tab="cal">Calibração</button>
<button data-tab="dbg">Debugging</button>
</nav>
<main>

<section id="set" class="sel">
<div class="card">
<h2>Parâmetros</h2>
<form id="form">
<label>Número de sensores em uso <small id="l_n"></small><input type="number" id="n" step="1"></label>
<label class="chk"><input type="checkbox" id="inv"> Inverter a lógica dos sensores</label>
<label>Tempo de estabilidade (s) <small id="l_stab"></small><input type="number" id="stab" step="0.1"></label>
<button class="act" type="submit">Guardar</button>
<button class="act red" type="button" id="factory">Repor valores de fábrica</button>
<div class="msg" id="setMsg"></div>
</form>
</div>
<div class="card">
<h2>Instalação</h2>
<form id="rform">
<label>Relé <small>(aplicar reinicia o dispositivo)</small>
<select id="rm"><option value="0">Local (Relay Unit no Mestre)</option><option value="1">Remoto (ESP-NOW)</option></select></label>
<button class="act" type="submit" id="rmApply">Aplicar e reiniciar</button>
<div class="msg" id="rmMsg"></div>
</form>
</div>
</section>

<section id="cal">
<div id="calCards"></div>
<div class="card"><div class="msg" style="color:var(--dim)">ON = íman detetado. OFF = sem íman. Só aparecem os sensores em uso. O jogo continua ativo durante a calibração.</div></div>
</section>

<section id="dbg">
<div class="card"><h2>Sensores</h2><div id="dSensors"></div></div>
<div class="card"><h2>Relé</h2>
<button class="act" id="open">Abrir</button>
<div class="msg" id="openMsg"></div>
<div class="row"><span>Relé</span><span id="dRelay">-</span></div>
</div>
<div class="card" id="linkCard" style="display:none"><h2>Relé remoto</h2>
<div class="row"><span>Ligação ao Remoto</span><span id="kLink">-</span></div>
<div class="row"><span>Relé no Remoto</span><span id="kRelay">-</span></div>
<div class="row"><span>Ordem atual</span><span id="kOrder">-</span></div>
<div class="row"><span>Falhas da ordem</span><span id="kFail">-</span></div>
<div class="row"><span>Último erro</span><span id="kErr">-</span></div>
<div class="row"><span>Arranque do Remoto</span><span id="kBoot">-</span></div>
</div>
<div class="card"><h2>Estado do jogo</h2>
<div class="row"><span>Estado</span><span id="dState">-</span></div>
<div class="row"><span>Combinação em contagem</span><span id="dCount">-</span></div>
<div class="bar"><div class="fill" id="dFill"></div></div>
<div class="row"><span>Tempo / estabilidade</span><span id="dTime">-</span></div>
</div>
<div class="card"><h2>Ponto de acesso</h2>
<div class="row"><span>Clientes ligados</span><span id="apCl">-</span></div>
<div class="row"><span>Tempo desde o arranque</span><span id="up">-</span></div>
</div>
</section>

</main>
<script>
"use strict";
const $ = id => document.getElementById(id);
let tab = "set", lastStatus = null, formDirty = false, formLoaded = false, nUsed = 4, liveBusy = false, rmCur = 0, rmDirty = false, gameSt = 0;

function http(method, url, body) {
  const ctl = new AbortController();
  const timer = setTimeout(() => ctl.abort(), 2500);
  const opt = {method, signal: ctl.signal, cache: "no-store"};
  if (body !== undefined) opt.body = body;
  return fetch(url, opt).then(r => r.json().then(j => ({ok: r.ok, data: j}))).finally(() => clearTimeout(timer));
}
function setConn(on) { const c = $("conn"); c.className = on ? "on" : ""; c.textContent = on ? "ligado" : "sem ligação"; }
function fmtS(ms) { return (ms / 1000).toFixed(1) + " s"; }
function fmtUp(ms) { const s = Math.floor(ms / 1000); return Math.floor(s / 3600) + " h " + Math.floor(s % 3600 / 60) + " min " + s % 60 + " s"; }
function onOff(v) { return v ? "ON" : "OFF"; }

// ---- tabs
document.querySelectorAll("nav button").forEach(b => b.onclick = () => {
  tab = b.dataset.tab;
  document.querySelectorAll("nav button").forEach(x => x.classList.toggle("sel", x === b));
  document.querySelectorAll("section").forEach(s => s.classList.toggle("sel", s.id === tab));
  pollLive();
});

// ---- settings
function fillForm(p, lim) {
  $("n").value = p.n;
  $("inv").checked = p.inv === 1;
  $("stab").value = (p.stab / 1000).toFixed(1);
  $("l_n").textContent = "(" + lim.nMin + " a " + lim.nMax + ")";
  $("l_stab").textContent = "(" + lim.stabMin / 1000 + " a " + lim.stabMax / 1000 + ")";
  formDirty = false;
}
document.querySelectorAll("#form input").forEach(i => i.oninput = () => { formDirty = true; });
$("form").onsubmit = e => {
  e.preventDefault();
  const msg = $("setMsg");
  const n = Math.round($("n").valueAsNumber);
  const stab = Math.round($("stab").valueAsNumber * 1000);
  if (!Number.isFinite(n) || !Number.isFinite(stab)) { msg.className = "msg err"; msg.textContent = "Preencha todos os campos."; return; }
  const body = new URLSearchParams({n: n, inv: $("inv").checked ? 1 : 0, stab: stab});
  http("POST", "/api/settings", body).then(r => {
    if (r.ok && r.data.ok) { msg.className = "msg ok"; msg.textContent = "Guardado."; formDirty = false; setTimeout(pollStatus, 300); }
    else { msg.className = "msg err"; msg.textContent = r.data.error || "Recusado."; }
  }).catch(() => { msg.className = "msg err"; msg.textContent = "Sem ligação ao dispositivo."; });
};
$("factory").onclick = () => {
  if (!confirm("Repor os valores de fábrica? Os ajustes atuais serão perdidos.")) return;
  const msg = $("setMsg");
  http("POST", "/api/factory", "").then(r => {
    if (r.ok && r.data.ok) { msg.className = "msg ok"; msg.textContent = "Valores de fábrica repostos."; formLoaded = false; setTimeout(pollStatus, 300); }
    else { msg.className = "msg err"; msg.textContent = r.data.error || "Recusado."; }
  }).catch(() => { msg.className = "msg err"; msg.textContent = "Sem ligação ao dispositivo."; });
};

// ---- installation (relay mode)
$("rm").onchange = () => { rmDirty = true; };
$("rform").onsubmit = e => {
  e.preventDefault();
  const msg = $("rmMsg");
  const v = parseInt($("rm").value, 10);
  if (v === rmCur) { msg.className = "msg"; msg.textContent = "Sem alterações."; rmDirty = false; return; }
  if (!confirm("O dispositivo vai reiniciar e o ponto de acesso desliga-se; volte a ligá-lo com um toque longo no botão. Continuar?")) return;
  http("POST", "/api/relaymode", new URLSearchParams({rm: v})).then(r => {
    if (r.ok && r.data.ok) { msg.className = "msg ok"; msg.textContent = r.data.changed ? "A reiniciar..." : "Sem alterações."; rmDirty = false; }
    else { msg.className = "msg err"; msg.textContent = r.data.error || "Recusado."; }
  }).catch(() => { msg.className = "msg err"; msg.textContent = "Sem ligação ao dispositivo."; });
};

// ---- status (always polled)
function renderStatus(s) {
  lastStatus = s;
  rmCur = s.rm; gameSt = s.st;
  if (!rmDirty) $("rm").value = s.rm;
  $("rmApply").disabled = s.st !== 0;
  if (s.st !== 0 && !$("rmMsg").textContent) { $("rmMsg").className = "msg"; $("rmMsg").textContent = "Só é possível mudar com o jogo Armado."; }
  if (s.st === 0 && $("rmMsg").textContent === "Só é possível mudar com o jogo Armado.") $("rmMsg").textContent = "";
  $("linkCard").style.display = s.rm === 1 ? "" : "none";
  if (!formLoaded || !formDirty) { if (!formLoaded || tab !== "set") { fillForm(s.p, s.lim); formLoaded = true; } }
  $("name").textContent = s.ap.ssid;
  document.title = s.ap.ssid;
  $("apCl").textContent = s.ap.clients;
  $("up").textContent = fmtUp(s.up);
}
function pollStatus() {
  return http("GET", "/api/status").then(r => { setConn(true); renderStatus(r.data); }).catch(() => setConn(false));
}

// ---- live data (fast poll, only on Calibração and Debugging)
function renderLive(d) {
  nUsed = d.n;
  let cal = "";
  let dbg = "";
  for (let i = 0; i < d.n; i++) {
    const on = d.s[i] === 1;
    cal += '<div class="card"><h2>Sensor ' + (i + 1) + '</h2><div class="big' + (on ? " on" : "") + '">' + onOff(on) +
           '</div><div class="row"><span>Íman</span><span>' + (on ? "detetado" : "não detetado") + '</span></div></div>';
    dbg += '<div class="row"><span>Sensor ' + (i + 1) + '</span><span class="' + (on ? "on-v" : "off-v") + '">' + onOff(on) + '</span></div>';
  }
  $("calCards").innerHTML = cal;
  $("dSensors").innerHTML = dbg;
  $("dRelay").textContent = onOff(d.relay === 1);
  $("dState").textContent = d.state === 0 ? "Armado" : "A aguardar rearme";
  $("dCount").textContent = d.held === 1 ? (d.state === 0 ? "abertura" : "rearme") : "sem contagem";
  const el = d.held === 1 ? d.elapsed : 0;
  $("dFill").style.width = Math.min(100, el / d.stab * 100) + "%";
  $("dTime").textContent = fmtS(el) + " / " + fmtS(d.stab);
  $("open").disabled = d.state !== 0;
  if (rmCur === 1 && d.link) {
    const k = d.link;
    const kinds = ["sem erro", "ordem sem confirmação (5 falhas)", "ordem caducada no Remoto", "Remoto sem pacotes (ordem caducada)"];
    $("kLink").innerHTML = '<span class="' + (k.on ? "on-v" : "off-v") + '">' + (k.on ? "online" : "offline") + "</span>";
    $("kRelay").textContent = onOff(k.rr === 1);
    $("kOrder").textContent = k.ok ? "confirmada" : "por confirmar";
    $("kFail").textContent = k.f;
    $("kErr").textContent = k.e ? kinds[k.e] + ", há " + fmtUp(k.ea) : "sem erro";
    $("kBoot").textContent = k.rb;
  }
}
function pollLive() {
  if ((tab !== "cal" && tab !== "dbg") || document.hidden || liveBusy) return;
  liveBusy = true;
  http("GET", "/api/live").then(r => renderLive(r.data)).catch(() => {}).finally(() => { liveBusy = false; });
}

// ---- open button
$("open").onclick = () => {
  const m = $("openMsg");
  http("POST", "/api/open", "").then(r => {
    m.className = "msg " + (r.ok && r.data.ok ? "ok" : "err");
    m.textContent = r.ok && r.data.ok ? "Pedido enviado." : (r.data.error || "Recusado.");
    setTimeout(pollLive, 300);
  }).catch(() => { m.className = "msg err"; m.textContent = "Sem ligação ao dispositivo."; });
};

pollStatus();
setInterval(pollStatus, 1000);
setInterval(pollLive, 250);
</script>
</body>
</html>
)HTML";
