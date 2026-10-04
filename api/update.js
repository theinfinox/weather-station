// project/api/update.js

let latest = null;

export default function handler(req, res) {
  if (req.method === 'POST') {
    try {
      const body = req.body || {};
      const { t, h, m, mood, status, probeConnected } = body;
      const isConnected = probeConnected !== false && m !== -1 && typeof m === 'number' && m >= 0;

      if (typeof t === 'number' && typeof h === 'number') {
        const currentM = isConnected ? Number(m) : -1;
        let currentMood = mood;
        let currentStatus = status;

        if (!isConnected) {
          currentMood = currentMood || 'searching';
          currentStatus = 'Sensor Not Connected ⚠️';
        } else {
          currentMood = currentMood || (currentM < 25 ? 'thirsty' : currentM > 75 ? 'dizzy' : 'happy');
          currentStatus = currentStatus || (currentM < 25 ? 'Thirsty! Hydration Needed 🪣' : currentM > 75 ? 'Excess Moisture ⚠️' : 'Optimal Environment 🌿');
        }

        latest = {
          t: Number(t),
          h: Number(h),
          m: currentM,
          probeConnected: isConnected,
          mood: currentMood,
          status: currentStatus,
          time: Date.now()
        };
        return res.status(200).json({ success: true, data: latest });
      }

      return res.status(400).json({ error: 'Invalid payload: t and h numbers required' });
    } catch (e) {
      return res.status(400).json({ error: 'Bad request' });
    }
  } 

  // GET request
  if (!latest) {
    return res.status(200).json({ waiting: true });
  }

  return res.status(200).json(latest);
}
