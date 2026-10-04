// project/api/update.js

let latest = {
  t: 24.0,
  h: 60.0,
  m: 52.0,
  mood: 'happy',
  status: 'Optimal 🌿',
  pets: 0,
  time: Date.now()
};

export default function handler(req, res) {
  if (req.method === 'POST') {
    try {
      const body = req.body || {};

      // Virtual Pet action from web dashboard
      if (body.action === 'pet') {
        latest.pets = (latest.pets || 0) + 1;
        latest.mood = 'loved';
        latest.time = Date.now();
        return res.status(200).json({ success: true, pets: latest.pets, mood: latest.mood });
      }

      const { t, h, m, mood, status, pets } = body;
      if (typeof t === 'number' && typeof h === 'number') {
        latest = {
          t: Number(t),
          h: Number(h),
          m: typeof m === 'number' ? Number(m) : latest.m,
          mood: mood || (m < 35 ? 'thirsty' : m > 75 ? 'dizzy' : 'happy'),
          status: status || (m < 35 ? 'Thirsty! 🪣' : m > 75 ? 'Too Wet! 🌊' : 'Optimal 🌿'),
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
  // Reset transient 'loved' mood back to physiological state after 8 seconds
  if (latest.mood === 'loved' && Date.now() - latest.time > 8000) {
    latest.mood = latest.m < 35 ? 'thirsty' : latest.m > 75 ? 'dizzy' : 'happy';
  }

  return res.status(200).json(latest);
}

