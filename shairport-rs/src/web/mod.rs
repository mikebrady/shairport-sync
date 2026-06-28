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
    :root {
      color-scheme: light dark;
      font-family: Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background: Canvas;
      color: CanvasText;
    }
    * { box-sizing: border-box; }
    body { margin: 0; min-height: 100vh; }
    main {
      width: min(960px, calc(100vw - 32px));
      margin: 0 auto;
      padding: 28px 0;
      display: grid;
      gap: 22px;
    }
    header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 16px;
      border-bottom: 1px solid color-mix(in srgb, CanvasText 14%, transparent);
      padding-bottom: 18px;
    }
    h1, h2, p { margin: 0; }
    h1 { font-size: 24px; font-weight: 720; }
    h2 { font-size: 14px; font-weight: 700; text-transform: uppercase; color: color-mix(in srgb, CanvasText 62%, transparent); }
    button, input { font: inherit; }
    button {
      min-width: 44px;
      height: 40px;
      border: 1px solid color-mix(in srgb, CanvasText 18%, transparent);
      background: color-mix(in srgb, Canvas 92%, CanvasText 8%);
      color: CanvasText;
      border-radius: 8px;
      cursor: pointer;
    }
    button:hover { background: color-mix(in srgb, Canvas 84%, CanvasText 16%); }
    button.primary { min-width: 64px; background: #1f7a5c; color: white; border-color: #1f7a5c; }
    button:disabled { opacity: .48; cursor: default; }
    .status {
      display: inline-flex;
      align-items: center;
      min-height: 32px;
      padding: 0 10px;
      border-radius: 999px;
      background: color-mix(in srgb, Canvas 88%, CanvasText 12%);
      font-size: 13px;
    }
    .now {
      display: grid;
      grid-template-columns: minmax(0, 1fr) 220px;
      gap: 20px;
      align-items: end;
    }
    .track { display: grid; gap: 8px; min-width: 0; }
    .title { font-size: clamp(30px, 7vw, 56px); line-height: 1.02; font-weight: 760; overflow-wrap: anywhere; }
    .artist { font-size: 18px; color: color-mix(in srgb, CanvasText 68%, transparent); overflow-wrap: anywhere; }
    .controls {
      display: grid;
      grid-template-columns: repeat(5, 1fr);
      gap: 10px;
      align-items: center;
    }
    .volume {
      display: grid;
      grid-template-columns: 72px minmax(0, 1fr) 72px;
      gap: 14px;
      align-items: center;
      border-top: 1px solid color-mix(in srgb, CanvasText 14%, transparent);
      border-bottom: 1px solid color-mix(in srgb, CanvasText 14%, transparent);
      padding: 18px 0;
    }
    .progress {
      display: grid;
      grid-template-columns: 58px minmax(0, 1fr) 58px;
      gap: 12px;
      align-items: center;
    }
    progress {
      width: 100%;
      height: 10px;
      accent-color: #1f7a5c;
    }
    input[type="range"] { width: 100%; accent-color: #1f7a5c; }
    .grid {
      display: grid;
      grid-template-columns: repeat(3, minmax(0, 1fr));
      gap: 16px;
    }
    .metric { display: grid; gap: 4px; min-width: 0; }
    .metric span { font-size: 13px; color: color-mix(in srgb, CanvasText 62%, transparent); }
    .metric strong { font-size: 16px; font-weight: 650; overflow-wrap: anywhere; }
    .message { min-height: 20px; color: color-mix(in srgb, CanvasText 68%, transparent); font-size: 13px; }
    @media (max-width: 720px) {
      main { width: min(100vw - 24px, 960px); padding: 18px 0; }
      header, .now { grid-template-columns: 1fr; display: grid; align-items: start; }
      .controls { grid-template-columns: repeat(5, minmax(44px, 1fr)); }
      .grid { grid-template-columns: 1fr; }
      .volume { grid-template-columns: 58px minmax(0, 1fr) 58px; }
    }
  </style>
</head>
<body>
  <main>
    <header>
      <h1>Shairport RS</h1>
      <div id="status" class="status">Connecting</div>
    </header>

    <section class="now" aria-label="Now playing">
      <div class="track">
        <h2>Now Playing</h2>
        <p id="title" class="title">No media</p>
        <p id="artist" class="artist"></p>
      </div>
      <div class="controls" aria-label="Playback controls">
        <button data-command="previous" title="Previous track">⏮</button>
        <button data-command="pause" title="Pause">⏸</button>
        <button data-command="playpause" class="primary" title="Play or pause">▶</button>
        <button data-command="stop" title="Stop">⏹</button>
        <button data-command="next" title="Next track">⏭</button>
      </div>
    </section>

    <section class="volume" aria-label="Volume">
      <strong>Volume</strong>
      <input id="volume" type="range" min="-60" max="0" step="0.5">
      <output id="volumeText" for="volume">0 dB</output>
    </section>

    <section class="progress" aria-label="Track progress">
      <span id="elapsed">0:00</span>
      <progress id="progress" value="0" max="1"></progress>
      <span id="duration">0:00</span>
    </section>

    <section class="grid" aria-label="Session details">
      <div class="metric"><span>State</span><strong id="playerState">unknown</strong></div>
      <div class="metric"><span>Client</span><strong id="client">-</strong></div>
      <div class="metric"><span>DACP</span><strong id="dacp">-</strong></div>
    </section>
    <p id="message" class="message"></p>
  </main>
  <script>
    const $ = selector => document.querySelector(selector);
    let volumeTimer = null;

    function setText(selector, value) {
      $(selector).textContent = value == null || value === '' ? '-' : String(value);
    }

    async function api(path, options) {
      const response = await fetch(path, options);
      if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
      return response.json();
    }

    function render(media) {
      setText('#status', media.active ? 'Streaming' : 'Idle');
      setText('#title', media.track.title || 'No media');
      setText('#artist', [media.track.artist, media.track.album].filter(Boolean).join(' · '));
      setText('#playerState', media.player_state);
      setText('#client', media.track.client_name);
      setText('#dacp', media.remote_control.dacp_addr || media.remote_control.last_error || 'not ready');
      $('#volume').value = media.volume.local_db;
      setText('#volumeText', `${Number(media.volume.local_db).toFixed(1)} dB`);
      const elapsed = media.estimated_progress_ms ?? media.track.progress_ms ?? 0;
      const duration = media.track.duration_ms ?? 0;
      $('#progress').max = Math.max(duration, 1);
      $('#progress').value = Math.min(elapsed, duration || elapsed);
      setText('#elapsed', formatTime(elapsed));
      setText('#duration', duration ? formatTime(duration) : '--:--');
      document.querySelectorAll('[data-command]').forEach(button => {
        button.disabled = false;
      });
    }

    function formatTime(ms) {
      const total = Math.max(0, Math.floor(Number(ms || 0) / 1000));
      const minutes = Math.floor(total / 60);
      const seconds = String(total % 60).padStart(2, '0');
      return `${minutes}:${seconds}`;
    }

    async function refresh() {
      try {
        render(await api('/api/v1/media'));
      } catch (error) {
        setText('#status', 'Disconnected');
        setText('#message', error.message);
      }
    }

    async function control(payload) {
      setText('#message', '');
      const result = await api('/api/v1/media/control', {
        method: 'POST',
        headers: {'content-type': 'application/json'},
        body: JSON.stringify(payload)
      });
      setText('#message', result.message);
      refresh();
    }

    document.querySelectorAll('[data-command]').forEach(button => {
      button.addEventListener('click', () => control({ command: button.dataset.command }));
    });

    $('#volume').addEventListener('input', event => {
      const db = Number(event.target.value);
      setText('#volumeText', `${db.toFixed(1)} dB`);
      clearTimeout(volumeTimer);
      volumeTimer = setTimeout(() => control({ volume_db: db }), 120);
    });

    refresh();
    const ws = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/api/v1/events`);
    ws.addEventListener('message', refresh);
    ws.addEventListener('open', refresh);
    ws.addEventListener('close', () => setText('#status', 'Disconnected'));
  </script>
</body>
</html>"#,
    )
}
