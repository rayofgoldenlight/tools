import runtimeInit, { start_with_async, set_bytecode, reset_state, export_binary, load_binary, set_scene, get_scene_json, set_selected_object, get_audio_count, get_audio_data, clear_audio, push_audio, get_audio_name, set_audio_name, toggle_hud } from "./pkg/runtime.js";
import compilerInit, { init_panic_hook, compile_source_wasm } from "./pkg_compiler/compiler_wasm.js";

const STORAGE = {
  script: "editor.script",
  auto: "editor.autoCompile",
  example: "editor.example",
};

const SOFT_LIMIT_BYTES = 20 * 1024;

const EXAMPLES = {
  movement: `-- Movement example
var speed = 220

function update(dt)
    if key_down("right") then
        obj[0].x = obj[0].x + speed * dt
    end
    if key_down("left") then
        obj[0].x = obj[0].x - speed * dt
    end
    if key_down("down") then
        obj[0].y = obj[0].y + speed * dt
    end
    if key_down("up") then
        obj[0].y = obj[0].y - speed * dt
    end
end
`,
  coins: `-- Coin collection example (works with base.bin objects 0..3)
var speed = 220
var score = 0

var dx = 0
var dy = 0

var coin1 = 0
var coin2 = 0

function update(dt)
    if key_down("right") then
        obj[0].x = obj[0].x + speed * dt
    end
    if key_down("left") then
        obj[0].x = obj[0].x - speed * dt
    end
    if key_down("down") then
        obj[0].y = obj[0].y + speed * dt
    end
    if key_down("up") then
        obj[0].y = obj[0].y - speed * dt
    end

    if coin1 == 0 then
        dx = obj[0].x - obj[1].x
        dy = obj[0].y - obj[1].y

        if dx < 28 then
            if dx > -28 then
                if dy < 28 then
                    if dy > -28 then
                        score = score + 1
                        coin1 = 1
                        obj[1].x = -1000
                        obj[1].y = -1000
                    end
                end
            end
        end
    end

    if coin2 == 0 then
        dx = obj[0].x - obj[2].x
        dy = obj[0].y - obj[2].y

        if dx < 28 then
            if dx > -28 then
                if dy < 28 then
                    if dy > -28 then
                        score = score + 1
                        coin2 = 1
                        obj[2].x = -1000
                        obj[2].y = -1000
                    end
                end
            end
        end
    end

    if score == 2 then
        obj[3].x = obj[0].x + 60
        obj[3].y = obj[0].y
    end
end
`,
};

// ── Scene data (shapes + objects managed by editor) ─────
const scene = {
  shapes: [],   // { type, mode, fillColor, strokeColor, strokeWidth, radius?, width?, height? }
  animations: [],
  audio: [],
  objects: [],  // { shapeIndex, x, y, rotation, scale }
};

// ── Web Audio System ──────────────────────
const audioCtx = new (window.AudioContext || window.webkitAudioContext)();
const audioBuffers = []; // Stores decoded audio ready for playback
const activeAudioNodes = {}; // Tracks playing sounds: id -> Set of source nodes

window.play_audio_js = function(id) {
  if (audioCtx.state === 'suspended') audioCtx.resume();
  const buffer = audioBuffers[id];
  if (!buffer) return;
  
  const source = audioCtx.createBufferSource();
  source.buffer = buffer;
  
  // Future proofing for fading: route through a GainNode
  const gainNode = audioCtx.createGain();
  source.connect(gainNode);
  gainNode.connect(audioCtx.destination);
  
  source.start(0);

  // Track the playing node so we can stop it
  if (!activeAudioNodes[id]) activeAudioNodes[id] = new Set();
  const instance = { source, gainNode };
  activeAudioNodes[id].add(instance);

  // Cleanup when sound finishes naturally
  source.onended = () => {
    if (activeAudioNodes[id]) activeAudioNodes[id].delete(instance);
  };
};

window.stop_audio_js = function(id) {
  if (!activeAudioNodes[id]) return;
  activeAudioNodes[id].forEach(instance => {
    try { instance.source.stop(); } catch(e) {}
  });
  activeAudioNodes[id].clear();
};

window.loop_audio_js = function(id) {
  // If it is already playing/looping, prevent starting it again
  if (activeAudioNodes[id] && activeAudioNodes[id].size > 0) return;
  
  if (audioCtx.state === 'suspended') audioCtx.resume();
  const buffer = audioBuffers[id];
  if (!buffer) return;
  
  const source = audioCtx.createBufferSource();
  source.buffer = buffer;
  source.loop = true; // Loop it forever
  
  const gainNode = audioCtx.createGain();
  source.connect(gainNode);
  gainNode.connect(audioCtx.destination);
  
  source.start(0);

  if (!activeAudioNodes[id]) activeAudioNodes[id] = new Set();
  const instance = { source, gainNode };
  activeAudioNodes[id].add(instance);

  source.onended = () => {
    if (activeAudioNodes[id]) activeAudioNodes[id].delete(instance);
  };
};

window.set_volume_js = function(id, vol) {
  if (!activeAudioNodes[id]) return;
  
  // Clamp volume between 0.0 (silent) and 1.0 (full volume)
  const safeVol = Math.max(0, Math.min(1, vol));
  
  activeAudioNodes[id].forEach(instance => {
    // Smoothly transition the volume over 50ms to prevent popping/clicking
    instance.gainNode.gain.setTargetAtTime(safeVol, audioCtx.currentTime, 0.05);
  });
};

function stopAllAudio() {
  Object.keys(activeAudioNodes).forEach(id => window.stop_audio_js(id));
}

async function reloadAudioFromRuntime() {
  audioBuffers.length = 0; // clear existing
  const count = get_audio_count();
  for (let i = 0; i < count; i++) {
    const uint8 = get_audio_data(i);
    try {
      // Slice it to extract only this specific audio file's bytes.
      const audioBytes = uint8.buffer.slice(
        uint8.byteOffset, 
        uint8.byteOffset + uint8.byteLength
      );
      
      const buffer = await audioCtx.decodeAudioData(audioBytes);
      audioBuffers.push(buffer);
    } catch (e) {
      console.error(`Failed to decode audio ${i}:`, e);
      audioBuffers.push(null);
    }
  }
  console.log(`Loaded ${audioBuffers.length} audio buffers.`);
}

async function compressAudioToMp3(file, targetSampleRate = 22050, kbps = 32) {
  const arrayBuffer = await file.arrayBuffer();
  
  // 1. Decode original file (handles wav, ogg, mp3, etc.)
  const tempCtx = new (window.AudioContext || window.webkitAudioContext)();
  const audioBuffer = await tempCtx.decodeAudioData(arrayBuffer);

  // 2. Downsample and mix to Mono using OfflineAudioContext
  const offlineCtx = new OfflineAudioContext(1, (audioBuffer.duration * targetSampleRate), targetSampleRate);
  const source = offlineCtx.createBufferSource();
  source.buffer = audioBuffer;
  source.connect(offlineCtx.destination);
  source.start(0);
  const renderedBuffer = await offlineCtx.startRendering();

  // 3. Convert Float32 audio data to Int16 for lamejs
  const floatData = renderedBuffer.getChannelData(0);
  const intData = new Int16Array(floatData.length);
  for (let i = 0; i < floatData.length; i++) {
    let s = Math.max(-1, Math.min(1, floatData[i]));
    intData[i] = s < 0 ? s * 0x8000 : s * 0x7FFF;
  }

  // 4. Encode to low-bitrate MP3
  const lamejs = window.lamejs;
  const encoder = new lamejs.Mp3Encoder(1, targetSampleRate, kbps);
  const mp3Data = [];
  const sampleBlockSize = 1152; // standard MP3 frame size

  for (let i = 0; i < intData.length; i += sampleBlockSize) {
    const sampleChunk = intData.subarray(i, i + sampleBlockSize);
    const mp3buf = encoder.encodeBuffer(sampleChunk);
    if (mp3buf.length > 0) mp3Data.push(mp3buf);
  }
  const mp3buf = encoder.flush();
  if (mp3buf.length > 0) mp3Data.push(mp3buf);

  // 5. Combine chunks into final Uint8Array
  const totalLength = mp3Data.reduce((acc, val) => acc + val.length, 0);
  const finalMp3 = new Uint8Array(totalLength);
  let offset = 0;
  for (const chunk of mp3Data) {
    finalMp3.set(chunk, offset);
    offset += chunk.length;
  }

  return finalMp3;
}

