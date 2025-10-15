let logs = [];

export default async function handler(req, res) {
  if (req.method === "POST") {
    try {
      const data = await req.json();
      logs = logs.concat(data);
      if (logs.length > 288) logs = logs.slice(-288);
      return res.status(200).json({ stored: logs.length });
    } catch (e) {
      return res.status(400).json({ error: "invalid data" });
    }
  }
  if (req.method === "GET") {
    return res.status(200).json(logs);
  }
  res.status(405).end();
}
