// =====================================================================
//  DASHBOARD PAGE - served by the ESP32 itself at "/"
//  Plain HTML + CSS + vanilla JS, no external libraries (device has no
//  guaranteed internet access, only the local WiFi network).
//
//  Two data sources on this page:
//   1. LIVE fields (balance, power, relay...) - fetched every 2s from
//      this device's own /api/status, /api/relay, /api/topup endpoints.
//   2. REFERENCE model results - baked in below from meter_results.json,
//      produced by the Colab notebook trained on the public UCI household
//      dataset. These numbers do not change at runtime; they are here so
//      the dashboard can show the model's accuracy and 30-day forecast
//      without needing internet access or a second server. Swap this
//      block for your own logged-data results once you have them.
// =====================================================================
#pragma once

const char DASHBOARD_HTML[] = R"HTMLPAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>Smart Prepaid Meter</title>
<style>
  :root{
    color-scheme: light;
    --surface-1:#fcfcfb; --page:#f9f9f7;
    --text-primary:#0b0b0b; --text-secondary:#52514e; --text-muted:#898781;
    --grid:#e1e0d9; --axis:#c3c2b7; --border:rgba(11,11,11,0.10);
    --series-1:#2a78d6; --series-2:#eb6834; --series-3:#1baf7a;
    --good:#0ca30c; --warning:#fab219; --critical:#d03b3b;
    --good-text:#006300;
    padding-top: env(safe-area-inset-top, 0px);
    padding-bottom: env(safe-area-inset-bottom, 0px);
  }
  @media (prefers-color-scheme: dark){
    :root:not([data-theme="light"]){
      color-scheme: dark;
      --surface-1:#1a1a19; --page:#0d0d0d;
      --text-primary:#ffffff; --text-secondary:#c3c2b7; --text-muted:#898781;
      --grid:#2c2c2a; --axis:#383835; --border:rgba(255,255,255,0.10);
      --series-1:#3987e5; --series-2:#d95926; --series-3:#199e70;
      --good:#0ca30c; --warning:#fab219; --critical:#e66767;
      --good-text:#0ca30c;
    }
  }
  :root[data-theme="dark"]{
    color-scheme: dark;
    --surface-1:#1a1a19; --page:#0d0d0d;
    --text-primary:#ffffff; --text-secondary:#c3c2b7; --text-muted:#898781;
    --grid:#2c2c2a; --axis:#383835; --border:rgba(255,255,255,0.10);
    --series-1:#3987e5; --series-2:#d95926; --series-3:#199e70;
    --good:#0ca30c; --warning:#fab219; --critical:#e66767;
    --good-text:#0ca30c;
  }
  *{box-sizing:border-box;}
  body{margin:0;background:var(--page);color:var(--text-primary);
       font-family:system-ui,-apple-system,"Segoe UI",sans-serif;font-size:14px;}
  .wrap{max-width:900px;margin:0 auto;padding:16px 16px 40px;}
  header{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:16px;flex-wrap:wrap;}
  h1{font-size:18px;margin:0;}
  .sub{color:var(--text-secondary);font-size:13px;margin-top:2px;}
  .conn{display:flex;align-items:center;gap:6px;font-size:12px;color:var(--text-secondary);}
  .dot{width:8px;height:8px;border-radius:50%;background:var(--text-muted);}
  .dot.ok{background:var(--good);}
  .dot.bad{background:var(--critical);}
  section{background:var(--surface-1);border:1px solid var(--border);border-radius:10px;
          padding:16px;margin-bottom:16px;}
  section h2{font-size:14px;margin:0 0 12px;color:var(--text-secondary);
             text-transform:uppercase;letter-spacing:.04em;}
  .tiles{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:10px;}
  .tile{border:1px solid var(--border);border-radius:8px;padding:10px 12px;}
  .tile .label{font-size:12px;color:var(--text-secondary);}
  .tile .value{font-size:22px;font-weight:600;margin-top:2px;}
  .tile .value small{font-size:12px;font-weight:400;color:var(--text-secondary);}
  .banner{display:flex;align-items:center;gap:8px;padding:10px 12px;border-radius:8px;
          font-weight:600;margin-top:12px;}
  .banner.good{background:color-mix(in srgb, var(--good) 14%, transparent);color:var(--good-text);}
  .banner.warn{background:color-mix(in srgb, var(--warning) 16%, transparent);color:#7a5400;}
  .banner.bad{background:color-mix(in srgb, var(--critical) 14%, transparent);color:var(--critical);}
  .banner .ico{font-size:16px;}
  .row{display:flex;gap:16px;flex-wrap:wrap;align-items:flex-start;}
  .controls-col{flex:1;min-width:220px;}
  .ctrl-row{display:flex;align-items:center;gap:10px;margin-bottom:10px;}
  .switch{position:relative;width:46px;height:26px;flex:none;}
  .switch input{opacity:0;width:0;height:0;}
  .slider{position:absolute;inset:0;background:var(--axis);border-radius:999px;cursor:pointer;transition:.15s;}
  .slider:before{content:"";position:absolute;width:20px;height:20px;left:3px;top:3px;
                 background:#fff;border-radius:50%;transition:.15s;}
  input:checked + .slider{background:var(--good);}
  input:checked + .slider:before{transform:translateX(20px);}
  input:disabled + .slider{opacity:.4;cursor:not-allowed;}
  .ctrl-label{font-size:13px;}
  .ctrl-hint{font-size:12px;color:var(--text-muted);margin-top:-4px;margin-bottom:10px;}
  input[type=text],input[type=number],select{
    background:var(--page);color:var(--text-primary);border:1px solid var(--border);
    border-radius:6px;padding:6px 8px;font-size:14px;width:100%;
  }
  button{background:var(--series-1);color:#fff;border:none;border-radius:6px;
         padding:7px 14px;font-size:13px;font-weight:600;cursor:pointer;}
  button:disabled{opacity:.5;cursor:not-allowed;}
  button.ghost{background:transparent;border:1px solid var(--border);color:var(--text-secondary);}
  .msg{font-size:12px;margin-top:6px;}
  .msg.ok{color:var(--good-text);}
  .msg.bad{color:var(--critical);}
  .legend{display:flex;gap:14px;flex-wrap:wrap;font-size:12px;color:var(--text-secondary);margin-top:8px;}
  .legend .sw{display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:5px;vertical-align:-1px;}
  table{width:100%;border-collapse:collapse;font-size:12px;margin-top:10px;}
  table th,table td{text-align:left;padding:5px 8px;border-bottom:1px solid var(--grid);}
  table th{color:var(--text-secondary);font-weight:600;}
  td.num,th.num{text-align:right;font-variant-numeric:tabular-nums;}
  .bar-row{display:flex;align-items:center;gap:8px;margin-bottom:8px;}
  .bar-name{width:120px;font-size:12px;color:var(--text-secondary);flex:none;}
  .bar-track{flex:1;background:var(--grid);border-radius:4px;height:16px;position:relative;}
  .bar-fill{height:100%;border-radius:4px;}
  .bar-val{width:70px;flex:none;font-size:12px;text-align:right;font-variant-numeric:tabular-nums;}
  .best-tag{display:inline-flex;align-items:center;gap:4px;font-size:11px;color:var(--good-text);margin-left:6px;}
  .note{font-size:12px;color:var(--text-muted);line-height:1.5;margin-top:10px;}
  .calc-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(60px,1fr));gap:6px;margin-bottom:10px;}
  .calc-grid label{font-size:11px;color:var(--text-muted);display:block;margin-bottom:2px;}
  .toggle-link{font-size:12px;color:var(--series-1);cursor:pointer;text-decoration:underline;background:none;border:none;padding:0;}
  #fcTooltip{position:absolute;pointer-events:none;background:var(--surface-1);border:1px solid var(--border);
             border-radius:6px;padding:5px 8px;font-size:12px;box-shadow:0 2px 8px rgba(0,0,0,.15);
             display:none;white-space:nowrap;}
  .chart-wrap{position:relative;}
  footer{color:var(--text-muted);font-size:11px;text-align:center;margin-top:8px;}
