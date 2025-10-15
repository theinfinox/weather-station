// api/update.js
let last = { t: 0, h: 0, time: new Date().toISOString() };

export default async function handler(req, res) {
  if (req.method === "POST") {
    try {
      const body = await req.json();
      // accept either {t,h} or {temp,hum}
      const t = typeof body.t === 'number' ? body.t : parseFloat(body.temp);
      const h = typeof body.h === 'number' ? body.h : parseFloat(body.hum);
      if (Number.isFinite(t) && Number.isFinite(h)) {
        last = { t, h, time: new Date().toISOString() };
        return res.status(200).json({ success: true });
      }
      return res.status(400).json({ error: "invalid payload" });
    } catch (e) {
      return res.status(400).json({ error: "invalid json" });
    }
  }
  if (req.method === "GET") {
    return res.status(200).json(last);
  }
  res.status(405).end();
}
