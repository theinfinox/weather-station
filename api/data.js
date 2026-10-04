// project/api/data.js

let lastData = {
  t: 24.0,
  h: 60.0,
  m: 52.0,
  mood: 'happy',
  status: 'Optimal 🌿',
  pets: 0,
  time: new Date().toISOString()
};

export default function handler(req, res) {
  if (req.method === 'POST') {
    const { t, h, m, mood, status, pets } = req.body || {};
    if (typeof t === 'number' && typeof h === 'number') {
      lastData = {
        t: Number(t),
        h: Number(h),
        m: typeof m === 'number' ? Number(m) : lastData.m,
        mood: mood || (m < 35 ? 'thirsty' : m > 75 ? 'dizzy' : 'happy'),
        status: status || (m < 35 ? 'Thirsty! 🪣' : m > 75 ? 'Too Wet! 🌊' : 'Optimal 🌿'),
        pets: typeof pets === 'number' ? pets : lastData.pets,
        time: new Date().toISOString()
      };
      return res.status(200).json({ success: true, data: lastData });
    } else {
      return res.status(400).json({ error: 'Invalid data' });
    }
  }

  if (req.method === 'GET') {
    res.status(200).json(lastData);
  } else {
    res.status(405).json({ error: 'Method Not Allowed' });
  }
}