</style>
</head>
<body>
<div class="wrap">
  <header>
    <div>
      <h1>Smart Prepaid Meter</h1>
      <div class="sub">Live device status + reference prediction model</div>
    </div>
    <div class="conn"><span class="dot" id="connDot"></span><span id="connText">connecting…</span></div>
  </header>

  <section>
    <h2>Live meter</h2>
    <div class="tiles">
      <div class="tile"><div class="label">Balance</div><div class="value" id="tBalance">–<small> kWh</small></div></div>
      <div class="tile"><div class="label">Days left</div><div class="value" id="tDays">–</div></div>
      <div class="tile"><div class="label">Power draw</div><div class="value" id="tPower">–<small> W</small></div></div>
      <div class="tile"><div class="label">Load</div><div class="value" id="tLoad" style="font-size:15px;">–</div></div>
    </div>
    <div class="banner" id="powerBanner"><span class="ico">●</span><span id="powerBannerText">Reading status…</span></div>
    <div class="note" id="topupHint"></div>
  </section>

  <section>
    <h2>Controls</h2>
    <div class="row">
      <div class="controls-col">
        <div class="ctrl-row">
          <label class="switch">
            <input type="checkbox" id="relaySwitch">
            <span class="slider"></span>
          </label>
          <span class="ctrl-label" id="relayLabel">Load power</span>
        </div>
        <div class="ctrl-hint" id="relayHint">Manual on/off for testing. Disabled while balance is zero.</div>
      </div>
      <div class="controls-col">
        <div class="ctrl-row" style="align-items:stretch;">
          <input type="text" id="tokenInput" placeholder="Enter token, e.g. 1245" maxlength="8">
          <button id="topupBtn" style="white-space:nowrap;">Top up</button>
        </div>
        <div class="msg" id="topupMsg"></div>
      </div>
    </div>
  </section>

  <section>
    <h2>Prediction model — reference (public dataset)</h2>
    <div class="note">
      Trained in the Colab notebook on the UCI "Individual household electric power consumption" dataset,
      not on this board's own readings. It stands in until you log and train on real meter data — the method
      and formula are identical either way. Average usage in that dataset: <strong id="refAvg"></strong> kWh/day.
    </div>

    <div style="margin-top:14px;" id="barChart"></div>
    <button class="toggle-link" id="toggleMetricsTable">Show full accuracy table ▾</button>
    <div id="metricsTableWrap" style="display:none;"></div>

    <div class="chart-wrap" style="margin-top:18px;">
      <div id="forecastChart"></div>
      <div id="fcTooltip"></div>
    </div>
    <button class="toggle-link" id="toggleForecastTable">Show forecast as table ▾</button>
    <div id="forecastTableWrap" style="display:none;"></div>

    <div class="note" id="refScenario"></div>
  </section>

  <section>
    <h2>Try the model</h2>
    <div class="note">Enter the last 7 days of usage (kWh), pick a month, and see what the trained
      Linear Regression predicts for tomorrow. Prefilled with the dataset's own last 7 days.</div>
    <div class="calc-grid" id="calcInputs"></div>
    <div class="ctrl-row">
      <select id="calcMonth"></select>
      <label style="display:flex;align-items:center;gap:6px;font-size:13px;">
        <input type="checkbox" id="calcWeekend" style="width:auto;"> Weekend
      </label>
      <button id="calcBtn">Predict</button>
    </div>
    <div class="tile" style="max-width:220px;">
      <div class="label">Predicted usage, next day</div>
      <div class="value" id="calcResult">– <small>kWh</small></div>
    </div>
  </section>

  <footer>Omsai_ESP32</footer>
