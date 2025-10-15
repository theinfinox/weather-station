// api/history.js
// simple in-memory history (keeps up to 2000 points). For production replace with DB.
let history = []; // each item: { t, h, time }

export default async function handler(req, res) {
  if (req.method === "POST") {
    try {
      const body = await req.json();
      if (Array.isArray(body)) {
        // append array of datapoints
        body.forEach(pt => {
          const t = Number(pt.t ?? pt.temp);
          const h = Number(pt.h ?? pt.hum);
          const time = pt.time || new Date().toISOString();
          if (Number.isFinite(t) && Number.isFinite(h)) history.push({ t, h, time });
        });
      } else {
        const t = Number(body.t ?? body.temp);
        const h = Number(body.h ?? body.hum);
        const time = body.time || new Date().toISOString();
        if (Number.isFinite(t) && Number.isFinite(h)) history.push({ t, h, time });
      }
      // cap history size
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
    