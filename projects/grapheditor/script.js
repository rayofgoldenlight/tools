// ===== Variables =====
let expression = "x^2+x-4";
let parameters = { a: 1, b: 1, c: 0 };

const canvasWidth = 900;
const canvasHeight = 600;
const scale = 20;

const maxWaves = 1000;

// Wave arrays
const waveX = new Float32Array(maxWaves);
const waveY = new Float32Array(maxWaves);
const waveSpeed = new Float32Array(maxWaves);
const waveActive = new Uint8Array(maxWaves);
let waveCount = 0;

// Destroyed waves
const destroyedX = new Float32Array(maxWaves);
const destroyedY = new Float32Array(maxWaves);
const destroyedTimer = new Float32Array(maxWaves);
let destroyedCount = 0;

// Game refs
let points = 0;
let health = 10;
let laserReady = true;
let laserActive = false;
let laserCooldown = 0;
let hover = null;
let spawnCount = 1;

// Bombs
const bombX = new Float32Array(maxWaves);
const bombY = new Float32Array(maxWaves);
const bombTimer = new Float32Array(maxWaves);
const bombActive = new Uint8Array(maxWaves);
let bombCount = 0;
let bombCooldown = 0;

// Function LUT
let fyLUT = new Float32Array(canvasWidth);
let path = null;
let f = (x, a, b, c) => x;

// Canvas + context
const canvas = document.getElementById("gameCanvas");
const ctx = canvas.getContext("2d");

// HUD elements
const expressionInput = document.getElementById("expressionInput");
const sliderA = document.getElementById("a");
const sliderB = document.getElementById("b");
const sliderC = document.getElementById("c");
const aValue = document.getElementById("aValue");
const bValue = document.getElementById("bValue");
const cValue = document.getElementById("cValue");

// ===== Music =====
const bgMusic = new Audio("https://audio.jukehost.co.uk/zqErpEMIzaHS0lbvbhBYRjZnf27X3vph");
bgMusic.loop = true;
bgMusic.volume = 0.5;

const gameOverMusic = new Audio("https://audio.jukehost.co.uk/H9NPgJDqUi3DdWf8qgXm8oRtkP9e0i4j");
gameOverMusic.loop = true;
gameOverMusic.volume = 0.5;

// Track whether music is playing
gameOverMusic.playing = false;
bgMusic.playing = false;

bgMusic.addEventListener("play", () => bgMusic.playing = true);
bgMusic.addEventListener("pause", () => bgMusic.playing = false);

gameOverMusic.addEventListener("play", () => gameOverMusic.playing = true);
gameOverMusic.addEventListener("pause", () => gameOverMusic.playing = false);

function fadeOut(audio, duration = 1000, callback = null) {
  const step = audio.volume / (duration / 50);
  const fade = setInterval(() => {
    if (audio.volume > step) {
      audio.volume -= step;
    } else {
      audio.volume = 0;
      audio.pause();
      audio.currentTime = 0;
      clearInterval(fade);
      if (callback) callback();
    }
  }, 50);
}

function fadeIn(audio, duration = 1000, targetVolume = 0.5) {
  audio.volume = 0;
  audio.play();
  const step = targetVolume / (duration / 50);
  const fade = setInterval(() => {
    if (audio.volume < targetVolume - step) {
      audio.volume += step;
    } else {
      audio.volume = targetVolume;
      clearInterval(fade);
    }
  }, 50);
}

let gameOverTriggered = false; // global

// ===== Wave sprite =====
let waveSprite = null;
(function makeWaveSprite() {
  const r = scale;
  const s = document.createElement("canvas");
  s.width = s.height = r * 2 + 4;
  const c = s.getContext("2d");
  c.fillStyle = "#00008B";
  c.beginPath();
  c.arc(s.width / 2, s.height / 2, r, 0, Math.PI * 2);
  c.fill();
  c.strokeStyle = "#fff";
  c.lineWidth = 2;
  c.stroke();
  waveSprite = s;
})();