</div>

<script>
// ---------- Reference model results (from meter_results.json) ----------
const REF = {
  source: "UCI Individual Household Electric Power Consumption",
  generated: "2026-10-10 14:48",
  average_kwh_per_day: 26.19,
  best_model: "7-day average",
  metrics: [
    {model:"7-day average", MAE:4.18, RMSE:5.72, MAPE:18.7},
    {model:"Linear Regression", MAE:4.28, RMSE:5.75, MAPE:19.9},
    {model:"Random Forest", MAE:4.19, RMSE:5.59, MAPE:19.0}
  ],
  meter_scenario: {balance_kwh:100.0, days_left:3.2, kwh_needed:988.0, topup_nad:1775.97, cost_per_kwh:2.0},
  forecast: [
    ["2010-11-27",33.47],["2010-11-28",32.27],["2010-11-29",29.27],["2010-11-30",29.03],
    ["2010-12-01",30.13],["2010-12-02",29.57],["2010-12-03",29.72],["2010-12-04",35.01],
    ["2010-12-05",36.23],["2010-12-06",31.55],["2010-12-07",31.36],["2010-12-08",31.92],
    ["2010-12-09",31.21],["2010-12-10",31.03],["2010-12-11",36.14],["2010-12-12",37.41],
    ["2010-12-13",32.52],["2010-12-14",32.2],["2010-12-15",32.67],["2010-12-16",31.87],
    ["2010-12-17",31.6],["2010-12-18",36.64],["2010-12-19",37.86],["2010-12-20",32.91],
    ["2010-12-21",32.54],["2010-12-22",32.97],["2010-12-23",32.14],["2010-12-24",31.83],
    ["2010-12-25",36.85],["2010-12-26",38.04]
  ],
  linear_model: {
    intercept: 7.517878695561066,
    w: {lag1:0.25358047959327723, lag2:-0.05255800422578466, lag3:0.18896834601943435,
        lag7:0.048206746455281134, mean7:0.22272584000556855, weekend:4.717274988867454,
        msin:1.2156802371393098, mcos:2.6930211438198364}
  },
  last7_seed: [28.28, 23.85, 29.94, 26.29, 34.03, 15.02, 36.62] // most recent day first
};

