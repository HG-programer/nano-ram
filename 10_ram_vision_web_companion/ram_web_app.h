#ifndef RAM_WEB_APP_H
#define RAM_WEB_APP_H

#include <Arduino.h>

const char PAGE_INDEX[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <meta name="mobile-web-app-capable" content="yes">
  <meta name="apple-mobile-web-app-capable" content="yes">
  <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
  <meta name="theme-color" content="#020617">
  <title>🌸 Chibi Ram Controller</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: system-ui, -apple-system, sans-serif; }
    body { background: #020617; color: #f8fafc; min-height: 100vh; display: flex; justify-content: center; padding: 12px; }
    .card { width: 100%; max-width: 420px; background: #0f172a; border: 1px solid rgba(244, 114, 182, 0.2); border-radius: 24px; padding: 18px; display: flex; flex-direction: column; gap: 14px; box-shadow: 0 20px 40px rgba(0,0,0,0.6); }
    .header { display: flex; align-items: center; justify-content: space-between; border-bottom: 1px solid #1e293b; padding-bottom: 12px; }
    .logo { display: flex; align-items: center; gap: 10px; }
    .icon { width: 38px; height: 38px; border-radius: 12px; background: linear-gradient(135deg, #ec4899, #f43f5e); display: flex; align-items: center; justify-content: center; font-size: 20px; box-shadow: 0 4px 12px rgba(236,72,153,0.3); }
    .badge { padding: 4px 10px; border-radius: 9999px; font-size: 11px; font-weight: 600; display: inline-flex; align-items: center; gap: 6px; }
    .badge-idle { background: rgba(16, 185, 129, 0.15); color: #34d399; border: 1px solid rgba(16, 185, 129, 0.3); }
    .badge-recording { background: rgba(244, 63, 94, 0.15); color: #fb7185; border: 1px solid rgba(244, 63, 94, 0.3); }
    .badge-thinking { background: rgba(245, 158, 11, 0.15); color: #fbbf24; border: 1px solid rgba(245, 158, 11, 0.3); }
    .badge-speaking { background: rgba(168, 85, 247, 0.15); color: #c084fc; border: 1px solid rgba(168, 85, 247, 0.3); }
    .cam-box { position: relative; border-radius: 16px; overflow: hidden; background: #000; border: 1px solid #334155; height: 180px; display: flex; align-items: center; justify-content: center; }
    .cam-img { width: 100%; height: 100%; object-fit: cover; }
    .cam-hud { position: absolute; inset: 6px; pointer-events: none; display: flex; flex-direction: column; justify-content: space-between; font-family: monospace; font-size: 10px; color: rgba(244, 114, 182, 0.9); }
    .hud-top { display: flex; justify-content: space-between; }
    .reticle { align-self: center; width: 60px; height: 60px; border: 1px dashed rgba(244, 114, 182, 0.5); border-radius: 8px; display: flex; align-items: center; justify-content: center; }
    .reticle-dot { width: 4px; height: 4px; background: #ec4899; border-radius: 50%; }
    .btn-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
    .btn-main { padding: 14px; border-radius: 16px; border: none; font-weight: 700; color: #fff; cursor: pointer; display: flex; flex-direction: column; align-items: center; gap: 4px; transition: transform 0.1s, opacity 0.2s; box-shadow: 0 4px 14px rgba(0,0,0,0.3); }
    .btn-main:active { transform: scale(0.97); }
    .btn-look { background: linear-gradient(135deg, #8b5cf6, #ec4899); border: 1px solid rgba(167, 139, 250, 0.3); }
    .btn-talk { background: linear-gradient(135deg, #ec4899, #f43f5e); border: 1px solid rgba(244, 114, 182, 0.3); }
    .btn-sub { background: #1e293b; color: #e2e8f0; border: 1px solid #334155; padding: 10px; border-radius: 12px; font-size: 12px; font-weight: 600; cursor: pointer; display: flex; align-items: center; justify-content: center; gap: 6px; }
    .btn-sub:active { transform: scale(0.97); background: #334155; }
    .convo-card { background: #020617; border: 1px solid #1e293b; border-radius: 16px; padding: 12px; display: flex; flex-direction: column; gap: 8px; font-size: 12px; }
    .chat-bubble { padding: 8px 10px; border-radius: 10px; }
    .chat-user { background: rgba(59, 130, 246, 0.1); border: 1px solid rgba(59, 130, 246, 0.2); }
    .chat-ram { background: rgba(236, 72, 153, 0.1); border: 1px solid rgba(236, 72, 153, 0.2); color: #fbcfe8; }
    .input-row { display: flex; gap: 8px; }
    .input-box { flex: 1; background: #020617; border: 1px solid #334155; border-radius: 12px; padding: 9px 12px; font-size: 12px; color: #fff; outline: none; }
    .input-box:focus { border-color: #ec4899; }
    .btn-send { background: #ec4899; color: #fff; border: none; padding: 0 16px; border-radius: 12px; font-weight: 700; font-size: 12px; cursor: pointer; }
    .slider-row { display: flex; flex-direction: column; gap: 4px; border-top: 1px solid #1e293b; padding-top: 10px; font-size: 11px; }
    .slider-info { display: flex; justify-content: space-between; color: #94a3b8; }
    input[type=range] { accent-color: #ec4899; width: 100%; cursor: pointer; }
    @keyframes pulse-fast { 0%, 100% { transform: scale(1); } 50% { transform: scale(1.22); } }
    .pulse-heart { display: inline-block; animation: pulse-fast 0.8s infinite ease-in-out; }
  </style>
</head>
<body>
  <div class="card">
    <div class="header">
      <div class="logo">
        <div class="icon">🌸</div>
        <div>
          <div style="font-weight:700; color:#f472b6; font-size:14px;">Chibi Ram</div>
          <div style="font-size:11px; color:#94a3b8;">Vision & Voice Companion</div>
        </div>
      </div>
      <div style="display:flex; flex-direction:column; align-items:flex-end; gap:4px;">
        <div id="badge" class="badge badge-idle">
          <span style="width:6px; height:6px; border-radius:50%; background:#10b981;"></span>
          <span id="badge-text">Idle</span>
        </div>
        <div id="mood-badge" style="font-size:10px; font-weight:700; padding:2px 8px; border-radius:9999px; background:rgba(236,72,153,0.15); border:1px solid rgba(236,72,153,0.3); color:#f472b6; display:inline-flex; align-items:center; gap:4px;">
          <span id="mood-icon">🌸</span>
          <span id="mood-text">Mood: 65%</span>
        </div>
      </div>
    </div>

    <!-- Camera Viewfinder -->
    <div class="cam-box">
      <img id="cam-img" class="cam-img" src="/cam.jpg" alt="OV3660 Camera Stream" onerror="camError()">
      <div class="cam-hud">
        <div class="hud-top">
          <span>● LIVE • OV3660</span>
          <span id="rssi-text">Wi-Fi: -- dBm</span>
        </div>
        <div class="reticle">
          <div class="reticle-dot"></div>
        </div>
        <div style="display:flex; justify-content:space-between;">
          <span id="lux-text">☀️ LUX: 120</span>
          <span id="uptime-text">ONLINE</span>
        </div>
      </div>
    </div>

    <!-- Primary Dual Buttons -->
    <div class="btn-grid">
      <button class="btn-main btn-look" onclick="doAction('look_and_react')">
        <span style="font-size:22px;">👁️</span>
        <span style="font-size:12px; text-transform:uppercase;">Look & React</span>
        <span style="font-size:9px; opacity:0.85;">Camera + Voice (5s)</span>
      </button>
      <button class="btn-main btn-talk" onclick="doAction('record')">
        <span style="font-size:22px;">🎙️</span>
        <span style="font-size:12px; text-transform:uppercase;">Talk to Ram</span>
        <span style="font-size:9px; opacity:0.85;">Voice Only (5s)</span>
      </button>
    </div>

    <!-- Secondary Quick Actions -->
    <div class="btn-grid">
      <button class="btn-sub" onclick="doAction('snap_roast')">
        <span>📸</span> Snap & Roast
      </button>
      <button class="btn-sub" style="border-color: rgba(236,72,153,0.4);" onclick="enrollHaru(event)">
        <span>🌸</span> Enroll Haru's Face
      </button>
    </div>

    <!-- Animatronic Gestures -->
    <div style="display:grid; grid-template-columns: repeat(4, 1fr); gap: 6px;">
      <button class="btn-sub" style="padding: 7px 2px; font-size: 10px;" onclick="triggerMotion('BOW')">🙇 Bow</button>
      <button class="btn-sub" style="padding: 7px 2px; font-size: 10px;" onclick="triggerMotion('TILT')">💫 Tilt</button>
      <button class="btn-sub" style="padding: 7px 2px; font-size: 10px;" onclick="triggerMotion('NOD')">✨ Nod</button>
      <button class="btn-sub" style="padding: 7px 2px; font-size: 10px;" onclick="triggerMotion('WAVE')">👋 Wave</button>
    </div>

    <!-- Live Subtitles & Speech Feed -->
    <div class="convo-card" id="speech-card" style="border: 1px solid #1e293b; transition: all 0.25s ease;">
      <div style="display:flex; justify-content:space-between; align-items:center;">
        <div style="display:flex; align-items:center; gap:6px;">
          <span style="color:#ec4899; font-size:11px; font-weight:800; letter-spacing:0.5px; text-transform:uppercase;">🌸 RAM LIVE SUBTITLES</span>
          <span id="motion-badge" style="background:rgba(236,72,153,0.18); color:#f472b6; border:1px solid rgba(236,72,153,0.35); font-size:9px; font-weight:700; padding:2px 6px; border-radius:6px;">IDLE</span>
        </div>
        <div id="id-badge" style="display:none; font-size:10px; font-weight:700; padding:2px 8px; border-radius:10px;"></div>
        <button id="copy-btn" onclick="copyRamText()" style="background:none; border:none; color:#94a3b8; font-size:11px; cursor:pointer; padding:2px 4px;">📋 Copy</button>
      </div>

      <!-- Ram's Spoken Japanese Text (Prominent, High-Legibility Anime Subtitles) -->
      <div id="ram-msg" class="chat-bubble chat-ram" style="font-size:17px; font-weight:700; line-height:1.6; color:#ffffff; background:rgba(236,72,153,0.15); border:1px solid rgba(236,72,153,0.35); letter-spacing:0.5px; font-family:'Hiragino Sans','Meiryo','Yu Gothic',system-ui,sans-serif; text-shadow:0 1px 3px rgba(0,0,0,0.5); transition: all 0.2s ease;">
        「ハル様、お呼びでしょうか。ラムはいつでもお側におりますわ。」
      </div>

      <!-- User / Stranger Spoken Input or Visual Action -->
      <div id="user-msg" class="chat-bubble chat-user" style="font-size:12px; color:#cbd5e1; opacity:0.9; transition: all 0.2s ease;">
        <span id="user-label" style="color:#60a5fa; font-weight:700;">Haru:</span> <span id="user-text">"..."</span>
      </div>
    </div>

    <!-- Text Chat Direct Input -->
    <div class="input-row">
      <input id="text-input" type="text" class="input-box" placeholder="Type a prompt for Ram...">
      <button class="btn-send" onclick="sendPrompt()">Send</button>
    </div>

    <!-- Volume Control -->
    <div class="slider-row">
      <div class="slider-info">
        <span>Speaker Volume</span>
        <span id="vol-text" style="color:#f472b6; font-weight:700;">75%</span>
      </div>
      <input id="vol-slider" type="range" min="0" max="100" value="75" onchange="setVolume(this.value)" oninput="document.getElementById('vol-text').innerText = this.value + '%'">
    </div>
  </div>

  <script>
    let camStreaming = true;
    let pollTimer = null;
    let lastSeq = -1;
    let lastKnownReply = "";

    function refreshCam() {
      if (!camStreaming) return;
      const img = document.getElementById('cam-img');
      img.src = '/cam.jpg?t=' + Date.now();
    }
    setInterval(refreshCam, 400);

    function camError() {}

    function updateBadge(state) {
      const b = document.getElementById('badge');
      const t = document.getElementById('badge-text');
      b.className = 'badge';
      if (state === 'recording') {
        b.classList.add('badge-recording');
        t.innerText = 'Listening (5s)...';
      } else if (state === 'thinking') {
        b.classList.add('badge-thinking');
        t.innerText = 'Thinking...';
      } else if (state === 'speaking') {
        b.classList.add('badge-speaking');
        t.innerText = 'Speaking!';
      } else {
        b.classList.add('badge-idle');
        t.innerText = 'Idle';
      }
    }

    function copyRamText() {
      const el = document.getElementById('ram-msg');
      const clean = el.innerText.replace(/^[「\s]+|[」\s]+$/g, '');
      if (navigator.clipboard) {
        navigator.clipboard.writeText(clean);
      }
      const btn = document.getElementById('copy-btn');
      btn.innerText = '✓ Copied!';
      setTimeout(() => { btn.innerText = '📋 Copy'; }, 1500);
    }

    async function pollStatus() {
      let nextDelay = 600;
      try {
        const res = await fetch('/api/status');
        if (res.ok) {
          const data = await res.json();
          updateBadge(data.state);

          if (data.wifi_rssi) {
            document.getElementById('rssi-text').innerText = 'Wi-Fi: ' + data.wifi_rssi + ' dBm';
          }
          if (data.last_transcript) {
            document.getElementById('user-text').innerText = '"' + data.last_transcript + '"';
          }
          if (data.last_motion) {
            document.getElementById('motion-badge').innerText = data.last_motion;
          }

          // Visual identity indicator badge and speaker label
          const idB = document.getElementById('id-badge');
          const userMsg = document.getElementById('user-msg');
          const userLabel = document.getElementById('user-label');
          if (data.identity === 'STRANGER') {
            idB.style.display = 'inline-block';
            idB.style.background = 'rgba(239, 68, 68, 0.25)';
            idB.style.color = '#f87171';
            idB.style.border = '1px solid #ef4444';
            idB.innerText = '🚨 STRANGER DETECTED';
            if (userLabel) {
              userLabel.innerText = '🚨 Stranger:';
              userLabel.style.color = '#f87171';
            }
            if (userMsg) {
              userMsg.style.background = 'rgba(239, 68, 68, 0.15)';
              userMsg.style.borderColor = 'rgba(239, 68, 68, 0.4)';
            }
          } else {
            if (data.identity === 'HARU') {
              idB.style.display = 'inline-block';
              idB.style.background = 'rgba(16, 185, 129, 0.2)';
              idB.style.color = '#34d399';
              idB.style.border = '1px solid #10b981';
              idB.innerText = '🌸 MASTER: HARU-SAMA';
            } else {
              idB.style.display = 'none';
            }
            if (userLabel) {
              userLabel.innerText = 'Haru:';
              userLabel.style.color = '#60a5fa';
            }
            if (userMsg) {
              userMsg.style.background = 'rgba(59, 130, 246, 0.1)';
              userMsg.style.borderColor = 'rgba(59, 130, 246, 0.2)';
            }
          }

          // Dynamic Emotion Engine Mood Badge
          if (data.ram_mood !== undefined) {
            const mText = document.getElementById('mood-text');
            const mIcon = document.getElementById('mood-icon');
            const mBadge = document.getElementById('mood-badge');
            if (mText && mIcon && mBadge) {
              const m = data.ram_mood;
              if (m >= 80) {
                mIcon.innerText = '💖';
                mIcon.className = 'pulse-heart';
                mText.innerText = 'Mood: ' + m + '% (Devoted)';
                mBadge.style.borderColor = '#ec4899';
                mBadge.style.color = '#f472b6';
              } else {
                mIcon.className = '';
                if (m >= 45) {
                  mIcon.innerText = '🌸';
                  mText.innerText = 'Mood: ' + m + '% (Happy)';
                  mBadge.style.borderColor = 'rgba(236,72,153,0.3)';
                  mBadge.style.color = '#f472b6';
                } else if (m >= 25) {
                  mIcon.innerText = '😒';
                  mText.innerText = 'Mood: ' + m + '% (Bored)';
                  mBadge.style.borderColor = 'rgba(245,158,11,0.3)';
                  mBadge.style.color = '#fbbf24';
                } else {
                  mIcon.innerText = '❄️';
                  mText.innerText = 'Mood: ' + m + '% (Cold)';
                  mBadge.style.borderColor = 'rgba(59,130,246,0.3)';
                  mBadge.style.color = '#93c5fd';
                }
              }
            }
          }

          // Environmental Light Lux in Camera HUD & Night Ambiance
          if (data.ambient_lux !== undefined) {
            const luxEl = document.getElementById('lux-text');
            const mainCard = document.querySelector('.card');
            if (luxEl) {
              luxEl.innerText = (data.ambient_lux < 25 ? '🌑 NIGHT (' + data.ambient_lux + ')' : '☀️ LUX: ' + data.ambient_lux);
            }
            if (mainCard) {
              if (data.ambient_lux < 25) {
                mainCard.style.boxShadow = '0 0 35px rgba(59, 130, 246, 0.2)';
              } else {
                mainCard.style.boxShadow = '0 20px 40px rgba(0,0,0,0.6)';
              }
            }
          }

          // Zero-latency subtitle update
          if (data.last_reply && (data.last_reply !== lastKnownReply || (data.seq && data.seq !== lastSeq))) {
            lastKnownReply = data.last_reply;
            lastSeq = data.seq || 0;
            const ramEl = document.getElementById('ram-msg');
            ramEl.innerText = (data.last_reply.startsWith('「')) ? data.last_reply : '「' + data.last_reply + '」';
          }

          // Active speaking visual cue (pink border glow)
          const card = document.getElementById('speech-card');
          const ramEl = document.getElementById('ram-msg');
          if (data.state === 'speaking') {
            card.style.borderColor = 'rgba(236, 72, 153, 0.7)';
            card.style.boxShadow = '0 0 18px rgba(236, 72, 153, 0.3)';
            ramEl.style.borderColor = '#ec4899';
            ramEl.style.background = 'rgba(236, 72, 153, 0.2)';
          } else {
            card.style.borderColor = '#1e293b';
            card.style.boxShadow = 'none';
            ramEl.style.borderColor = 'rgba(236, 72, 153, 0.3)';
            ramEl.style.background = 'rgba(236, 72, 153, 0.12)';
          }

          if (data.volume !== undefined) {
            document.getElementById('vol-slider').value = data.volume;
            document.getElementById('vol-text').innerText = data.volume + '%';
          }

          // High-frequency polling (80ms) during active states so text appears instantly without latency
          nextDelay = (data.state === 'idle') ? 500 : 80;
        }
      } catch (err) {
        nextDelay = 600;
      }
      pollTimer = setTimeout(pollStatus, nextDelay);
    }

    function triggerFastPoll() {
      clearTimeout(pollTimer);
      pollStatus();
    }

    // Start polling immediately
    pollStatus();

    async function enrollHaru(evt) {
      const btn = evt.currentTarget;
      btn.innerText = '⏳ Enrolling...';
      try {
        const res = await fetch('/api/enroll', { method: 'POST' });
        const data = await res.json();
        if (data.ok) {
          alert('🌸 ' + (data.message || 'Haru-sama enrolled successfully!'));
        } else {
          alert('⚠️ Enrollment failed: ' + (data.error || 'Unknown error'));
        }
      } catch (err) {
        alert('Enrollment error: ' + err);
      }
      btn.innerHTML = '<span>🌸</span> Enroll Haru\'s Face';
    }

    async function doAction(actionName) {
      try {
        updateBadge('thinking');
        triggerFastPoll();
        await fetch('/api/action', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ action: actionName })
        });
      } catch (err) {
        alert('Action error: ' + err);
      }
    }

    async function triggerMotion(motionTag) {
      try {
        await fetch('/api/motion', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ motion: motionTag })
        });
      } catch (err) {}
    }

    async function sendPrompt() {
      const input = document.getElementById('text-input');
      const text = input.value.trim();
      if (!text) return;
      input.value = '';
      document.getElementById('user-text').innerText = '"' + text + '"';
      updateBadge('thinking');
      triggerFastPoll();
      try {
        await fetch('/api/say', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ message: text })
        });
      } catch (err) {
        alert('Send error: ' + err);
      }
    }

    async function setVolume(val) {
      try {
        await fetch('/api/volume', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ volume: parseInt(val) })
        });
      } catch (err) {}
    }
  </script>
</body>
</html>
)rawliteral";

#endif // RAM_WEB_APP_H
