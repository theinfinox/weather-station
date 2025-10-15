let lastData = { t: 0, h: 0, time: new Date().toISOString() };

export default function handler(req, res) {
  if (req.method === 'POST') {
    const { t, h } = req.body || {};
    if (typeof t === 'number' && typeof h === 'number') {
      lastData = { t, h, time: new Date().toISOString() };
      return res.status(200).json({ success: true });
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