const COLORS = {s1:"var(--series-1)", s2:"var(--series-2)", s3:"var(--series-3)"};

function predictDailyKWh(last7, month, weekend){
  const w = REF.linear_model.w;
  const mean7 = last7.reduce((a,b)=>a+b,0)/7;
  const rad = 2*Math.PI*month/12;
  let p = REF.linear_model.intercept
        + w.lag1*last7[0] + w.lag2*last7[1] + w.lag3*last7[2] + w.lag7*last7[6]
        + w.mean7*mean7 + w.weekend*(weekend?1:0)
        + w.msin*Math.sin(rad) + w.mcos*Math.cos(rad);
  return Math.max(0, p);
}

// ---------- Live polling ----------
let lastOkTime = 0;
async function pollStatus(){
  try{
    const res = await fetch('/api/status', {cache:'no-store'});
    if(!res.ok) throw new Error('bad status');
    const d = await res.json();
    lastOkTime = Date.now();
    setConn(true);
    renderLive(d);
  }catch(e){
    setConn(false);
  }
}
function setConn(ok){
  document.getElementById('connDot').className = 'dot ' + (ok?'ok':'bad');
  document.getElementById('connText').textContent = ok ? 'connected' : 'offline — retrying…';
}
function renderLive(d){
  document.getElementById('tBalance').innerHTML = d.balance.toFixed(2) + ' <small>kWh</small>';
  document.getElementById('tDays').textContent = d.daysRemaining.toFixed(1);
  document.getElementById('tPower').innerHTML = d.watts.toFixed(1) + ' <small>W</small>';
  document.getElementById('tLoad').textContent = d.loadStatus;

  const banner = document.getElementById('powerBanner');
  const text = document.getElementById('powerBannerText');
  banner.classList.remove('good','warn','bad');
  if(d.powerCutOff){
    banner.classList.add('bad');
    text.textContent = 'Power CUT — balance is zero. Top up to restore.';
  } else if (d.balance < 2.0){
    banner.classList.add('warn');
    text.textContent = 'Power ON — low balance, top up soon.';
  } else {
    banner.classList.add('good');
    text.textContent = 'Power ON — relay closed.';
  }
  document.getElementById('topupHint').textContent =
    'Recommended top-up to reach ' + d.targetDays + ' days: N$ ' + d.nadToTopUp.toFixed(2) +
    ' (at N$' + d.costPerKWh.toFixed(2) + '/kWh, ' + d.dailyAvgUsage.toFixed(2) + ' kWh/day assumed).';

  const sw = document.getElementById('relaySwitch');
  if(document.activeElement !== sw){ sw.checked = d.relayIsOn; }
  sw.disabled = d.powerCutOff;
  document.getElementById('relayHint').textContent = d.powerCutOff
    ? 'Disabled — balance is zero. Top up first.'
    : 'Manual on/off for testing, independent of the keypad.';
}

