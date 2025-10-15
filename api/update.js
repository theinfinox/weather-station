let latest = { t: 0, h: 0, time: Date.now() };

export default async function handler(req, res) {
  res.setHeader('Access-Control-Allow-Origin', '*'); // allow all origins
  res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

  if (req.method === 'OPTIONS') {
    return res.status(200).end(); // preflight
  }

  if (req.method === 'POST') {
    try {
      const body = req.body;
      let t = body?.t;
      let h = body?.h;

      // Vercel sometimes parses JSON automatically, else parse manually
      if (typeof t !== 'number' || typeof h !== 'number') {
        return res.status(400).json({ error: 'Invalid payload' });
      }

      latest = { t, h, time: Date.now() };
      return res.status(200).json({ success: true });
    } catch (e) {
      return res.status(400).json({ error: 'Bad request' });
    }
  }

  // GET request
  return res.status(200).json(latest);
}
