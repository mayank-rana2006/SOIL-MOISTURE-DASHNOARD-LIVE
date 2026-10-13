require('dotenv').config();
const express = require('express');
const cors = require('cors');
const mongoose = require('mongoose');

const PORT = process.env.PORT || 5000;
const MONGO_URI = process.env.MONGO_URI || 'mongodb://127.0.0.1:27017/soil_moisture';
const API_KEY = process.env.API_KEY || 'change-me';
const DRY_BELOW = Number(process.env.DRY_BELOW || 15);
const WET_ABOVE = Number(process.env.WET_ABOVE || 35);

const readingSchema = new mongoose.Schema({
  deviceId: { type: String, required: true, index: true },
  location: { type: String, default: 'Site 1' },
  raw: Number,
  moisture: { type: Number, required: true, min: 0, max: 100 },
  createdAt: { type: Date, default: Date.now, index: true },
});
const Reading = mongoose.model('Reading', readingSchema);

function statusFor(moisture) {
  if (moisture < DRY_BELOW) return 'dry';
  if (moisture > WET_ABOVE) return 'wet';
  return 'workable';
}

const app = express();
app.use(cors());
app.use(express.json());

// Only the device endpoint needs the key; the dashboard is read-only.
function requireKey(req, res, next) {
  if (req.get('x-api-key') !== API_KEY) return res.status(401).json({ error: 'Invalid API key' });
  next();
}

app.get('/api/config', (_req, res) => res.json({ dryBelow: DRY_BELOW, wetAbove: WET_ABOVE }));

// ESP32 posts here
app.post('/api/readings', requireKey, async (req, res, next) => {
  try {
    const { deviceId, location, raw, moisture } = req.body || {};
    if (!deviceId || typeof moisture !== 'number' || moisture < 0 || moisture > 100) {
      return res.status(400).json({ error: 'deviceId and moisture (0-100) are required' });
    }
    const doc = await Reading.create({ deviceId, location, raw, moisture });
    res.status(201).json({ ...doc.toObject(), status: statusFor(moisture) });
  } catch (e) { next(e); }
});

// Latest reading per device (one entry per monitoring point)
app.get('/api/devices', async (_req, res, next) => {
  try {
    const rows = await Reading.aggregate([
      { $sort: { createdAt: -1 } },
      {
        $group: {
          _id: '$deviceId',
          location: { $first: '$location' },
          moisture: { $first: '$moisture' },
          raw: { $first: '$raw' },
          updatedAt: { $first: '$createdAt' },
        },
      },
      { $sort: { _id: 1 } },
    ]);
    res.json(
      rows.map((r) => ({
        deviceId: r._id,
        location: r.location,
        moisture: r.moisture,
        raw: r.raw,
        updatedAt: r.updatedAt,
        status: statusFor(r.moisture),
      }))
    );
  } catch (e) { next(e); }
});

// History for the chart: ?deviceId=esp32-01&hours=24
app.get('/api/readings', async (req, res, next) => {
  try {
    const hours = Math.min(Number(req.query.hours) || 24, 24 * 30);
    const q = { createdAt: { $gte: new Date(Date.now() - hours * 3600 * 1000) } };
    if (req.query.deviceId) q.deviceId = req.query.deviceId;
    const rows = await Reading.find(q).sort({ createdAt: 1 }).limit(2000).lean();
    res.json(rows.map((r) => ({ ...r, status: statusFor(r.moisture) })));
  } catch (e) { next(e); }
});

app.use((err, _req, res, _next) => {
  console.error(err);
  res.status(500).json({ error: 'Server error' });
});

mongoose
  .connect(MONGO_URI)
  .then(() => app.listen(PORT, '0.0.0.0', () => console.log(`API running on port ${PORT}`)))
  .catch((e) => {
    console.error('MongoDB connection failed:', e.message);
    process.exit(1);
  });