document.getElementById('relaySwitch').addEventListener('change', async (e)=>{
  const wantOn = e.target.checked;
  e.target.disabled = true;
  try{
    const res = await fetch('/api/relay', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body: 'cut=' + (wantOn ? '0' : '1')});
    const d = await res.json();
    if(!d.success){ alert(d.message); }
  }catch(err){}
  pollStatus();
});

document.getElementById('topupBtn').addEventListener('click', async ()=>{
  const token = document.getElementById('tokenInput').value.trim();
  const msg = document.getElementById('topupMsg');
  if(!token){ msg.className='msg bad'; msg.textContent='Enter a token first.'; return; }
  try{
    const res = await fetch('/api/topup', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body: 'token=' + encodeURIComponent(token)});
    const d = await res.json();
    msg.className = 'msg ' + (d.success ? 'ok' : 'bad');
    msg.textContent = d.message + (d.success ? (' — balance now ' + d.balance.toFixed(2) + ' kWh') : '');
    if(d.success) document.getElementById('tokenInput').value = '';
  }catch(err){
    msg.className='msg bad'; msg.textContent='Could not reach the meter.';
  }
  pollStatus();
});

setInterval(pollStatus, 2000);
pollStatus();

// ---------- Accuracy bar chart ----------
function renderBarChart(){
  const el = document.getElementById('barChart');
  const maxMAE = Math.max(...REF.metrics.map(m=>m.MAE)) * 1.15;
  const colors = [COLORS.s1, COLORS.s2, COLORS.s3];
  el.innerHTML = REF.metrics.map((m,i)=>{
    const pct = (m.MAE/maxMAE*100).toFixed(1);
    const best = m.model === REF.best_model ? '<span class="best-tag">✓ best</span>' : '';
    return `<div class="bar-row" title="${m.model}: MAE ${m.MAE} kWh, RMSE ${m.RMSE} kWh, MAPE ${m.MAPE}%">
      <div class="bar-name">${m.model}${best}</div>
      <div class="bar-track"><div class="bar-fill" style="width:${pct}%;background:${colors[i]};"></div></div>
      <div class="bar-val">${m.MAE.toFixed(2)} kWh</div>
    </div>`;
  }).join('');
  el.insertAdjacentHTML('beforeend',
    '<div class="legend">Bars show mean absolute error (lower is better) in predicted kWh/day.</div>');

  const tableWrap = document.getElementById('metricsTableWrap');
  tableWrap.innerHTML = `<table><thead><tr><th>Model</th><th class="num">MAE (kWh)</th>
    <th class="num">RMSE (kWh)</th><th class="num">MAPE (%)</th></tr></thead><tbody>
    ${REF.metrics.map(m=>`<tr><td>${m.model}${m.model===REF.best_model?' ✓':''}</td>
      <td class="num">${m.MAE.toFixed(2)}</td><td class="num">${m.RMSE.toFixed(2)}</td>
      <td class="num">${m.MAPE.toFixed(1)}</td></tr>`).join('')}</tbody></table>`;
}
document.getElementById('toggleMetricsTable').addEventListener('click', (e)=>{
  const w = document.getElementById('metricsTableWrap');
  const show = w.style.display === 'none';
  w.style.display = show ? 'block' : 'none';
  e.target.textContent = (show ? 'Hide full accuracy table ▴' : 'Show full accuracy table ▾');
});