let editingShapeIndex = -1; // -1 = new, >= 0 = editing existing
let editingAnimIndex = -1; 
let currentAnimFrames = []; 
let animPreviewHandle = null;
let editingObjectIndex = -1; // -1 = new, >= 0 = editing existing

function fmtMs(ms) {
  return `${ms.toFixed(1)}ms`;
}

function loadBool(key, fallback) {
  const v = localStorage.getItem(key);
  if (v === null) return fallback;
  return v === "1";
}

function saveBool(key, val) {
  localStorage.setItem(key, val ? "1" : "0");
}

function hexToRgb(hex) {
  const r = parseInt(hex.slice(1, 3), 16);
  const g = parseInt(hex.slice(3, 5), 16);
  const b = parseInt(hex.slice(5, 7), 16);
  return { r, g, b, a: 255 };
}

function rgbToHex(c) {
  const h = (v) => v.toString(16).padStart(2, "0");
  return `#${h(c.r)}${h(c.g)}${h(c.b)}`;
}

function drawShapeSwatch(canvas, shape) {
  const ctx = canvas.getContext("2d");
  const w = canvas.width;
  const h = canvas.height;
  ctx.clearRect(0, 0, w, h);

  ctx.save();
  ctx.translate(w / 2, h / 2);

  const fill = `rgba(${shape.fillColor.r},${shape.fillColor.g},${shape.fillColor.b},${shape.fillColor.a / 255})`;
  const stroke = `rgba(${shape.strokeColor.r},${shape.strokeColor.g},${shape.strokeColor.b},${shape.strokeColor.a / 255})`;

  if (shape.type === "circle") {
    const r = Math.min(shape.radius || 20, Math.min(w, h) / 2 - 4);
    ctx.beginPath();
    ctx.arc(0, 0, r, 0, Math.PI * 2);
    if (shape.mode === "fill" || shape.mode === "both") {
      ctx.fillStyle = fill;
      ctx.fill();
    }
    if (shape.mode === "stroke" || shape.mode === "both") {
      ctx.strokeStyle = stroke;
      ctx.lineWidth = shape.strokeWidth || 1;
      ctx.stroke();
    }
  } else if (shape.type === "rect") {
    const sw = Math.min(shape.width || 40, w - 8);
    const sh = Math.min(shape.height || 40, h - 8);
    if (shape.mode === "fill" || shape.mode === "both") {
      ctx.fillStyle = fill;
      ctx.fillRect(-sw / 2, -sh / 2, sw, sh);
    }
    if (shape.mode === "stroke" || shape.mode === "both") {
      ctx.strokeStyle = stroke;
      ctx.lineWidth = shape.strokeWidth || 1;
      ctx.strokeRect(-sw / 2, -sh / 2, sw, sh);
    }
  } else if (shape.type === "path") {
    ctx.beginPath();
    for (const cmd of (shape.commands || [])) {
      if (cmd.type === "move") ctx.moveTo(cmd.x, cmd.y);
      else if (cmd.type === "line") ctx.lineTo(cmd.x, cmd.y);
      else if (cmd.type === "quad") ctx.quadraticCurveTo(cmd.cx, cmd.cy, cmd.x, cmd.y);
      else if (cmd.type === "close") ctx.closePath();
      else if (cmd.type === "fill") {
        ctx.fillStyle = `rgba(${cmd.r},${cmd.g},${cmd.b},${cmd.a/255})`;
        ctx.fill();
        ctx.beginPath();
      }
      else if (cmd.type === "stroke") {
        ctx.strokeStyle = `rgba(${cmd.r},${cmd.g},${cmd.b},${cmd.a/255})`;
        ctx.lineWidth = cmd.w || 1;
        ctx.stroke();
        ctx.beginPath();
      }
    }
    if (shape.mode === "fill" || shape.mode === "both") {
      ctx.fillStyle = fill; ctx.fill();
    }
    if (shape.mode === "stroke" || shape.mode === "both") {
      ctx.strokeStyle = stroke; ctx.lineWidth = shape.strokeWidth || 1; ctx.stroke();
    }
  } else if (shape.type === "text") {
    const size = shape.size || 24;
    ctx.font = `bold ${size}px sans-serif`;
    ctx.textAlign = shape.align === 0 ? "left" : (shape.align === 2 ? "right" : "center");
    ctx.textBaseline = "middle";
    if (shape.mode === "fill" || shape.mode === "both") {
      ctx.fillStyle = fill;
      ctx.fillText(shape.text || "Text", 0, 0);
    }
    if (shape.mode === "stroke" || shape.mode === "both") {
      ctx.strokeStyle = stroke;
      ctx.lineWidth = shape.strokeWidth || 1;
      ctx.strokeText(shape.text || "Text", 0, 0);
    }
  }

  ctx.restore();
}

