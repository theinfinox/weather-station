// project/api/update.js

let latest = { t: 0, h: 0, time: Date.now() };

export default function handler(req, res) {
  if (req.method === 'POST') {
    try {
      const { t, h } = req.body;
      if (typeof t === 'number' && typeof h === 'number') {
        latest = { t, h, time: Date.now() };
        return res.status(200).json({ success: true });
      }
      return res.status(400).json({ error: 'Invalid payload' });
    } catch (e) {
      return res.status(400).json({ error: 'Bad request' });
    }
  } 
  // GET request
  return res.status(200).json(latest);
}