// ---------- Forecast line chart (hand-rolled SVG, crosshair + tooltip) ----------
function renderForecastChart(){
  const data = REF.forecast;
  const W = 640, H = 220, padL = 40, padR = 12, padT = 16, padB = 28;
  const xs = data.map((_,i)=>i);
  const ys = data.map(d=>d[1]);
  const yMin = Math.min(...ys) * 0.95, yMax = Math.max(...ys) * 1.05;
  const x = i => padL + i/(data.length-1) * (W-padL-padR);
  const y = v => padT + (1 - (v-yMin)/(yMax-yMin)) * (H-padT-padB);

  let gridLines = '';
  for(let g=0; g<=4; g++){
    const v = yMin + g/4*(yMax-yMin);
    const yy = y(v);
    gridLines += `<line x1="${padL}" y1="${yy}" x2="${W-padR}" y2="${yy}" stroke="var(--grid)" stroke-width="1"/>
      <text x="${padL-6}" y="${yy+4}" text-anchor="end" font-size="10" fill="var(--text-muted)">${v.toFixed(0)}</text>`;
  }
  const path = data.map((d,i)=> (i===0?'M':'L') + x(i).toFixed(1) + ',' + y(d[1]).toFixed(1)).join(' ');
  const areaPath = path + ` L${x(data.length-1).toFixed(1)},${y(yMin).toFixed(1)} L${x(0).toFixed(1)},${y(yMin).toFixed(1)} Z`;

  // sparse x labels (every ~6th point)
  let xLabels = '';
  data.forEach((d,i)=>{
    if(i % 6 === 0 || i === data.length-1){
      xLabels += `<text x="${x(i).toFixed(1)}" y="${H-8}" text-anchor="middle" font-size="10" fill="var(--text-muted)">${d[0].slice(5)}</text>`;
    }
  });

  const svg = `<svg viewBox="0 0 ${W} ${H}" width="100%" style="max-width:${W}px;display:block;" id="fcSvg">
    ${gridLines}
    <path d="${areaPath}" fill="var(--series-1)" opacity="0.08" stroke="none"/>
    <path d="${path}" fill="none" stroke="var(--series-1)" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
    <line id="fcCrosshair" x1="0" y1="${padT}" x2="0" y2="${H-padB}" stroke="var(--axis)" stroke-width="1" style="display:none;"/>
    <circle id="fcDot" r="4" fill="var(--series-1)" stroke="var(--surface-1)" stroke-width="2" style="display:none;"/>
    ${xLabels}
    <rect x="${padL}" y="${padT}" width="${W-padL-padR}" height="${H-padT-padB}" fill="transparent" id="fcHit"/>
  </svg>`;
  document.getElementById('forecastChart').innerHTML =
    '<div class="legend" style="margin-bottom:6px;"><span class="sw" style="background:var(--series-1);"></span>Predicted daily usage (Linear Regression), next 30 days</div>' + svg;

  const svgEl = document.getElementById('fcSvg');
  const hit = document.getElementById('fcHit');
  const crosshair = document.getElementById('fcCrosshair');
  const dot = document.getElementById('fcDot');
  const tip = document.getElementById('fcTooltip');

  function moveTo(evt){
    const rect = svgEl.getBoundingClientRect();
    const clientX = evt.touches ? evt.touches[0].clientX : evt.clientX;
    const px = (clientX - rect.left) / rect.width * W;
    let i = Math.round((px - padL) / (W-padL-padR) * (data.length-1));
    i = Math.max(0, Math.min(data.length-1, i));
    const cx = x(i), cy = y(data[i][1]);
    crosshair.setAttribute('x1', cx); crosshair.setAttribute('x2', cx);
    crosshair.style.display = 'block';
    dot.setAttribute('cx', cx); dot.setAttribute('cy', cy); dot.style.display = 'block';
    tip.style.display = 'block';
    tip.style.left = Math.min(rect.width-90, Math.max(0, px*(rect.width/W)-40)) + 'px';
    tip.style.top = (cy - 38) + 'px';
    tip.textContent = data[i][0] + ' · ' + data[i][1].toFixed(1) + ' kWh';
  }
  hit.addEventListener('mousemove', moveTo);
  hit.addEventListener('touchmove', moveTo);
  hit.addEventListener('mouseleave', ()=>{ crosshair.style.display='none'; dot.style.display='none'; tip.style.display='none'; });

  const tableWrap = document.getElementById('forecastTableWrap');
  tableWrap.innerHTML = '<table><thead><tr><th>Date</th><th class="num">Predicted kWh</th></tr></thead><tbody>' +
    data.map(d=>`<tr><td>${d[0]}</td><td class="num">${d[1].toFixed(2)}</td></tr>`).join('') + '</tbody></table>';
}
document.getElementById('toggleForecastTable').addEventListener('click', (e)=>{
  const w = document.getElementById('forecastTableWrap');
  const show = w.style.display === 'none';
  w.style.display = show ? 'block' : 'none';
  e.target.textContent = (show ? 'Hide forecast table ▴' : 'Show forecast as table ▾');
});

