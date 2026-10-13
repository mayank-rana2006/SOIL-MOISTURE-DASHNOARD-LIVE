// TEST ONLY: sends FAKE readings under sim-01 / sim-02 so you can preview the dashboard without hardware.
// Never run this during real monitoring or lab testing.
// Usage: npm run simulate   (backend must be running)
require('dotenv').config();
const URL = `http://127.0.0.1:${process.env.PORT || 5000}/api/readings`;
const KEY = process.env.API_KEY || 'change-me';
const sites = [
  { deviceId: 'sim-01', location: 'SIMULATED - Foundation trench', base: 28 },
  { deviceId: 'sim-02', location: 'SIMULATED - Embankment fill', base: 18 },
];
let t = 0;
async function tick() {
  for (const s of sites) {
    const moisture = Math.max(2, Math.min(60, s.base + 8 * Math.sin(t / 8) + (Math.random() - 0.5) * 2));
    const body = { deviceId: s.deviceId, location: s.location, raw: Math.round(3200 - moisture * 30), moisture: +moisture.toFixed(1) };
    try {
      await fetch(URL, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-api-key': KEY }, body: JSON.stringify(body) });
      console.log(s.deviceId, body.moisture + '%');
    } catch (e) { console.error('Backend not reachable:', e.message); }
  }
  t++;
}
tick();
setInterval(tick, 3000);
