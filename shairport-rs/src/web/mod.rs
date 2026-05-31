use axum::{Router, response::Html, routing::get};

pub fn router() -> Router {
    Router::new().route("/", get(index))
}

async fn index() -> Html<&'static str> {
    Html(
        r#"<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Shairport RS</title>
  <style>
    :root { color-scheme: light dark; font-family: system-ui, sans-serif; }
    body { margin: 0; padding: 24px; }
    main { max-width: 920px; margin: 0 auto; display: grid; gap: 16px; }
    section { border: 1px solid color-mix(in srgb, CanvasText 18%, transparent); border-radius: 8px; padding: 16px; }
    dl { display: grid; grid-template-columns: 160px 1fr; gap: 8px 12px; }
    dt { font-weight: 650; }
    button, input, select { font: inherit; }
  </style>
</head>
<body>
  <main>
    <h1>Shairport RS</h1>
    <section>
      <h2>Now Playing</h2>
      <dl id="state"></dl>
    </section>
    <section>
      <h2>Volume</h2>
      <input id="volume" type="range" min="-30" max="0" step="0.5">
      <button id="apply-volume">Apply</button>
    </section>
    <section>
      <h2>Audio Devices</h2>
      <select id="devices"></select>
    </section>
  </main>
  <script>
    async function refresh() {
      const state = await fetch('/api/v1/state').then(r => r.json());
      document.querySelector('#state').innerHTML = [
        ['Active', state.active],
        ['Player', state.player_state],
        ['Title', state.track.title || ''],
        ['Artist', state.track.artist || ''],
        ['Volume dB', state.volume.local_db],
        ['Audio host', state.audio.host],
        ['mDNS', `${state.mdns.backend} ${state.mdns.running ? 'running' : 'stopped'}`],
        ['PTP', `${state.ptp.sync_quality} packets=${state.ptp.packets_seen}`],
      ].map(([k, v]) => `<dt>${k}</dt><dd>${v}</dd>`).join('');
      document.querySelector('#volume').value = state.volume.local_db;
    }
    async function refreshDevices() {
      const devices = await fetch('/api/v1/audio/devices').then(r => r.json());
      document.querySelector('#devices').innerHTML = devices.map(d => `<option value="${d.id}">${d.host}: ${d.name}</option>`).join('');
    }
    document.querySelector('#apply-volume').addEventListener('click', async () => {
      await fetch('/api/v1/volume', { method: 'POST', headers: {'content-type': 'application/json'}, body: JSON.stringify({ db: Number(document.querySelector('#volume').value) }) });
      refresh();
    });
    refresh();
    refreshDevices();
    const ws = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/api/v1/events`);
    ws.addEventListener('message', refresh);
  </script>
</body>
</html>"#,
    )
}