// ---------- Calculator ----------
function renderCalc(){
  const grid = document.getElementById('calcInputs');
  const labels = ['Yesterday','2 days ago','3 days ago','4 days ago','5 days ago','6 days ago','7 days ago'];
  grid.innerHTML = REF.last7_seed.map((v,i)=>
    `<div><label>${labels[i]}</label><input type="number" step="0.1" value="${v}" id="lag${i}"></div>`).join('');
  const monthSel = document.getElementById('calcMonth');
  const names = ['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'];
  monthSel.innerHTML = names.map((n,i)=>`<option value="${i+1}" ${i===10?'selected':''}>${n}</option>`).join('');
}
document.getElementById('calcBtn').addEventListener('click', ()=>{
  const last7 = REF.last7_seed.map((_,i)=> parseFloat(document.getElementById('lag'+i).value) || 0);
  const month = parseInt(document.getElementById('calcMonth').value, 10);
  const weekend = document.getElementById('calcWeekend').checked;
  const p = predictDailyKWh(last7, month, weekend);
  document.getElementById('calcResult').innerHTML = p.toFixed(2) + ' <small>kWh</small>';
});

// ---------- static reference content ----------
document.getElementById('refAvg').textContent = REF.average_kwh_per_day.toFixed(2);
document.getElementById('refScenario').innerHTML =
  'If a full household on this public dataset held the same ' + REF.meter_scenario.balance_kwh.toFixed(0) +
  ' kWh balance as this demo\'s starting point: about <strong>' + REF.meter_scenario.days_left.toFixed(1) +
  ' days left</strong>, needing roughly <strong>N$ ' + REF.meter_scenario.topup_nad.toFixed(2) +
  '</strong> to reach a 30-day target. That scale does not match this board\'s small LED loads — it is shown '+
  'only to illustrate what the model produces on real household consumption.';

renderBarChart();
renderForecastChart();
renderCalc();
</script>
</body>
</html>
)HTMLPAGE";