// ===== Compile expression =====
function compileExpression() {
  try {
    const toJs = (s) => s.replace(/^y\s*=\s*/, "").replace(/\^/g, "**");
    try {
      f = new Function(
        "x",
        "a",
        "b",
        "c",
        `
        const {sin, cos, tan, abs, sqrt} = Math;
        return ${toJs(expression)};
      `
      );
    } catch {
      f = (x, a, b, c) => x;
    }

    const lut = new Float32Array(canvasWidth);
    const p = new Path2D();
    for (let px = 0; px < canvasWidth; px++) {
      const x = (px - canvasWidth / 2) / scale;
      let y = f(x, parameters.a, parameters.b, parameters.c);
      if (!isFinite(y)) y = 0;
      lut[px] = y;
      const py = canvasHeight / 2 - y * scale;
      if (px === 0) p.moveTo(px, py);
      else p.lineTo(px, py);
    }
    fyLUT = lut;
    path = p;
  } catch {
    f = (x, a, b, c) => x;
    path = null;
  }
}
compileExpression();

// ===== Laser =====
function fireLaser() {
  if (!laserReady) return;
  laserReady = false;
  laserActive = true;
  laserCooldown = 3;

  for (let i = 0; i < waveCount; i++) {
    if (!waveActive[i]) continue;
    const fx = waveX[i];
    const fy = f(fx, parameters.a, parameters.b, parameters.c);
    if (Math.abs(waveY[i] - fy) <= 3.5) {
      waveActive[i] = 0;
      points++;
      if (destroyedCount < maxWaves) {
        destroyedX[destroyedCount] = waveX[i];
        destroyedY[destroyedCount] = waveY[i];
        destroyedTimer[destroyedCount] = 0.2;
        destroyedCount++;
      }
    }
  }

  setTimeout(() => (laserActive = false), 200);
}

// ===== Events =====
expressionInput.addEventListener("input", (e) => {
  expression = e.target.value;
  compileExpression();
});

sliderA.addEventListener("input", (e) => {
  parameters.a = parseFloat(e.target.value);
  aValue.textContent = parameters.a.toFixed(2);
  compileExpression();
});

sliderB.addEventListener("input", (e) => {
  parameters.b = parseFloat(e.target.value);
  bValue.textContent = parameters.b.toFixed(2);
  compileExpression();
});

sliderC.addEventListener("input", (e) => {
  parameters.c = parseFloat(e.target.value);
  cValue.textContent = parameters.c.toFixed(2);
  compileExpression();
});

canvas.addEventListener("mousemove", (e) => {
  const rect = canvas.getBoundingClientRect();
  const x = (e.clientX - rect.left - canvasWidth / 2) / scale;
  const y = (canvasHeight / 2 - (e.clientY - rect.top)) / scale;
  hover = { x, y };
});

canvas.addEventListener("click", (e) => {
  if (bombCooldown > 0) return;
  const rect = canvas.getBoundingClientRect();
  const x = (e.clientX - rect.left - canvasWidth / 2) / scale;
  const y = (canvasHeight / 2 - (e.clientY - rect.top)) / scale;

  if (bombCount < maxWaves) {
    bombX[bombCount] = x;
    bombY[bombCount] = y;
    bombTimer[bombCount] = 0.3;
    bombActive[bombCount] = 1;
    bombCount++;
    bombCooldown = 4;
  }
});

window.addEventListener("keydown", (e) => {
  if (e.key === "Enter") {
    if (bgMusic.paused) {
      bgMusic.play(); // start background music on first Enter press
    }
    fireLaser();}
});

