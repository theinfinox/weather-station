// api/history.js
// In-memory time-series history (keeps up to 2000 points).
let history = []; // each item: { t, h, m, mood, time }

export default async function handler(req, res) {
  if (req.method === "POST") {
    try {
      const body = req.body || (typeof req.json === 'function' ? await req.json() : {});
      const addPoint = (pt) => {
        const t = Number(pt.t ?? pt.temp);
        const h = Number(pt.h ?? pt.hum);
        const m = Number(pt.m ?? pt.moist ?? pt.moisture ?? 50);
        const mood = pt.mood || (m < 35 ? 'thirsty' : m > 75 ? 'dizzy' : 'happy');
        const time = pt.time || new Date().toISOString();
        if (Number.isFinite(t) && Number.isFinite(h)) {
          history.push({ t, h, m: Number.isFinite(m) ? m : 50, mood, time });
        }
      };

      if (Array.isArray(body)) {
        body.forEach(addPoint);
      } else {
        addPoint(body);
      }

      // cap history size to 2000 points
      if (history.length > 2000) history = history.slice(-2000);
      return res.status(200).json({ stored: history.length });
    } catch (e) {
      return res.status(400).json({ error: "invalid json" });
    }
  }

  if (req.method === "GET") {
    return res.status(200).json(history);
  }
  res.status(405).end();
}

    