async function main() {
  const statusEl = document.getElementById("status");
  const statsEl = document.getElementById("stats");
  const errorsEl = document.getElementById("errors");
  const textarea = document.getElementById("script");

  const compileBtn = document.getElementById("compileBtn");
  const resetBtn = document.getElementById("resetBtn");
  const autoCompileEl = document.getElementById("autoCompile");
  const exampleSelect = document.getElementById("exampleSelect");

  const exportBtn = document.getElementById("exportBtn");

  const importBtn = document.getElementById("importBtn");
  const importFile = document.getElementById("importFile");

  const objManagerBtn = document.getElementById("objManagerBtn");
  const objModal = document.getElementById("objModal");
  const objModalClose = document.getElementById("objModalClose");

  const animForm = document.getElementById("animForm");
  const animFormTitle = document.getElementById("animFormTitle");
  const animList = document.getElementById("animList");
  const addAnimBtn = document.getElementById("addAnimBtn");
  const afFps = document.getElementById("af-fps");
  const afTimeline = document.getElementById("af-timeline");
  const afShapeSelect = document.getElementById("af-shape-select");
  const afAddFrame = document.getElementById("af-add-frame");
  const afBulkFrames = document.getElementById("af-bulk-frames");
  const afAddBulk = document.getElementById("af-add-bulk");
  const afPreview = document.getElementById("af-preview");
  const afSave = document.getElementById("af-save");
  const afCancel = document.getElementById("af-cancel");
  const ofAnim = document.getElementById("of-anim"); // Dropdown on object form

  // Restore prefs
  autoCompileEl.checked = loadBool(STORAGE.auto, true);

  const savedExample = localStorage.getItem(STORAGE.example) || "movement";
  exampleSelect.value = EXAMPLES[savedExample] ? savedExample : "movement";

  // Restore script (if present), else use selected example
  const savedScript = localStorage.getItem(STORAGE.script);
  textarea.value = (savedScript && savedScript.trim().length > 0)
    ? savedScript
    : EXAMPLES[exampleSelect.value];

  statusEl.textContent = "Loading runtime.wasm...";
  await runtimeInit();

  statusEl.textContent = "Loading compiler.wasm...";
  await compilerInit();
  init_panic_hook();

  errorsEl.textContent = "";
  statsEl.textContent = "Bytecode: (none)";

  statusEl.textContent = "Starting preview from base.bin...";
  await start_with_async("preview-canvas", "base.bin");
  await reloadAudioFromRuntime();

  let lastGood = null; // { bytes, vars }
  let compileTimer = null;
  let compileSeq = 0;
  const DEBOUNCE_MS = 400;

  function setStatusOk(text) {
    statusEl.style.color = "#9aa4c7";
    statusEl.textContent = text;
  }
  function setStatusErr(text) {
    statusEl.style.color = "#ff6b6b";
    statusEl.textContent = text;
  }

  function updateStats(byteLen, varCount) {
    const pct = Math.min(999, Math.round((byteLen / SOFT_LIMIT_BYTES) * 100));
    statsEl.textContent = `Bytecode: ${byteLen} bytes • vars=${varCount} • ${pct}% of ${SOFT_LIMIT_BYTES}B soft limit`;
    statsEl.style.color = byteLen > SOFT_LIMIT_BYTES ? "#ff6b6b" : "#9aa4c7";
  }

  function compileAndReload(reason) {
    const t0 = performance.now();
    try {
      stopAllAudio();

      const result = compile_source_wasm(textarea.value);
      const bytecode = result.bytecode; // Uint8Array
      const varCount = (Number(result.variable_count) | 0);

      set_bytecode(bytecode, varCount, result.behaviors);

      const t1 = performance.now();
      lastGood = { bytes: bytecode.length, vars: varCount };

      errorsEl.textContent = "";
      updateStats(bytecode.length, varCount);
      setStatusOk(`OK (${reason}) • ${fmtMs(t1 - t0)}`);

      return true;
    } catch (e) {
      const t1 = performance.now();
      errorsEl.textContent = String(e);

      if (lastGood) {
        updateStats(lastGood.bytes, lastGood.vars);
        setStatusErr(`ERROR (${reason}) • keeping last good • ${fmtMs(t1 - t0)}`);
      } else {
        setStatusErr(`ERROR (${reason}) • ${fmtMs(t1 - t0)}`);
      }
      return false;
    }
  }

  function scheduleCompile() {
    localStorage.setItem(STORAGE.script, textarea.value);

    if (!autoCompileEl.checked) return;

    compileSeq++;
    const mySeq = compileSeq;
    if (compileTimer) clearTimeout(compileTimer);

    compileTimer = setTimeout(() => {
      if (mySeq !== compileSeq) return;
      compileAndReload("auto");
    }, DEBOUNCE_MS);
  }

  // Events
  textarea.addEventListener("input", scheduleCompile);

  autoCompileEl.addEventListener("change", () => {
    saveBool(STORAGE.auto, autoCompileEl.checked);
    if (autoCompileEl.checked) scheduleCompile();
  });

  exampleSelect.addEventListener("change", () => {
    const key = exampleSelect.value;
    localStorage.setItem(STORAGE.example, key);

    textarea.value = EXAMPLES[key];
    localStorage.setItem(STORAGE.script, textarea.value);

    // Compile immediately so preview matches example
    compileSeq++;
    if (compileTimer) clearTimeout(compileTimer);
    compileAndReload("example");
  });

  compileBtn.onclick = () => {
    compileSeq++;
    if (compileTimer) clearTimeout(compileTimer);
    compileAndReload("manual");
  };

  resetBtn.onclick = () => {
    try {
      stopAllAudio();
      reset_state();
      setStatusOk("Reset OK");
    } catch (e) {
      setStatusErr("Reset failed: " + String(e));
    }
  };

  // ── Object Manager modal ──────────────────

  objManagerBtn.onclick = () => {
    objModal.style.display = "flex";
    set_selected_object(-1);
  };

  objModalClose.onclick = () => {
    stopAllAudio();
    objModal.style.display = "none";
  };

  // Close modal on backdrop click
  objModal.addEventListener("click", (e) => {
    if (e.target === objModal) {
      stopAllAudio();
      objModal.style.display = "none";
    }
  });

  // Close modal on Escape
  window.addEventListener("keydown", (e) => {
    if (e.key === "Escape" && objModal.style.display !== "none") {
      stopAllAudio();
      objModal.style.display = "none";
    }
  });

  // Tab switching
  document.querySelectorAll(".modal-tab").forEach((tab) => {
    tab.addEventListener("click", () => {
      document.querySelectorAll(".modal-tab").forEach((t) => t.classList.remove("active"));
      document.querySelectorAll(".tab-content").forEach((c) => c.classList.remove("active"));

      tab.classList.add("active");
      const target = document.getElementById("tab-" + tab.dataset.tab);
      if (target) target.classList.add("active");
    });
  });

  // ── Shape editor ──────────────────────────

  const shapeForm = document.getElementById("shapeForm");
  const shapeFormTitle = document.getElementById("shapeFormTitle");
  const shapeList = document.getElementById("shapeList");
  const addShapeBtn = document.getElementById("addShapeBtn");

  const sfType = document.getElementById("sf-type");
  const sfMode = document.getElementById("sf-mode");
  const sfFill = document.getElementById("sf-fill");
  const sfStroke = document.getElementById("sf-stroke");
  const sfStrokeW = document.getElementById("sf-strokeW");
  const sfRadius = document.getElementById("sf-radius");
  const sfRadiusLabel = document.getElementById("sf-radius-label");
  const sfWidth = document.getElementById("sf-width");
  const sfWidthLabel = document.getElementById("sf-width-label");
  const sfHeight = document.getElementById("sf-height");
  const sfHeightLabel = document.getElementById("sf-height-label");
  const sfText = document.getElementById("sf-text"); 
  const sfTextLabel = document.getElementById("sf-text-label"); 
  const sfSize = document.getElementById("sf-size");
  const sfSizeLabel = document.getElementById("sf-size-label");
  const sfAlign = document.getElementById("sf-align"); 
  const sfAlignLabel = document.getElementById("sf-align-label");
  const sfPreview = document.getElementById("sf-preview");
  const sfSave = document.getElementById("sf-save");
  const sfCancel = document.getElementById("sf-cancel");

  function updateShapeParamVisibility() {
    const type = sfType.value;
    sfRadiusLabel.style.display = type === "circle" ? "" : "none";
    sfWidthLabel.style.display = type === "rect" ? "" : "none";
    sfHeightLabel.style.display = type === "rect" ? "" : "none";
    sfTextLabel.style.display = type === "text" ? "" : "none";
    sfSizeLabel.style.display = type === "text" ? "" : "none";
    sfAlignLabel.style.display = type === "text" ? "" : "none";
  }

  function getShapeFromForm() {
    const shape = {
      type: sfType.value,
      mode: sfMode.value,
      fillColor: hexToRgb(sfFill.value),
      strokeColor: hexToRgb(sfStroke.value),
      strokeWidth: parseFloat(sfStrokeW.value) || 1,
    };
    if (shape.type === "circle") {
      shape.radius = parseFloat(sfRadius.value) || 20;
    } else if (shape.type === "rect") {
      shape.width = parseFloat(sfWidth.value) || 40;
      shape.height = parseFloat(sfHeight.value) || 40;
    } else if (shape.type === "text") {
      shape.text = sfText.value || "Text";
      shape.size = parseFloat(sfSize.value) || 24;
      shape.align = parseInt(sfAlign.value) || 1;
    }
    return shape;
  }

  function loadShapeIntoForm(shape) {
    sfType.value = shape.type;
    sfMode.value = shape.mode;
    sfFill.value = rgbToHex(shape.fillColor);
    sfStroke.value = rgbToHex(shape.strokeColor);
    sfStrokeW.value = shape.strokeWidth;
    sfRadius.value = shape.radius || 20;
    sfWidth.value = shape.width || 40;
    sfHeight.value = shape.height || 40;
    sfText.value = shape.text || "Hello";
    sfSize.value = shape.size || 24;
    sfAlign.value = shape.align ?? 1;
    updateShapeParamVisibility();
    updateFormPreview();
  }

  function updateFormPreview() {
    drawShapeSwatch(sfPreview, getShapeFromForm());
  }

  function renderShapeList() {
    if (scene.shapes.length === 0) {
      shapeList.innerHTML = '<div class="item-list-empty">No shapes defined. Click "+ Add Shape" to start.</div>';
      return;
    }

    shapeList.innerHTML = "";
    scene.shapes.forEach((shape, i) => {
      const card = document.createElement("div");
      card.className = "item-card";

      // Swatch
      const swatch = document.createElement("canvas");
      swatch.className = "swatch";
      swatch.width = 32;
      swatch.height = 32;
      drawShapeSwatch(swatch, shape);

      // Info
      const info = document.createElement("div");
      info.className = "item-info";

      const name = document.createElement("div");
      name.className = "item-name";
      name.textContent = `Shape ${i}`;

      const detail = document.createElement("div");
      detail.className = "item-detail";
      if (shape.type === "circle") {
        detail.textContent = `${shape.type} • r=${shape.radius} • ${shape.mode}`;
      } else if (shape.type === "rect") {
        detail.textContent = `${shape.type} • ${shape.width}×${shape.height} • ${shape.mode}`;
      } else if (shape.type === "path") {
        detail.textContent = `path • ${shape.commands ? shape.commands.length : 0} points`;
      } else if (shape.type === "text") {
        detail.textContent = `text • "${(shape.text||"").substring(0, 10)}" • ${shape.size}px`;
      }

      info.appendChild(name);
      info.appendChild(detail);

      // Actions
      const actions = document.createElement("div");
      actions.className = "item-actions";

      const editBtn = document.createElement("button");
      editBtn.textContent = "Edit";
      editBtn.addEventListener("click", () => {
        editingShapeIndex = i;
        shapeFormTitle.textContent = `Edit Shape ${i}`;
        loadShapeIntoForm(shape);
        shapeForm.style.display = "";
      });

      const delBtn = document.createElement("button");
      delBtn.textContent = "Del";
      delBtn.className = "danger";
      delBtn.addEventListener("click", () => {
        // Check if any objects use this shape
        const usedBy = scene.objects.filter((o) => o.shapeIndex === i);
        if (usedBy.length > 0) {
          alert(`Cannot delete: ${usedBy.length} object(s) use this shape.`);
          return;
        }
        scene.shapes.splice(i, 1);
        // Fix object indices that pointed to shapes after this one
        scene.objects.forEach((o) => {
          if (o.shapeIndex > i) o.shapeIndex--;
        });
        renderShapeList();
        renderObjectList();
        syncScene();
      });

      actions.appendChild(editBtn);
      actions.appendChild(delBtn);

      card.appendChild(swatch);
      card.appendChild(info);
      card.appendChild(actions);
      shapeList.appendChild(card);
    });
  }

  // Form events
  sfType.addEventListener("change", () => {
    updateShapeParamVisibility();
    updateFormPreview();
  });
  [sfMode, sfFill, sfStroke, sfStrokeW, sfRadius, sfWidth, sfHeight, sfText, sfSize, sfAlign].forEach((el) => {
    el.addEventListener("input", updateFormPreview);
  });

  addShapeBtn.addEventListener("click", () => {
    editingShapeIndex = -1;
    shapeFormTitle.textContent = "New Shape";
    loadShapeIntoForm({
      type: "circle", mode: "fill",
      fillColor: { r: 0, g: 255, b: 136, a: 255 },
      strokeColor: { r: 255, g: 255, b: 255, a: 255 },
      strokeWidth: 2, radius: 20,
    });
    shapeForm.style.display = "";
  });

  sfSave.addEventListener("click", () => {
    const shape = getShapeFromForm();
    if (editingShapeIndex >= 0) {
      scene.shapes[editingShapeIndex] = shape;
    } else {
      scene.shapes.push(shape);
    }
    shapeForm.style.display = "none";
    renderShapeList();
    renderObjectList();
    syncScene();
  });

  sfCancel.addEventListener("click", () => {
    shapeForm.style.display = "none";
  });

  // Initial render
  renderShapeList();

  // ── Vector Drawing Tool ───────────────────
  const drawShapeBtn = document.getElementById("drawShapeBtn");
  const drawModal = document.getElementById("drawModal");
  const drawModalClose = document.getElementById("drawModalClose");
  const drawCanvas = document.getElementById("drawCanvas");
  const drawCtx = drawCanvas.getContext("2d");
  
  const drawBrushBtn = document.getElementById("drawBrushBtn");
  const drawEraserBtn = document.getElementById("drawEraserBtn");
  const drawUndoBtn = document.getElementById("drawUndoBtn");
  const drawClearBtn = document.getElementById("drawClearBtn");
  const drawOnionSkin = document.getElementById("drawOnionSkin");
  const drawSaveBtn = document.getElementById("drawSaveBtn");
  const drawCancelBtn = document.getElementById("drawCancelBtn");

  const drawColorPicker = document.getElementById("drawColorPicker");
  let drawStrokes = []; // Array of arrays of {x, y}
  let currentStroke = null;
  let drawTool = "brush"; // "brush" or "eraser"

  function renderDrawCanvas() {
    drawCtx.clearRect(0, 0, drawCanvas.width, drawCanvas.height);
    
    // Draw crosshair center
    drawCtx.strokeStyle = "#2a2f45"; drawCtx.lineWidth = 1;
    drawCtx.beginPath();
    drawCtx.moveTo(100, 0); drawCtx.lineTo(100, 200);
    drawCtx.moveTo(0, 100); drawCtx.lineTo(200, 100);
    drawCtx.stroke();

    // Onion Skin: Draw the previous shape faintly
    if (drawOnionSkin.checked && scene.shapes.length > 0) {
      // Pick the shape right before the one we are editing, or the last shape if new
      let targetIdx = editingShapeIndex >= 0 ? editingShapeIndex - 1 : scene.shapes.length - 1;
      if (targetIdx >= 0 && scene.shapes[targetIdx]) {
        drawCtx.save();
        drawCtx.globalAlpha = 0.3;

        // drawShapeSwatch translates to the center of the canvas passed to it.
        // Just pass the exact dimensions of our 200x200 drawCanvas.
        const tempShape = JSON.parse(JSON.stringify(scene.shapes[targetIdx]));
        
        // Force rendering in a faint blue for the onion skin
        tempShape.strokeColor = {r: 100, g: 200, b: 255, a: 255};
        tempShape.fillColor = {r: 100, g: 200, b: 255, a: 255};
        
        // Create a fake canvas object that forwards calls to our drawCtx, 
        // but reports its width/height as 200 so drawShapeSwatch centers correctly.
        const fakeCanvas = {
          width: 200,
          height: 200,
          getContext: () => drawCtx
        };
        
        drawShapeSwatch(fakeCanvas, tempShape);
        drawCtx.restore();
      }
    }
    // Draw active strokes
    drawCtx.strokeStyle = "#ffffff";
    drawCtx.lineWidth = 2;
    drawCtx.lineCap = "round";
    drawCtx.lineJoin = "round";

    for (const stroke of drawStrokes) {
      if (stroke.points.length === 0) continue;
      drawCtx.strokeStyle = stroke.color;
      drawCtx.beginPath();
      drawCtx.moveTo(stroke.points[0].x + 100, stroke.points[0].y + 100);
      for (let i = 1; i < stroke.points.length; i++) {
        drawCtx.lineTo(stroke.points[i].x + 100, stroke.points[i].y + 100);
      }
      drawCtx.stroke();
    }

    if (currentStroke && currentStroke.points.length > 0) {
      drawCtx.strokeStyle = currentStroke.color;
      drawCtx.beginPath();
      drawCtx.moveTo(currentStroke.points[0].x + 100, currentStroke.points[0].y + 100);
      for (let i = 1; i < currentStroke.points.length; i++) {
        drawCtx.lineTo(currentStroke.points[i].x + 100, currentStroke.points[i].y + 100);
      }
      drawCtx.stroke();
    }
  }

  function getDrawCoords(e) {
    const rect = drawCanvas.getBoundingClientRect();
    const scaleX = drawCanvas.width / rect.width;
    const scaleY = drawCanvas.height / rect.height;
    // Offset by -100 so (0,0) is the center of the canvas
    return {
      x: (e.clientX - rect.left) * scaleX - 100,
      y: (e.clientY - rect.top) * scaleY - 100,
    };
  }

    drawCanvas.addEventListener("mousedown", (e) => {
    const pt = getDrawCoords(e);
    if (drawTool === "brush") {
      currentStroke = { color: drawColorPicker.value, points: [pt] };
    } else if (drawTool === "eraser") {
      for (let i = drawStrokes.length - 1; i >= 0; i--) {
        const stroke = drawStrokes[i];
        for (const p of stroke.points) {
          const dx = p.x - pt.x; const dy = p.y - pt.y;
          if (Math.sqrt(dx*dx + dy*dy) < 10) {
            drawStrokes.splice(i, 1);
            renderDrawCanvas();
            return;
          }
        }
      }
    }
    renderDrawCanvas();
  });

  drawCanvas.addEventListener("mousemove", (e) => {
    if (currentStroke && drawTool === "brush") {
      currentStroke.points.push(getDrawCoords(e));
      renderDrawCanvas();
    } else if (e.buttons === 1 && drawTool === "eraser") {
      const pt = getDrawCoords(e);
      for (let i = drawStrokes.length - 1; i >= 0; i--) {
        const stroke = drawStrokes[i];
        for (const p of stroke.points) {
          const dx = p.x - pt.x; const dy = p.y - pt.y;
          if (Math.sqrt(dx*dx + dy*dy) < 10) {
            drawStrokes.splice(i, 1);
            renderDrawCanvas();
            break;
          }
        }
      }
    }
  });

  window.addEventListener("mouseup", () => {
    if (currentStroke) {
      drawStrokes.push(currentStroke);
      currentStroke = null;
      renderDrawCanvas();
    }
  });

  // Tools
  drawBrushBtn.onclick = () => { drawTool = "brush"; drawBrushBtn.className = "tool-btn active"; drawEraserBtn.className = "tool-btn"; };
  drawEraserBtn.onclick = () => { drawTool = "eraser"; drawEraserBtn.className = "tool-btn active"; drawBrushBtn.className = "tool-btn"; };
  drawUndoBtn.onclick = () => { drawStrokes.pop(); renderDrawCanvas(); };
  drawClearBtn.onclick = () => { drawStrokes = []; renderDrawCanvas(); };
  drawOnionSkin.onchange = () => renderDrawCanvas();

  // Open / Close
  drawShapeBtn.onclick = () => {
    editingShapeIndex = -1;
    drawStrokes = [];
    drawModal.style.display = "flex";
    renderDrawCanvas();
  };

  drawCancelBtn.onclick = drawModalClose.onclick = () => {
    drawModal.style.display = "none";
  };

  // Convert strokes to PathCmds and Save
  drawSaveBtn.onclick = () => {
    const commands = [];
    for (const stroke of drawStrokes) {
      if (stroke.points.length === 0) continue;
      
      commands.push({ type: "move", x: Math.round(stroke.points[0].x), y: Math.round(stroke.points[0].y) });
      
      let lastP = stroke.points[0];
      for (let i = 1; i < stroke.points.length; i++) {
        const p = stroke.points[i];
        const dist = Math.sqrt(Math.pow(p.x - lastP.x, 2) + Math.pow(p.y - lastP.y, 2));
        if (dist > 2 || i === stroke.points.length - 1) {
          commands.push({ type: "line", x: Math.round(p.x), y: Math.round(p.y) });
          lastP = p;
        }
      }
      // Attach the stroke command so this specific sub-path renders with its own color
      const rgb = hexToRgb(stroke.color);
      commands.push({ type: "stroke", r: rgb.r, g: rgb.g, b: rgb.b, a: 255, w: 2 });
    }

    const newShape = {
      type: "path",
      mode: "both", // 'both' so standard engine path doesn't hide it
      fillColor: { r: 0, g: 0, b: 0, a: 0 },
      strokeColor: { r: 255, g: 255, b: 255, a: 0 }, // Transparent base, colors are handled by the path itself
      strokeWidth: 2,
      commands: commands
    };

    if (editingShapeIndex >= 0) {
      scene.shapes[editingShapeIndex] = newShape;
    } else {
      scene.shapes.push(newShape);
    }

    drawModal.style.display = "none";
    renderShapeList();
    renderObjectList();
    syncScene();
  };

  // ── Animation editor ──────────────────────

  function renderTimeline() {
    afTimeline.innerHTML = "";
    currentAnimFrames.forEach((shapeIdx, i) => {
      const box = document.createElement("div");
      box.className = "timeline-frame";
      
      const canvas = document.createElement("canvas");
      canvas.width = 32; canvas.height = 32;
      if (scene.shapes[shapeIdx]) drawShapeSwatch(canvas, scene.shapes[shapeIdx]);
      
      const delBtn = document.createElement("button");
      delBtn.className = "del-frame";
      delBtn.textContent = "×";
      delBtn.onclick = () => { currentAnimFrames.splice(i, 1); renderTimeline(); };
      
      box.appendChild(canvas);
      box.appendChild(delBtn);
      afTimeline.appendChild(box);
    });
  }

  function playAnimPreview() {
    if (animPreviewHandle) cancelAnimationFrame(animPreviewHandle);
    let lastTime = performance.now();
    let frameIdx = 0;
    
    function loop(now) {
      if (animForm.style.display === "none") return;
      const fps = parseFloat(afFps.value) || 12;
      const ctx = afPreview.getContext("2d");
      ctx.clearRect(0, 0, afPreview.width, afPreview.height);

      if (currentAnimFrames.length > 0 && fps > 0) {
        const dt = now - lastTime;
        const frameDur = 1000 / fps;
        if (dt >= frameDur) {
          frameIdx = (frameIdx + Math.floor(dt / frameDur)) % currentAnimFrames.length;
          lastTime = now - (dt % frameDur);
        }
        const shape = scene.shapes[currentAnimFrames[frameIdx]];
        if (shape) drawShapeSwatch(afPreview, shape);
      }
      animPreviewHandle = requestAnimationFrame(loop);
    }
    animPreviewHandle = requestAnimationFrame(loop);
  }

  function renderAnimList() {
    if (scene.animations.length === 0) {
      animList.innerHTML = '<div class="item-list-empty">No animations defined.</div>';
      return;
    }
    animList.innerHTML = "";
    scene.animations.forEach((anim, i) => {
      const card = document.createElement("div");
      card.className = "item-card";

      const swatch = document.createElement("canvas");
      swatch.className = "swatch"; swatch.width = 32; swatch.height = 32;
      if (anim.frames.length > 0 && scene.shapes[anim.frames[0]]) {
        drawShapeSwatch(swatch, scene.shapes[anim.frames[0]]);
      }

      const info = document.createElement("div");
      info.className = "item-info";
      info.innerHTML = `<div class="item-name">Animation ${i}</div><div class="item-detail">${anim.frames.length} frames • ${anim.fps} fps</div>`;

      const actions = document.createElement("div");
      actions.className = "item-actions";
      
      const editBtn = document.createElement("button");
      editBtn.textContent = "Edit";
      editBtn.onclick = () => {
        editingAnimIndex = i;
        animFormTitle.textContent = `Edit Animation ${i}`;
        afFps.value = anim.fps;
        currentAnimFrames = [...anim.frames];
        
        // Populate available shapes
        afShapeSelect.innerHTML = "";
        scene.shapes.forEach((s, idx) => {
          const opt = document.createElement("option"); opt.value = idx; opt.textContent = `Shape ${idx}`;
          afShapeSelect.appendChild(opt);
        });

        animForm.style.display = "";
        renderTimeline();
        playAnimPreview();
      };

      const delBtn = document.createElement("button");
      delBtn.textContent = "Del"; delBtn.className = "danger";
      delBtn.onclick = () => {
        scene.animations.splice(i, 1);
        renderAnimList(); syncScene();
      };

      actions.appendChild(editBtn); actions.appendChild(delBtn);
      card.appendChild(swatch); card.appendChild(info); card.appendChild(actions);
      animList.appendChild(card);
    });
  }

  addAnimBtn.addEventListener("click", () => {
    editingAnimIndex = -1;
    animFormTitle.textContent = "New Animation";
    afFps.value = 12;
    currentAnimFrames = [];
    
    afShapeSelect.innerHTML = "";
    scene.shapes.forEach((s, idx) => {
      const opt = document.createElement("option"); opt.value = idx; opt.textContent = `Shape ${idx}`;
      afShapeSelect.appendChild(opt);
    });

    animForm.style.display = "";
    renderTimeline();
    playAnimPreview();
  });

  afAddFrame.onclick = () => {
    if (afShapeSelect.value !== "") {
      currentAnimFrames.push(parseInt(afShapeSelect.value));
      renderTimeline();
    }
  };

  afAddBulk.onclick = () => {
    const text = afBulkFrames.value.trim();
    if (!text) return;
    
    // Split by comma
    const parts = text.split(",");
    for (let part of parts) {
      part = part.trim();
      if (part.includes("-")) {
        // Handle ranges like 4-20
        const bounds = part.split("-");
        const start = parseInt(bounds[0]);
        const end = parseInt(bounds[1]);
        
        if (!isNaN(start) && !isNaN(end)) {
          // Supports going backwards too, e.g., 20-4
          const step = start <= end ? 1 : -1;
          for (let i = start; start <= end ? i <= end : i >= end; i += step) {
             currentAnimFrames.push(i);
          }
        }
      } else {
        // Handle single numbers like 3
        const n = parseInt(part);
        if (!isNaN(n)) currentAnimFrames.push(n);
      }
    }
    
    renderTimeline();
    afBulkFrames.value = ""; // Clear input after adding
  };

  afSave.onclick = () => {
    const anim = { fps: parseFloat(afFps.value) || 12, frames: [...currentAnimFrames] };
    if (editingAnimIndex >= 0) scene.animations[editingAnimIndex] = anim;
    else scene.animations.push(anim);
    animForm.style.display = "none";
    renderAnimList(); syncScene();
  };

  afCancel.onclick = () => { animForm.style.display = "none"; };

  // ── Audio editor ──────────────────────────

  const audioUpload = document.getElementById("audioUpload");
  const addAudioBtn = document.getElementById("addAudioBtn");
  const audioList = document.getElementById("audioList");

  addAudioBtn.onclick = () => audioUpload.click();

  audioUpload.onchange = async () => {
    const file = audioUpload.files[0];
    if (!file) return;
    
    // Optional: visual feedback
    const originalText = addAudioBtn.textContent;
    addAudioBtn.textContent = "Compressing...";
    addAudioBtn.disabled = true;

    try {
      // Compress everything to 32kbps Mono MP3
      // Change 32 to 16 for more compressiona or to 64 for better quality
      const compressedData = await compressAudioToMp3(file, 22050, 32);
      
      scene.audio.push({ 
        name: file.name + " (Compressed)", 
        format: 1, // 1 = MP3
        data: compressedData 
      });
      
      renderAudioList();
      await syncAudioToRuntime();
    } catch (e) {
      console.error("Audio compression failed:", e);
      alert("Failed to compress audio. See console.");
    } finally {
      addAudioBtn.textContent = originalText;
      addAudioBtn.disabled = false;
      audioUpload.value = "";
    }
  };

  function renderAudioList() {
    if (scene.audio.length === 0) {
      audioList.innerHTML = '<div class="item-list-empty">No audio files uploaded.</div>';
      return;
    }
    audioList.innerHTML = "";
    scene.audio.forEach((aud, i) => {
      const card = document.createElement("div");
      card.className = "item-card";

      const info = document.createElement("div");
      info.className = "item-info";
      info.innerHTML = `<div class="item-name">Sound ${i}: ${aud.name}</div>
                        <div class="item-detail">${(aud.data.length / 1024).toFixed(1)} KB</div>`;

      const actions = document.createElement("div");
      actions.className = "item-actions";

      const playBtn = document.createElement("button");
      playBtn.textContent = "Play";
      playBtn.onclick = () => window.play_audio_js(i); 

      const stopBtn = document.createElement("button");
      stopBtn.textContent = "Stop";
      stopBtn.onclick = () => window.stop_audio_js(i);

      const renameBtn = document.createElement("button");
      renameBtn.textContent = "Rename";
      renameBtn.onclick = async () => {
        const newName = prompt("Enter new audio name:", aud.name);
        if (newName && newName.trim() !== "") {
          aud.name = newName.trim();
          renderAudioList();
          await syncAudioToRuntime();
        }
      };

      const delBtn = document.createElement("button");
      delBtn.textContent = "Del";
      delBtn.className = "danger";
      delBtn.onclick = async () => {
        window.stop_audio_js(i);
        scene.audio.splice(i, 1);
        renderAudioList();
        await syncAudioToRuntime();
      };

      actions.appendChild(playBtn);
      actions.appendChild(stopBtn);
      actions.appendChild(renameBtn);
      actions.appendChild(delBtn);
      card.appendChild(info);
      card.appendChild(actions);
      audioList.appendChild(card);
    });
  }

  async function syncAudioToRuntime() {
    clear_audio();
    for (const aud of scene.audio) {
      push_audio(aud.name, aud.format, aud.data); // <--- Pass aud.name
    }
    await reloadAudioFromRuntime();
  }

  // ── Object editor ─────────────────────────

  const objectForm = document.getElementById("objectForm");
  const objectFormTitle = document.getElementById("objectFormTitle");
  const objectList = document.getElementById("objectList");
  const addObjectBtn = document.getElementById("addObjectBtn");

  const ofShape = document.getElementById("of-shape");
  const ofX = document.getElementById("of-x");
  const ofY = document.getElementById("of-y");
  const ofRot = document.getElementById("of-rot");
  const ofScale = document.getElementById("of-scale");
  const ofSave = document.getElementById("of-save");
  const ofCancel = document.getElementById("of-cancel");

  function populateShapeDropdown() {
    ofShape.innerHTML = "";
    if (scene.shapes.length === 0) {
      ofShape.innerHTML = '<option value="" disabled>(no shapes)</option>';
    } else {
      scene.shapes.forEach((shape, i) => {
        const opt = document.createElement("option"); opt.value = i; opt.textContent = `Shape ${i}`;
        ofShape.appendChild(opt);
      });
    }

    // Populate Animations dropdown
    ofAnim.innerHTML = '<option value="-1">None</option>';
    scene.animations.forEach((anim, i) => {
      const opt = document.createElement("option"); opt.value = i; opt.textContent = `Anim ${i}`;
      ofAnim.appendChild(opt);
    });
  }

  function getObjectFromForm() {
    const animVal = parseInt(ofAnim.value);
    return {
      shapeIndex: parseInt(ofShape.value) || 0,
      animIndex: animVal === -1 ? 65535 : animVal, 
      x: parseFloat(ofX.value) || 0,
      y: parseFloat(ofY.value) || 0,
      rotation: parseFloat(ofRot.value) || 0,
      scale: parseFloat(ofScale.value) || 1,
    };
  }

  function loadObjectIntoForm(obj) {
    populateShapeDropdown();
    ofShape.value = obj.shapeIndex;
    ofAnim.value = (obj.animIndex === undefined || obj.animIndex === 65535) ? -1 : obj.animIndex;
    ofX.value = obj.x;
    ofY.value = obj.y;
    ofRot.value = obj.rotation;
    ofScale.value = obj.scale;
  }

  function shapeLabel(index) {
    const shape = scene.shapes[index];
    if (!shape) return `Shape ${index} (missing!)`;
    if (shape.type === "circle") {
      return `Shape ${index}: Circle r=${shape.radius}`;
    }
    return `Shape ${index}: Rect ${shape.width}×${shape.height}`;
  }

  function renderObjectList() {
    if (scene.objects.length === 0) {
      objectList.innerHTML = '<div class="item-list-empty">No objects placed. Click "+ Add Object" to start.</div>';
      return;
    }

    objectList.innerHTML = "";
    scene.objects.forEach((obj, i) => {
      const card = document.createElement("div");
      card.className = "item-card";

      // Swatch from the object's shape
      const swatch = document.createElement("canvas");
      swatch.className = "swatch";
      swatch.width = 32;
      swatch.height = 32;
      if (scene.shapes[obj.shapeIndex]) {
        drawShapeSwatch(swatch, scene.shapes[obj.shapeIndex]);
      }

      // Info
      const info = document.createElement("div");
      info.className = "item-info";

      const name = document.createElement("div");
      name.className = "item-name";
      name.textContent = `Object ${i}`;

      const detail = document.createElement("div");
      detail.className = "item-detail";
      detail.textContent = `${shapeLabel(obj.shapeIndex)} • (${Math.round(obj.x)}, ${Math.round(obj.y)}) • scale=${obj.scale}`;

      info.appendChild(name);
      info.appendChild(detail);

      // Actions
      const actions = document.createElement("div");
      actions.className = "item-actions";

      const editBtn = document.createElement("button");
      editBtn.textContent = "Edit";
      editBtn.addEventListener("click", () => {
        editingObjectIndex = i;
        objectFormTitle.textContent = `Edit Object ${i}`;
        loadObjectIntoForm(obj);
        objectForm.style.display = "";
        set_selected_object(i);
      });

      const dupBtn = document.createElement("button");
      dupBtn.textContent = "Dup";
      dupBtn.addEventListener("click", () => {
        const copy = { ...obj, x: obj.x + 30, y: obj.y + 30 };
        scene.objects.push(copy);
        renderObjectList();
        syncScene();
      });

      const delBtn = document.createElement("button");
      delBtn.textContent = "Del";
      delBtn.className = "danger";
      delBtn.addEventListener("click", () => {
        scene.objects.splice(i, 1);
        renderObjectList();
        syncScene();
      });

      actions.appendChild(editBtn);
      actions.appendChild(dupBtn);
      actions.appendChild(delBtn);

      card.appendChild(swatch);
      card.appendChild(info);
      card.appendChild(actions);
      objectList.appendChild(card);
    });
  }

  addObjectBtn.addEventListener("click", () => {
    if (scene.shapes.length === 0) {
      alert("Create at least one shape first (Shapes tab).");
      return;
    }
    editingObjectIndex = -1;
    objectFormTitle.textContent = "New Object";
    loadObjectIntoForm({
      shapeIndex: 0,
      x: Math.round(400 + Math.random() * 100 - 50),
      y: Math.round(300 + Math.random() * 100 - 50),
      rotation: 0,
      scale: 1,
    });
    objectForm.style.display = "";
  });

  ofSave.addEventListener("click", () => {
    if (scene.shapes.length === 0) return;
    const obj = getObjectFromForm();
    if (editingObjectIndex >= 0) {
      scene.objects[editingObjectIndex] = obj;
    } else {
      scene.objects.push(obj);
    }
    objectForm.style.display = "none";
    renderObjectList();
    syncScene();
  });

  ofCancel.addEventListener("click", () => {
    objectForm.style.display = "none";
  });

  // Initial render
  renderObjectList();

  // ── Scene sync ────────────────────────────

  function syncScene() {
    try {
      const shapesJson = JSON.stringify(scene.shapes);
      const animsJson = JSON.stringify(scene.animations);
      const objectsJson = JSON.stringify(scene.objects);
      set_scene(shapesJson, animsJson, objectsJson);    
    } catch (e) {
      console.error("Scene sync failed:", e);
    }
  }

  function loadSceneFromRuntime() {
    try {
      const json = get_scene_json();
      const data = JSON.parse(json);
      scene.shapes = data.shapes || [];
      scene.animations = data.animations || []; 
      scene.objects = data.objects || [];

      // Restore audio from WASM state
      scene.audio = [];
      const audioCount = get_audio_count();
      for (let i = 0; i < audioCount; i++) {
         const data = get_audio_data(i);
         const audName = get_audio_name(i); // <--- Get the real name
         const safeData = new Uint8Array(data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength));
         scene.audio.push({ name: audName, format: 1, data: safeData });
      }

      renderShapeList();
      renderAnimList();
      renderAudioList();
      renderObjectList();
    } catch (e) {
      console.error("Failed to load scene from runtime:", e);
    }
  }
  loadSceneFromRuntime();

  // ── Canvas drag & select ──────────────────

  const previewCanvas = document.getElementById("preview-canvas");
  const modeToggleBtn = document.getElementById("modeToggleBtn");
  let dragState = null; // { objectIndex, startX, startY, origX, origY }
  let editorMode = "edit";

  modeToggleBtn.onclick = () => {
    if (editorMode === "edit") {
      editorMode = "play";
      modeToggleBtn.textContent = "Mode: Play (Drag Disabled)";
      modeToggleBtn.style.background = "#00cc6a";
      modeToggleBtn.style.color = "#000";
      set_selected_object(-1); // Clear selection frame
      dragState = null;
    } else {
      editorMode = "edit";
      modeToggleBtn.textContent = "Mode: Edit";
      modeToggleBtn.style.background = "#1e2440";
      modeToggleBtn.style.color = "#eaeaea";
    }
  };

  function hitTestObjects(canvasX, canvasY) {
    // Test in reverse order (top-most object first)
    for (let i = scene.objects.length - 1; i >= 0; i--) {
      const obj = scene.objects[i];
      const shape = scene.shapes[obj.shapeIndex];
      if (!shape) continue;

      const dx = canvasX - obj.x;
      const dy = canvasY - obj.y;
      const scale = obj.scale || 1;

      if (shape.type === "circle") {
        const r = (shape.radius || 20) * scale;
        if (dx * dx + dy * dy <= r * r) return i;
      } else {
        const hw = ((shape.width || 40) * scale) / 2;
        const hh = ((shape.height || 40) * scale) / 2;
        if (Math.abs(dx) <= hw && Math.abs(dy) <= hh) return i;
      }
    }
    return -1;
  }

  function canvasCoords(e) {
    const rect = previewCanvas.getBoundingClientRect();
    const scaleX = previewCanvas.width / rect.width;
    const scaleY = previewCanvas.height / rect.height;
    return {
      x: (e.clientX - rect.left) * scaleX,
      y: (e.clientY - rect.top) * scaleY,
    };
  }

  previewCanvas.addEventListener("mousedown", (e) => {
    if (editorMode !== "edit") return;
    const pos = canvasCoords(e);
    const hit = hitTestObjects(pos.x, pos.y);

    if (hit >= 0) {
      const obj = scene.objects[hit];
      dragState = {
        objectIndex: hit,
        startX: pos.x,
        startY: pos.y,
        origX: obj.x,
        origY: obj.y,
      };
      set_selected_object(hit);
      previewCanvas.style.cursor = "grabbing";
    } else {
      set_selected_object(-1);
      dragState = null;
    }
  });

  previewCanvas.addEventListener("mousemove", (e) => {
    if (editorMode !== "edit") {
      previewCanvas.style.cursor = "default";
      return;
    }

    const pos = canvasCoords(e);

    if (dragState) {
      const dx = pos.x - dragState.startX;
      const dy = pos.y - dragState.startY;

      const obj = scene.objects[dragState.objectIndex];
      obj.x = Math.round(dragState.origX + dx);
      obj.y = Math.round(dragState.origY + dy);

      syncScene();
    } else {
      // Hover cursor
      const hit = hitTestObjects(pos.x, pos.y);
      previewCanvas.style.cursor = hit >= 0 ? "grab" : "default";
    }
  });

  previewCanvas.addEventListener("mouseup", () => {
    if (dragState) {
      // Sync final position to object list UI
      renderObjectList();
      previewCanvas.style.cursor = "grab";
      dragState = null;
    }
  });

  previewCanvas.addEventListener("mouseleave", () => {
    if (dragState) {
      renderObjectList();
      previewCanvas.style.cursor = "default";
      dragState = null;
    }
  });

  exportBtn.onclick = async () => {
    try {
      const originalText = exportBtn.textContent;
      exportBtn.textContent = "Building...";
      exportBtn.disabled = true;

      // 1. Force a strict compile so Bytecode and Script perfectly match
      const compileSuccess = compileAndReload("export_build");
      if (!compileSuccess) {
        alert("Cannot export: Script contains compile errors.");
        exportBtn.textContent = originalText;
        exportBtn.disabled = false;
        return;
      }

      // 2. Ensure audio and scene layouts are 100% synced to the runtime state
      await syncAudioToRuntime();
      syncScene();

      // 3. Generate the final unified .bin (via format::serialize::encode)
      const bytes = export_binary(textarea.value);
      
      // 4. Trigger the download
      const blob = new Blob([bytes], { type: "application/octet-stream" });
      const url = URL.createObjectURL(blob);
      const a = document.createElement("a");
      a.href = url;
      a.download = "game.bin";
      a.click();
      URL.revokeObjectURL(url);

      setStatusOk(`Exported ${bytes.length} bytes successfully.`);
      
      exportBtn.textContent = originalText;
      exportBtn.disabled = false;
    } catch (e) {
      setStatusErr("Export failed: " + String(e));
      exportBtn.textContent = "Export .bin";
      exportBtn.disabled = false;
    }
  };

  importBtn.onclick = () => {
    importFile.click();
  };

  importFile.addEventListener("change", () => {
    const file = importFile.files[0];
    if (!file) return;

    const reader = new FileReader();
    reader.onload = () => {
      const bytes = new Uint8Array(reader.result);
      
      try {
        const script = load_binary(bytes);

        // Restore script to editor if the binary contained one
        if (script && script.length > 0) {
          textarea.value = script;
          localStorage.setItem(STORAGE.script, script);
        }

        // Sync editor scene state from the loaded binary
        loadSceneFromRuntime();
        reloadAudioFromRuntime();

        // Re-compile current script on top of imported scene
        compileAndReload("import");

        setStatusOk(`Imported ${file.name} (${bytes.length} bytes)`);
      } catch (e) {
        setStatusErr("Import failed: " + String(e));
      }

      // Reset so the same file can be re-imported
      importFile.value = "";
    };

    reader.onerror = () => {
      setStatusErr("Failed to read file");
      importFile.value = "";
    };

    reader.readAsArrayBuffer(file);
  });

  // Ctrl+S compile
  window.addEventListener("keydown", (ev) => {
    if ((ev.ctrlKey || ev.metaKey) && ev.key.toLowerCase() === "s") {
      ev.preventDefault();
      compileSeq++;
      if (compileTimer) clearTimeout(compileTimer);
      compileAndReload("ctrl+s");
    }
  });
  // Shift+G+U+I Toggle HUD
  {
    const comboKeys = new Set();
    let hudCooldown = false;

    window.addEventListener("keydown", (ev) => {
      comboKeys.add(ev.key.toLowerCase());

      // Check for Shift + G + U + I held simultaneously
      if (
        ev.shiftKey &&
        comboKeys.has("g") &&
        comboKeys.has("u") &&
        comboKeys.has("i") &&
        !hudCooldown
      ) {
        hudCooldown = true;
        toggle_hud();

        // Prevent re-toggling for 500ms even if keys are still held
        setTimeout(() => { hudCooldown = false; }, 500);
      }
    });

    window.addEventListener("keyup", (ev) => {
      comboKeys.delete(ev.key.toLowerCase());
    });

    // Safety: clear all keys if window loses focus
    window.addEventListener("blur", () => {
      comboKeys.clear();
    });
  }

  // First compile immediately so textarea program replaces base.bin program
  setStatusOk("Ready. Compiling initial script...");
  compileAndReload("initial");
}

main().catch((e) => {
  console.error(e);
  const statusEl = document.getElementById("status");
  statusEl.style.color = "#ff6b6b";
  statusEl.textContent = "Fatal error: " + (e?.message ?? e);
});