// ===== Background =====
const bgCanvas = document.createElement("canvas");
bgCanvas.width = canvasWidth;
bgCanvas.height = canvasHeight;
const bgCtx = bgCanvas.getContext("2d");
const grad = bgCtx.createLinearGradient(0, 0, 0, canvasHeight);
grad.addColorStop(0, "#87ceeb");
grad.addColorStop(0.6, "#00bfff");
grad.addColorStop(1, "#f0e68c");
bgCtx.fillStyle = grad;
bgCtx.fillRect(0, 0, canvasWidth, canvasHeight);
bgCtx.strokeStyle = "rgba(255,255,255,0.3)";
bgCtx.lineWidth = 1;
for (let x = 0; x < canvasWidth; x += scale) {
  bgCtx.beginPath();
  bgCtx.moveTo(x, 0);
  bgCtx.lineTo(x, canvasHeight);
  bgCtx.stroke();
}
for (let y = 0; y < canvasHeight; y += scale) {
  bgCtx.beginPath();
  bgCtx.moveTo(0, y);
  bgCtx.lineTo(canvasWidth, y);
  bgCtx.stroke();
}
bgCtx.strokeStyle = "#fff";
bgCtx.lineWidth = 2;
bgCtx.beginPath();
bgCtx.moveTo(0, canvasHeight / 2);
bgCtx.lineTo(canvasWidth, canvasHeight / 2);
bgCtx.stroke();
bgCtx.beginPath();
bgCtx.moveTo(canvasWidth / 2, 0);
bgCtx.lineTo(canvasWidth / 2, canvasHeight);
bgCtx.stroke();

// ===== Game Loop =====
let lastTime = performance.now();
let spawnTimer = 0;

