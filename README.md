# Site Soil Moisture Monitor (ESP32 + capacitive sensor)

Monitors soil moisture on a construction site (subgrade, embankment, foundation, backfill)
and shows whether the soil is too dry, workable, or too wet for compaction.

```
Soil -> Capacitive sensor -> ESP32 -> Wi-Fi (REST) -> Node/Express -> MongoDB -> Dashboard
```

## Folders
- `firmware/`  Arduino code for the ESP32
- `backend/`   Node.js + Express + MongoDB API
- `frontend/`  Static dashboard (HTML/CSS/JS)

## 1. Backend
```
cd backend
npm install
cp .env.example .env      # set API_KEY, MONGO_URI, DRY_BELOW, WET_ABOVE
npm start
```
Needs MongoDB running locally (or a MongoDB Atlas URI in `.env`).
No hardware yet? Run `npm run simulate` in a second terminal to send fake readings.

## 2. Frontend
Open `frontend/index.html` in a browser (or serve it with `npx serve frontend`).
If the backend is not on `localhost:5000`, edit `API_BASE` at the top of `frontend/app.js`.

## 3. ESP32
1. Wire: sensor VCC -> 3V3, GND -> GND, AOUT -> GPIO34.
2. Open `firmware/esp32_soil_moisture/esp32_soil_moisture.ino` in Arduino IDE (board: ESP32 Dev Module).
3. Set Wi-Fi name/password, `SERVER_URL` (your PC's local IP, same network), and `API_KEY` (same as backend `.env`).
4. Calibrate: note the Serial Monitor `raw` value with the sensor in open air (set `RAW_DRY`) and in water (set `RAW_WET`).
5. Upload. A reading is sent every 30 seconds.

## Moisture bands
Defaults: dry below 15%, wet above 35% (change `DRY_BELOW` / `WET_ABOVE` in `.env`).
For real compaction work, set these around the soil's Optimum Moisture Content from the lab
(Proctor test). Also calibrate the sensor against oven-dried samples of the actual soil, because
capacitive sensors read differently in sand, silt and clay.

## API
- `POST /api/readings` (header `x-api-key`) body `{deviceId, location, raw, moisture}`
- `GET /api/devices` latest reading per monitoring point
- `GET /api/readings?deviceId=&hours=24` history
- `GET /api/config` moisture bands
