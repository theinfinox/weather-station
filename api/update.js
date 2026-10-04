// project/api/update.js

let latest = null;

export default function handler(req, res) {
  if (req.method === 'POST') {
    try {
      const body = req.body || {};

      // Virtual Pet action from web dashboard
      if (body.action === 'pet') {
        if (!latest) latest = {};
        latest.pets = (latest.pets || 0) + 1;
        latest.mood = 'loved';
        latest.time = Date.now();
        return res.status(200).json({ success: true, pets: latest.pets, mood: latest.mood });
      }

      const { t, h, m, mood, status, pets, probeConnected } = body;
      const isConnected = probeConnected !== false && m !== -1 && typeof m === 'number' && m >= 0;

      if (typeof t === 'number' && typeof h === 'number') {
        const currentM = isConnected ? Number(m) : -1;
        let currentMood = mood;
        let currentStatus = status;

        if (!isConnected) {
          currentMood = currentMood || 'searching';
          currentStatus = 'Sensor Not Connected ⚠️';
        } else {
          currentMood = currentMood || (currentM < 35 ? 'thirsty' : currentM > 75 ? 'dizzy' : 'happy');
          currentStatus = currentStatus || (currentM < 35 ? 'Thirsty! 🪣' : currentM > 75 ? 'Too Wet! 🌊' : 'Optimal 🌿');
        }

        latest = {
          t: Number(t),
          h: Number(h),
          m: currentM,
          probeConnected: isConnected,
          mood: currentMood,
          status: currentStatus,
          pets: typeof pets === 'number' ? pets : latest.pets,
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

  // Reset transient 'loved' mood back to physiological state after 8 seconds
  if (latest.mood === 'loved' && Date.now() - latest.time > 8000) {
    latest.mood = latest.m < 35 ? 'thirsty' : latest.m > 75 ? 'dizzy' : 'happy';
  }

  return res.status(200).json(latest);
}