function animate(time) {
  const delta = (time - lastTime) / 1000;
  lastTime = time;

  ctx.drawImage(bgCanvas, 0, 0);

  // Center glow
  if (health > 0) {
    const glowRad = 50 + 30 * Math.sin(time / 300);
    const glow = ctx.createRadialGradient(
      canvasWidth / 2,
      canvasHeight / 2,
      0,
      canvasWidth / 2,
      canvasHeight / 2,
      glowRad
    );
    glow.addColorStop(0, "rgba(255,255,0,0.3)");
    glow.addColorStop(1, "rgba(255,255,0,0)");
    ctx.fillStyle = glow;
    ctx.beginPath();
    ctx.arc(canvasWidth / 2, canvasHeight / 2, glowRad, 0, Math.PI * 2);
    ctx.fill();
  }

  // Spawn waves
  spawnTimer += delta;
  if (spawnTimer >= 4) {
    spawnTimer -= 4;
    for (let i = 0; i < spawnCount; i++) {
      if (waveCount >= maxWaves) break;
      const edge = Math.floor(Math.random() * 4);
      let x = 0,
        y = 0;
       
       const baseSpeed = 3; 
       const speed = baseSpeed + time/3000; //increases speed over time
      
      switch (edge) {
        case 0:
          x = -20;
          y = Math.random() * 20 - 10;
          break;
        case 1:
          x = 20;
          y = Math.random() * 20 - 10;
          break;
        case 2:
          y = -15;
          x = Math.random() * 30 - 15;
          break;
        case 3:
          y = 15;
          x = Math.random() * 30 - 15;
          break;
      }
      waveX[waveCount] = x;
      waveY[waveCount] = y;
      waveSpeed[waveCount] = speed;
      waveActive[waveCount] = 1;
      waveCount++;
    }
  }

  // Ramp up difficulty
  if (Math.floor(time / 20000) > Math.floor((time - delta * 1000) / 20000))
    spawnCount += 2;

  // Update waves
  for (let i = 0; i < waveCount; i++) {
    if (!waveActive[i]) continue;
    const dx = -waveX[i],
      dy = -waveY[i];
    const dist = Math.sqrt(dx * dx + dy * dy);
    if (dist > 0) {
      waveX[i] += (dx / dist) * waveSpeed[i] * delta;
      waveY[i] += (dy / dist) * waveSpeed[i] * delta;
    }
    if (Math.abs(waveX[i]) < 0.1 && Math.abs(waveY[i]) < 0.1) {
      waveActive[i] = 0;
      health = Math.max(0, health - 1);
    }
  }

  // ===== Music check for game over =====
    if (health <= 0 && !gameOverTriggered) {
        gameOverTriggered = true;
        fadeOut(bgMusic, 1500, () => fadeIn(gameOverMusic, 1500, 0.5));
    }

  // Update bombs
  for (let i = 0; i < bombCount; i++) {
    if (!bombActive[i]) continue;
    bombTimer[i] -= delta;
    if (bombTimer[i] <= 0) {
      bombActive[i] = 0;
      for (let j = 0; j < waveCount; j++) {
        if (!waveActive[j]) continue;
        const dx = waveX[j] - bombX[i];
        const dy = waveY[j] - bombY[i];
        if (Math.sqrt(dx * dx + dy * dy) <= 6) {
          waveActive[j] = 0;
          points++;
        }
      }
    }
  }

  // Cooldowns
  if (!laserReady) {
    laserCooldown = Math.max(0, laserCooldown - delta);
    if (laserCooldown === 0) laserReady = true;
  }
  if (bombCooldown > 0) bombCooldown = Math.max(0, bombCooldown - delta);

  // Draw waves
  if (health > 0 && waveSprite) {
    for (let i = 0; i < waveCount; i++) {
      if (!waveActive[i]) continue;
      const px = canvasWidth / 2 + waveX[i] * scale;
      const py = canvasHeight / 2 - waveY[i] * scale;
      ctx.drawImage(waveSprite, px - waveSprite.width / 2, py - waveSprite.height / 2);
    }
  } else waveCount = 0;

  // Draw destroyed flashes
  for (let i = 0; i < destroyedCount; i++) {
    if (destroyedTimer[i] <= 0) continue;
    destroyedTimer[i] -= delta;
    const alpha = destroyedTimer[i] / 0.2;
    const px = canvasWidth / 2 + destroyedX[i] * scale;
    const py = canvasHeight / 2 - destroyedY[i] * scale;
    ctx.fillStyle = `rgba(255,255,0,${alpha})`;
    ctx.beginPath();
    ctx.arc(px, py, scale * 0.8, 0, Math.PI * 2);
    ctx.fill();
  }
  // Compact destroyed
  let newCount = 0;
  for (let i = 0; i < destroyedCount; i++) {
    if (destroyedTimer[i] > 0) {
      destroyedX[newCount] = destroyedX[i];
      destroyedY[newCount] = destroyedY[i];
      destroyedTimer[newCount] = destroyedTimer[i];
      newCount++;
    }
  }
  destroyedCount = newCount;

  // Draw bombs
  for (let i = 0; i < bombCount; i++) {
    if (!bombActive[i]) continue;
    const px = canvasWidth / 2 + bombX[i] * scale;
    const py = canvasHeight / 2 - bombY[i] * scale;
    const radius = 6 * scale * (1 - bombTimer[i] / 0.3);
    ctx.strokeStyle = "yellow";
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.arc(px, py, radius, 0, Math.PI * 2);
    ctx.stroke();
  }

  // Draw function/laser
  ctx.strokeStyle = laserActive ? "#FFFF00" : "#FF4500";
  ctx.lineWidth = 3;
  if (path) ctx.stroke(path);

  // HUD
  ctx.fillStyle = "rgba(0,0,0,0.5)";
  ctx.fillRect(canvasWidth - 160, 10, 150, 110);
  ctx.fillStyle = "#fff";
  ctx.font = "18px Arial";
  ctx.textAlign = "right";
  ctx.fillText(`Points: ${points}`, canvasWidth - 20, 30);
  ctx.fillText(`Health: ${health}`, canvasWidth - 20, 55);
  ctx.fillText(`Laser CD: ${laserCooldown.toFixed(1)}s`, canvasWidth - 20, 80);
  ctx.fillText(`Bomb CD: ${bombCooldown.toFixed(1)}s`, canvasWidth - 20, 105);

  // Cursor
  if (hover) {
    ctx.fillStyle = "#fff";
    ctx.font = "14px Arial";
    ctx.textAlign = "left";
    ctx.fillText(
      `x: ${hover.x.toFixed(2)}, y: ${hover.y.toFixed(2)}`,
      10,
      canvasHeight - 10
    );
  }

  requestAnimationFrame(animate);
}

requestAnimationFrame(animate);
