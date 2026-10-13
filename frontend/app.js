// ---- Settings ----
// Change this to your backend address (use your PC's IP if opening the page from another device)
const API_BASE = 'http://localhost:5000';
const REFRESH_MS = 10000;
const STALE_AFTER_MS = 2 * 60 * 1000;

const $ = (id) => document.getElementById(id);
const state = { devices: [], selected: null, config: { dryBelow: 15, wetAbove: 35 } };

const VERDICTS = {
  dry: 'Too dry for compaction. Add water before placing and compacting fill.',
  workable: 'Moisture is in the workable range for compaction.',
  wet: 'Too wet. Let the soil dry or treat it before placing fill.',
};

async function api(path) {
  const res = await fetch(API_BASE + path);
  if (!res.ok) throw new Error('HTTP ' + res.status);
  return res.json();
}

function ago(date) {
  const s = Math.max(0, Math.round((Date.now() - new Date(date)) / 1000));
  if (s < 60) return s + ' s ago';
  if (s < 3600) return Math.round(s / 60) + ' min ago';
  if (s < 86400) return Math.round(s / 3600) + ' h ago';
  return Math.round(s / 86400) + ' d ago';
}

function renderPoints() {
  const nav = $('points');
  nav.innerHTML = '';
  state.devices.forEach((d) => {
    const b = document.createElement('button');
    b.textContent = d.location || d.deviceId;
    b.setAttribute('aria-pressed', String(d.deviceId === state.selected));
    b.onclick = () => { state.selected = d.deviceId; renderAll(); };
    nav.appendChild(b);
  });
}

function renderReading() {
  const d = state.devices.find((x) => x.deviceId === state.selected);
  const { dryBelow, wetAbove } = state.config;

  $('markDry').style.bottom = dryBelow + '%';
  $('markDryLabel').textContent = 'Dry below ' + dryBelow + '%';
  $('markWet').style.bottom = wetAbove + '%';
  $('markWetLabel').textContent = 'Wet above ' + wetAbove + '%';

  if (!d) return;
  $('where').textContent = d.location + ' (' + d.deviceId + ')';
  $('value').textContent = d.moisture.toFixed(1);
  $('fill').style.height = d.moisture + '%';
  const v = $('verdict');
  v.textContent = VERDICTS[d.status];
  v.className = 'verdict ' + d.status;
  $('meta').textContent = 'Updated ' + ago(d.updatedAt) + (d.raw != null ? ' · raw ADC ' + d.raw : '');

  const stale = Date.now() - new Date(d.updatedAt) > STALE_AFTER_MS;
  const c = $('conn');
  c.className = 'conn ' + (stale ? 'stale' : 'live');
  c.textContent = stale ? 'Sensor not reporting. Last reading ' + ago(d.updatedAt) : 'Sensor live';
}

function renderChart(rows) {
  const svg = $('chart');
  const W = 800, H = 280, L = 44, R = 16, T = 12, B = 30;
  svg.innerHTML = '';
  $('empty').hidden = rows.length > 0;
  if (!rows.length) return;

  const t0 = new Date(rows[0].createdAt).getTime();
  const t1 = new Date(rows[rows.length - 1].createdAt).getTime();
  const span = Math.max(t1 - t0, 1);
  const x = (t) => L + ((t - t0) / span) * (W - L - R);
  const y = (m) => T + (1 - m / 100) * (H - T - B);
  const { dryBelow, wetAbove } = state.config;
  const ns = 'http://www.w3.org/2000/svg';
  const add = (tag, attrs, text) => {
    const el = document.createElementNS(ns, tag);
    Object.entries(attrs).forEach(([k, v]) => el.setAttribute(k, v));
    if (text) el.textContent = text;
    svg.appendChild(el);
  };

  add('rect', { class: 'band', x: L, y: y(wetAbove), width: W - L - R, height: y(dryBelow) - y(wetAbove) });
  [0, 25, 50, 75, 100].forEach((m) => {
    add('line', { class: 'axis', x1: L, x2: W - R, y1: y(m), y2: y(m) });
    add('text', { class: 'tick', x: L - 8, y: y(m) + 4, 'text-anchor': 'end' }, m + '%');
  });
  const fmt = (t) => new Date(t).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
  add('text', { class: 'tick', x: L, y: H - 8 }, fmt(t0));
  add('text', { class: 'tick', x: W - R, y: H - 8, 'text-anchor': 'end' }, fmt(t1));

  const pts = rows.map((r) => x(new Date(r.createdAt).getTime()).toFixed(1) + ',' + y(r.moisture).toFixed(1)).join(' ');
  add('polyline', { class: 'line', points: pts });
}

async function loadHistory() {
  if (!state.selected) return renderChart([]);
  try {
    const rows = await api('/api/readings?deviceId=' + encodeURIComponent(state.selected) + '&hours=' + $('range').value);
    renderChart(rows);
  } catch { renderChart([]); }
}

function renderAll() {
  renderPoints();
  renderReading();
  loadHistory();
}

async function refresh() {
  try {
    state.config = await api('/api/config');
    state.devices = await api('/api/devices');
    if (!state.devices.length) {
      $('conn').className = 'conn stale';
      $('conn').textContent = 'Connected to server. No readings received yet.';
      return renderReading();
    }
    if (!state.selected || !state.devices.some((d) => d.deviceId === state.selected)) state.selected = state.devices[0].deviceId;
    renderAll();
  } catch {
    $('conn').className = 'conn down';
    $('conn').textContent = 'Cannot reach the server at ' + API_BASE + '. Check that the backend is running.';
  }
}

$('range').addEventListener('change', loadHistory);
refresh();
setInterval(refresh, REFRESH_MS);
