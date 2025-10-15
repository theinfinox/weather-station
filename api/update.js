let latest = { t: 0, h: 0, time: new Date().toISOString() };

export default async function handler(req, res) {
  if (req.method === "POST") {
    try {
      const body = await req.json();
      latest = { 
        t: parseFloat(body.temp), 
        h: parseFloat(body.hum), 
        time: new Date().toISOString() 
      };
      return res.status(200).json({ status: "ok" });
    } catch (err) {
      return res.status(400).json({ error: "invalid data" });
    }
  } else if (req.method === "GET") {
    return res.status(200).json(latest);
  } else {
    return res.status(405).end();
  }
}
