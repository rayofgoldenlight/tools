use std::cell::RefCell;
use std::rc::Rc;
use wasm_bindgen::prelude::*;
use wasm_bindgen::JsCast;
use wasm_bindgen_futures::JsFuture;
use web_sys::{CanvasRenderingContext2d, HtmlCanvasElement};

use format::{Color, DrawMode, GameFile, SceneObject, Shape, ShapeParams, ShapeType, PathCmd};
mod input;
use input::InputState;

mod engine_context;

use format::vm::VM;
use crate::engine_context::RuntimeContext;

use js_sys::Uint8Array;

thread_local! {
    static GLOBAL_STATE: RefCell<Option<Rc<RefCell<GameState>>>> = RefCell::new(None);
}

// --- JS Interop for Audio ---
#[wasm_bindgen]
extern "C" {
    #[wasm_bindgen(js_namespace = window)]
    fn play_audio_js(id: u16);

    #[wasm_bindgen(js_namespace = window)]
    fn stop_audio_js(id: u16);

    #[wasm_bindgen(js_namespace = window)]
    fn loop_audio_js(id: u16);

    #[wasm_bindgen(js_namespace = window)]
    fn set_volume_js(id: u16, vol: f32);
}

#[wasm_bindgen]
pub fn get_audio_count() -> usize {
    GLOBAL_STATE.with(|gs| {
        gs.borrow().as_ref().map(|s| s.borrow().game_file.audio.len()).unwrap_or(0)
    })
}

#[wasm_bindgen]
pub fn get_audio_data(index: usize) -> js_sys::Uint8Array {
    GLOBAL_STATE.with(|gs| {
        let state_rc = gs.borrow().as_ref().unwrap().clone();
        let state = state_rc.borrow();
        let data = &state.game_file.audio[index].data;
        // Copies the bytes out of WASM memory into a standard JS array
        js_sys::Uint8Array::from(data.as_slice())
    })
}

#[wasm_bindgen]
pub fn clear_audio() {
    GLOBAL_STATE.with(|gs| {
        if let Some(state_rc) = gs.borrow().as_ref() {
            state_rc.borrow_mut().game_file.audio.clear();
        }
    });
}

#[wasm_bindgen]
pub fn get_audio_name(index: usize) -> String {
    GLOBAL_STATE.with(|gs| {
        gs.borrow().as_ref().map(|s| s.borrow().game_file.audio[index].name.clone()).unwrap_or_default()
    })
}

#[wasm_bindgen]
pub fn set_audio_name(index: usize, name: &str) {
    GLOBAL_STATE.with(|gs| {
        if let Some(state_rc) = gs.borrow().as_ref() {
            if let Some(aud) = state_rc.borrow_mut().game_file.audio.get_mut(index) {
                aud.name = name.to_string();
            }
        }
    });
}

#[wasm_bindgen]
pub fn push_audio(name: &str, format: u8, data: &[u8]) {
    GLOBAL_STATE.with(|gs| {
        if let Some(state_rc) = gs.borrow().as_ref() {
            let fmt = match format {
                1 => format::AudioFormat::Mp3,
                2 => format::AudioFormat::Opus,
                _ => format::AudioFormat::Synth,
            };
            state_rc.borrow_mut().game_file.audio.push(format::AudioAsset {
                name: name.to_string(),
                format: fmt,
                data: data.to_vec(),
            });
        }
    });
}

fn window() -> web_sys::Window {
    web_sys::window().unwrap()
}

fn request_animation_frame(f: &Closure<dyn FnMut(f64)>) {
    window()
        .request_animation_frame(f.as_ref().unchecked_ref())
        .unwrap();
}

// ── Rendering ───────────────────────────────────

fn render_shape(ctx: &CanvasRenderingContext2d, shape: &Shape, obj: &SceneObject) {
    ctx.save();

    // Move to object position
    ctx.translate(obj.x as f64, obj.y as f64).unwrap();
    ctx.rotate(obj.rotation as f64).unwrap();
    ctx.scale(obj.scale as f64, obj.scale as f64).unwrap();

    match &shape.params {
        ShapeParams::Circle { radius } => {
            let r = *radius as f64;

            ctx.begin_path();
            ctx.arc(0.0, 0.0, r, 0.0, std::f64::consts::TAU).unwrap();

            if matches!(shape.draw_mode, DrawMode::Fill | DrawMode::Both) {
                ctx.set_fill_style_str(&shape.fill_color.to_css());
                ctx.fill();
            }
            if matches!(shape.draw_mode, DrawMode::Stroke | DrawMode::Both) {
                ctx.set_stroke_style_str(&shape.stroke_color.to_css());
                ctx.set_line_width(shape.stroke_width as f64);
                ctx.stroke();
            }
        }
        ShapeParams::Rect { width, height } => {
            let w = *width as f64;
            let h = *height as f64;

            if matches!(shape.draw_mode, DrawMode::Fill | DrawMode::Both) {
                ctx.set_fill_style_str(&shape.fill_color.to_css());
                ctx.fill_rect(-w / 2.0, -h / 2.0, w, h);
            }
            if matches!(shape.draw_mode, DrawMode::Stroke | DrawMode::Both) {
                ctx.set_stroke_style_str(&shape.stroke_color.to_css());
                ctx.set_line_width(shape.stroke_width as f64);
                ctx.stroke_rect(-w / 2.0, -h / 2.0, w, h);
            }
        }
        ShapeParams::Path { commands } => {
            ctx.begin_path();
            for cmd in commands {
                match cmd {
                    PathCmd::MoveTo(x, y) => ctx.move_to(*x as f64, *y as f64),
                    PathCmd::LineTo(x, y) => ctx.line_to(*x as f64, *y as f64),
                    PathCmd::QuadTo(cx, cy, x, y) => {
                        ctx.quadratic_curve_to(*cx as f64, *cy as f64, *x as f64, *y as f64)
                    }
                    PathCmd::Fill(c) => {
                        ctx.set_fill_style_str(&c.to_css());
                        ctx.fill();
                        ctx.begin_path();
                    }
                    PathCmd::Stroke(c, w) => {
                        ctx.set_stroke_style_str(&c.to_css());
                        ctx.set_line_width(*w as f64);
                        ctx.stroke();
                        ctx.begin_path();
                    }
                    PathCmd::Close => ctx.close_path(),
                }
            }

            if matches!(shape.draw_mode, DrawMode::Fill | DrawMode::Both) {
                ctx.set_fill_style_str(&shape.fill_color.to_css());
                ctx.fill();
            }
            if matches!(shape.draw_mode, DrawMode::Stroke | DrawMode::Both) {
                ctx.set_stroke_style_str(&shape.stroke_color.to_css());
                ctx.set_line_width(shape.stroke_width as f64);
                ctx.stroke();
            }
        }
        ShapeParams::Text { text, size, align } => {
            let display_text = if text.contains("{}") {
                text.replace("{}", &obj.value.to_string())
            } else {
                text.clone()
            };

            let align_str = match align {
                1 => "center",
                2 => "right",
                _ => "left",
            };
            ctx.set_font(&format!("bold {}px sans-serif", size));
            ctx.set_text_align(align_str);
            ctx.set_text_baseline("middle");

            if matches!(shape.draw_mode, DrawMode::Fill | DrawMode::Both) {
                ctx.set_fill_style_str(&shape.fill_color.to_css());
                ctx.fill_text(&display_text, 0.0, 0.0).unwrap(); // <-- Use display_text
            }
            if matches!(shape.draw_mode, DrawMode::Stroke | DrawMode::Both) {
                ctx.set_stroke_style_str(&shape.stroke_color.to_css());
                ctx.set_line_width(shape.stroke_width as f64);
                ctx.stroke_text(&display_text, 0.0, 0.0).unwrap(); // <-- Use display_text
            }
        }
    }

    ctx.restore();
}

// ── Game State ──────────────────────────────────

struct GameState {
    ctx: CanvasRenderingContext2d,
    game_file: GameFile,
    base_objects: Vec<SceneObject>,
    input: InputState,
    vm: VM,
    time: f64,
    frame_count: u32,
    fps_time: f64,
    fps: f64,
    selected_object: i32,
    show_hud: bool,
}

impl GameState {
    fn update(&mut self, timestamp: f64) {
        let dt = if self.time == 0.0 {
            1.0 / 60.0
        } else {
            ((timestamp - self.time) / 1000.0) as f32
        };
        self.time = timestamp;

        // --- ANIMATION AUTO-ADVANCE ---
        for obj in &mut self.game_file.objects {
            if obj.anim_index != 0xFFFF {
                if let Some(anim) = self.game_file.animations.get(obj.anim_index as usize) {
                    if anim.frames.is_empty() { continue; }
                    if anim.fps > 0.0 {
                        obj.anim_time += dt;
                        let frame_duration = 1.0 / anim.fps;
                        
                        // How many frames should we advance?
                        if obj.anim_time >= frame_duration {
                            let frames_to_advance = (obj.anim_time / frame_duration).floor() as usize;
                            obj.anim_time %= frame_duration;
                            
                            // Find current frame index, advance, and wrap around
                            let current_idx = anim.frames.iter().position(|&f| f == obj.shape_index).unwrap_or(0);
                            let next_idx = (current_idx + frames_to_advance) % anim.frames.len();
                            obj.shape_index = anim.frames[next_idx]; // Swap the visible shape automatically
                        }
                    }
                }
            }
        }

        self.frame_count += 1;
        let elapsed = timestamp - self.fps_time;
        if elapsed >= 1000.0 {
            self.fps = self.frame_count as f64 / (elapsed / 1000.0);
            self.frame_count = 0;
            self.fps_time = timestamp;
        }

        // Run bytecode if present
        if !self.game_file.bytecode.is_empty() || !self.game_file.behaviors.is_empty() {
            // 1. Run Global update(dt)
            if !self.game_file.bytecode.is_empty() {
                let bytecode_slice = unsafe { std::slice::from_raw_parts(self.game_file.bytecode.as_ptr(), self.game_file.bytecode.len()) };
                let mut ctx = RuntimeContext { input: &self.input, objects: &mut self.game_file.objects, dt };
                self.vm.current_object = None;
                if let Err(e) = self.vm.run_frame(bytecode_slice, &mut ctx) {
                    web_sys::console::error_1(&format!("Global VM error: {:?}", e).into());
                }
            }

            // 2. Run Object Behaviors
            let obj_count = self.game_file.objects.len();
            for i in 0..obj_count {
                let shape_idx = if i < self.base_objects.len() {
                    self.base_objects[i].shape_index
                } else {
                    self.game_file.objects[i].shape_index
                };
                
                if let Some((_, code)) = self.game_file.behaviors.iter().find(|(s, _)| *s == shape_idx) {
                    let code_slice = unsafe { std::slice::from_raw_parts(code.as_ptr(), code.len()) };
                    let mut ctx = RuntimeContext { input: &self.input, objects: &mut self.game_file.objects, dt };
                    self.vm.current_object = Some(i as u16);
                    if let Err(e) = self.vm.run_frame(code_slice, &mut ctx) {
                        web_sys::console::error_1(&format!("Behavior VM error (obj {}): {:?}", i, e).into());
                    }
                }
            }
        }
    }

    fn draw(&self) {
        let w = self.game_file.header.screen_width as f64;
        let h = self.game_file.header.screen_height as f64;

        // Clear
        self.ctx.set_fill_style_str("#1a1a2e");
        self.ctx.fill_rect(0.0, 0.0, w, h);

        // Draw all objects from the game file
        for obj in &self.game_file.objects {
            if !obj.visible { continue; } // <--- skip hidden objects
            let shape = &self.game_file.shapes[obj.shape_index as usize];
            render_shape(&self.ctx, shape, obj);
        }

        // ── HUD / Debug overlay ─────────────

        if self.show_hud {
            // FPS counter
            self.ctx.set_fill_style_str("#ffffff");
            self.ctx.set_font("16px monospace");
            let fps_text = format!("FPS: {:.0}", self.fps);
            self.ctx.fill_text(&fps_text, 10.0, 24.0).unwrap();

            // Input debug display
            let held = self.input.held_keys();
            if held.is_empty() {
                self.ctx.set_fill_style_str("#666666");
                self.ctx
                    .fill_text("Keys: (press arrow keys, WASD, space...)", 10.0, 48.0)
                    .unwrap();
            } else {
                let key_names: Vec<String> = held
                    .iter()
                    .map(|&code| format!("{}({})", InputState::key_name(code), code))
                    .collect();
                let key_text = format!("Keys: {}", key_names.join(", "));
                self.ctx.set_fill_style_str("#00ff88");
                self.ctx.fill_text(&key_text, 10.0, 48.0).unwrap();
            }

            // Visual key indicator boxes
            self.draw_key_indicators();

            // File stats
            self.ctx.set_fill_style_str("#ffffff");
            self.ctx.set_font("12px monospace");
            let stats = format!(
                "BIN: {} shapes, {} objects, {} bytes bytecode",
                self.game_file.shapes.len(),
                self.game_file.objects.len(),
                self.game_file.bytecode.len(),
            );
            self.ctx
                .fill_text(&stats, 10.0, h - 10.0)
                .unwrap();
        }

        // ── Selection highlight ─────────────
        if self.selected_object >= 0 {
            let idx = self.selected_object as usize;
            if idx < self.game_file.objects.len() {
                let obj = &self.game_file.objects[idx];
                let shape = &self.game_file.shapes[obj.shape_index as usize];

                self.ctx.save();
                self.ctx.translate(obj.x as f64, obj.y as f64).unwrap();
                self.ctx.rotate(obj.rotation as f64).unwrap();
                self.ctx.scale(obj.scale as f64, obj.scale as f64).unwrap();

                self.ctx.set_stroke_style_str("#00ffff");
                self.ctx.set_line_width(2.0);
                self.ctx.set_line_dash(&js_sys::Array::of2(
                    &JsValue::from_f64(4.0),
                    &JsValue::from_f64(4.0),
                )).unwrap();

                match &shape.params {
                    ShapeParams::Circle { radius } => {
                        let r = *radius as f64 + 4.0;
                        self.ctx.begin_path();
                        self.ctx.arc(0.0, 0.0, r, 0.0, std::f64::consts::TAU).unwrap();
                        self.ctx.stroke();
                    }
                    ShapeParams::Rect { width, height } => {
                        let w = *width as f64 + 8.0;
                        let h = *height as f64 + 8.0;
                        self.ctx.stroke_rect(-w / 2.0, -h / 2.0, w, h);
                    }
                    ShapeParams::Path { commands } => {
                        self.ctx.begin_path();
                        for cmd in commands {
                            match cmd {
                                PathCmd::MoveTo(x, y) => self.ctx.move_to(*x as f64, *y as f64),
                                PathCmd::LineTo(x, y) => self.ctx.line_to(*x as f64, *y as f64),
                                PathCmd::QuadTo(cx, cy, x, y) => {
                                    self.ctx.quadratic_curve_to(*cx as f64, *cy as f64, *x as f64, *y as f64)
                                }
                                PathCmd::Close => self.ctx.close_path(),
                                PathCmd::Fill(c) => {
                                    self.ctx.set_fill_style_str(&c.to_css());
                                    self.ctx.fill();
                                    self.ctx.begin_path();
                                }
                                PathCmd::Stroke(c, w) => {
                                    self.ctx.set_stroke_style_str(&c.to_css());
                                    self.ctx.set_line_width(*w as f64);
                                    self.ctx.stroke();
                                    self.ctx.begin_path();
                                }
                            }
                        }
                        self.ctx.stroke();
                    }
                    ShapeParams::Text { text, size, align } => {
                        // Replace "{}" with the object's dynamic value
                        let display_text = if text.contains("{}") {
                            text.replace("{}", &obj.value.to_string())
                        } else {
                            text.clone()
                        };

                        let align_str = match align { 1 => "center", 2 => "right", _ => "left" };
                        self.ctx.set_font(&format!("bold {}px sans-serif", size));
                        self.ctx.set_text_align(align_str);
                        self.ctx.set_text_baseline("middle");

                        if matches!(shape.draw_mode, DrawMode::Fill | DrawMode::Both) {
                            self.ctx.set_fill_style_str(&shape.fill_color.to_css());
                            self.ctx.fill_text(&display_text, 0.0, 0.0).unwrap();
                        }
                        if matches!(shape.draw_mode, DrawMode::Stroke | DrawMode::Both) {
                            self.ctx.set_stroke_style_str(&shape.stroke_color.to_css());
                            self.ctx.set_line_width(shape.stroke_width as f64);
                            self.ctx.stroke_text(&display_text, 0.0, 0.0).unwrap();
                        }
                    }
                }

                self.ctx.set_line_dash(&js_sys::Array::new()).unwrap();
                self.ctx.restore();
            }
        }
    }
    fn draw_key_indicators(&self) {
        // Draw a small arrow-key + WASD visual in the bottom-right
        let base_x = self.game_file.header.screen_width as f64 - 180.0;
        let base_y = self.game_file.header.screen_height as f64 - 100.0;
        let size = 30.0;
        let gap = 4.0;

        // Arrow keys layout:
        //        [UP]
        // [LEFT][DOWN][RIGHT]
        let arrow_keys = [
            (input::keys::UP, 1.0, 0.0, "^"),
            (input::keys::LEFT, 0.0, 1.0, "<"),
            (input::keys::DOWN, 1.0, 1.0, "v"),
            (input::keys::RIGHT, 2.0, 1.0, ">"),
        ];

        self.ctx.set_font("14px monospace");

        for (key_code, col, row, label) in &arrow_keys {
            let x = base_x + (*col) * (size + gap);
            let y = base_y + (*row) * (size + gap);
            let held = self.input.is_key_down(*key_code);

            // Box
            if held {
                self.ctx.set_fill_style_str("#00ff88");
                self.ctx.fill_rect(x, y, size, size);
                self.ctx.set_fill_style_str("#000000");
            } else {
                self.ctx.set_stroke_style_str("#444444");
                self.ctx.set_line_width(1.0);
                self.ctx.stroke_rect(x, y, size, size);
                self.ctx.set_fill_style_str("#444444");
            }

            // Label
            self.ctx
                .fill_text(label, x + size / 2.0 - 4.0, y + size / 2.0 + 5.0)
                .unwrap();
        }

        // WASD layout (offset to the left)
        let wasd_x = base_x - 130.0;

        let wasd_keys = [
            (input::keys::W, 1.0, 0.0, "W"),
            (input::keys::A, 0.0, 1.0, "A"),
            (input::keys::S, 1.0, 1.0, "S"),
            (input::keys::D, 2.0, 1.0, "D"),
        ];

        for (key_code, col, row, label) in &wasd_keys {
            let x = wasd_x + (*col) * (size + gap);
            let y = base_y + (*row) * (size + gap);
            let held = self.input.is_key_down(*key_code);

            if held {
                self.ctx.set_fill_style_str("#00ff88");
                self.ctx.fill_rect(x, y, size, size);
                self.ctx.set_fill_style_str("#000000");
            } else {
                self.ctx.set_stroke_style_str("#444444");
                self.ctx.set_line_width(1.0);
                self.ctx.stroke_rect(x, y, size, size);
                self.ctx.set_fill_style_str("#444444");
            }

            self.ctx
                .fill_text(label, x + size / 2.0 - 4.0, y + size / 2.0 + 5.0)
                .unwrap();
        }

        // Space bar
        let space_y = base_y + 2.0 * (size + gap) + 8.0;
        let space_w = 3.0 * size + 2.0 * gap;
        let held = self.input.is_key_down(input::keys::SPACE);

        if held {
            self.ctx.set_fill_style_str("#00ff88");
            self.ctx.fill_rect(wasd_x, space_y, space_w, size * 0.7);
            self.ctx.set_fill_style_str("#000000");
        } else {
            self.ctx.set_stroke_style_str("#444444");
            self.ctx.set_line_width(1.0);
            self.ctx
                .stroke_rect(wasd_x, space_y, space_w, size * 0.7);
            self.ctx.set_fill_style_str("#444444");
        }

        self.ctx
            .fill_text("SPACE", wasd_x + space_w / 2.0 - 18.0, space_y + 15.0)
            .unwrap();
    }
}

// ── Fetch ───────────────────────────────────────

async fn fetch_bytes(url: &str) -> Result<Vec<u8>, JsValue> {
    let window = web_sys::window().unwrap();

    let resp: web_sys::Response = JsFuture::from(window.fetch_with_str(url))
        .await?
        .dyn_into()?;

    let array_buffer = JsFuture::from(resp.array_buffer()?).await?;
    let uint8_array = js_sys::Uint8Array::new(&array_buffer);

    let mut bytes = vec![0u8; uint8_array.length() as usize];
    uint8_array.copy_to(&mut bytes);

    Ok(bytes)
}

// ── Entry Point ─────────────────────────────────

#[wasm_bindgen(start)]
pub fn start() -> Result<(), JsValue> {
    // Only auto-start if game-canvas exists (i.e. index.html, not editor.html)
    let document = window().document().unwrap();
    if document.get_element_by_id("game-canvas").is_some() {
        start_with("game-canvas", "test.bin")?;
    }
    Ok(())
}

#[wasm_bindgen]
pub fn start_with(canvas_id: &str, bin_url: &str) -> Result<(), JsValue> {
    let canvas_id = canvas_id.to_string();
    let bin_url = bin_url.to_string();

    wasm_bindgen_futures::spawn_local(async move {
        if let Err(e) = run_with(&canvas_id, &bin_url).await {
            web_sys::console::error_1(&e);
        }
    });

    Ok(())
}

#[wasm_bindgen]
pub async fn start_with_async(canvas_id: String, bin_url: String) -> Result<(), JsValue> {
    // This awaits loading/parsing the bin and setting GLOBAL_STATE.
    run_with(&canvas_id, &bin_url).await
}

#[wasm_bindgen]
pub fn load_binary(data: &[u8]) -> Result<String, JsValue> {
    let game_file = format::deserialize::decode(data)
        .map_err(|e| JsValue::from_str(&format!("Failed to decode binary: {:?}", e)))?;

    GLOBAL_STATE.with(|gs| {
        let Some(state_rc) = gs.borrow().as_ref().cloned() else {
            return Err(JsValue::from_str("Runtime not started."));
        };

        let mut state = state_rc.borrow_mut();

        web_sys::console::log_1(
            &format!(
                "Loading binary: {}x{}, {} shapes, {} objects, {} bytes bytecode, {} vars",
                game_file.header.screen_width,
                game_file.header.screen_height,
                game_file.shapes.len(),
                game_file.objects.len(),
                game_file.bytecode.len(),
                game_file.variable_count,
            )
            .into(),
        );

        // Resize canvas to match imported game
        let document = window().document().unwrap();
        if let Some(canvas_el) = document.get_element_by_id("preview-canvas") {
            if let Ok(canvas) = canvas_el.dyn_into::<HtmlCanvasElement>() {
                canvas.set_width(game_file.header.screen_width as u32);
                canvas.set_height(game_file.header.screen_height as u32);
            }
        }

        // Replace state
        state.base_objects = game_file.objects.clone();
        state.vm = VM::new(game_file.variable_count);
        state.game_file = game_file;
        state.time = 0.0;
        state.frame_count = 0;
        state.fps_time = 0.0;
        state.fps = 0.0;

        let script = state.game_file.script_source.clone();

        web_sys::console::log_1(
            &format!("Binary loaded OK ({} bytes script source)", script.len()).into(),
        );

        Ok(script)
    })
}

#[wasm_bindgen]
pub fn export_binary(script_source: &str) -> Result<Uint8Array, JsValue> {
    GLOBAL_STATE.with(|gs| {
        let Some(state_rc) = gs.borrow().as_ref().cloned() else {
            return Err(JsValue::from_str("Runtime not started."));
        };

        let state = state_rc.borrow();

        let mut game_file = state.game_file.clone();
        game_file.header.section_count = 6;
        game_file.header.version = format::VERSION;
        game_file.script_source = script_source.to_string();

        let bytes = format::serialize::encode(&game_file);

        web_sys::console::log_1(
            &format!(
                "Exported binary: {} bytes ({} bytes script)",
                bytes.len(),
                script_source.len()
            )
            .into(),
        );

        Ok(Uint8Array::from(bytes.as_slice()))
    })
}

#[wasm_bindgen]
pub fn toggle_hud() {
    GLOBAL_STATE.with(|gs| {
        if let Some(state_rc) = gs.borrow().as_ref() {
            let mut state = state_rc.borrow_mut();
            state.show_hud = !state.show_hud;
            web_sys::console::log_1(
                &format!("HUD: {}", if state.show_hud { "ON" } else { "OFF" }).into()
            );
        }
    });
}

#[wasm_bindgen]
pub fn reset_state() -> Result<(), JsValue> {
    GLOBAL_STATE.with(|gs| {
        let Some(state_rc) = gs.borrow().as_ref().cloned() else {
            return Err(JsValue::from_str("Runtime not started."));
        };

        let mut state = state_rc.borrow_mut();

        // Restore objects to baseline loaded from base.bin
        state.game_file.objects = state.base_objects.clone();

        // Reset VM (clears persistent variables)
        let var_count = state.game_file.variable_count;
        state.vm = VM::new(var_count);

        // Reset timing/fps counters (optional)
        state.time = 0.0;
        state.frame_count = 0;
        state.fps_time = 0.0;
        state.fps = 0.0;

        web_sys::console::log_1(&"Reset OK".into());
        Ok(())
    })
}

#[wasm_bindgen]
pub fn set_bytecode(bytecode: Vec<u8>, variable_count: u16, behaviors_val: JsValue) -> Result<(), JsValue> {
    GLOBAL_STATE.with(|gs| {
        let Some(state_rc) = gs.borrow().as_ref().cloned() else { return Err(JsValue::from_str("Runtime not started.")); };
        let mut state = state_rc.borrow_mut();

        state.game_file.bytecode = bytecode;
        state.game_file.variable_count = variable_count;
        
        // --- Unpack behaviors ---
        let mut behaviors = Vec::new();
        if let Ok(arr) = behaviors_val.dyn_into::<js_sys::Array>() {
            for i in 0..arr.length() {
                let obj = js_sys::Object::from(arr.get(i));
                let shape_idx = js_sys::Reflect::get(&obj, &JsValue::from_str("shape_index")).unwrap().as_f64().unwrap() as u16;
                let code_val = js_sys::Reflect::get(&obj, &JsValue::from_str("bytecode")).unwrap();
                let code_arr = js_sys::Uint8Array::new(&code_val);
                let mut code = vec![0; code_arr.length() as usize];
                code_arr.copy_to(&mut code);
                behaviors.push((shape_idx, code));
            }
        }
        state.game_file.behaviors = behaviors;
        
        state.vm = VM::new(variable_count);
        Ok(())
    })
}

fn parse_shapes_json(json: &str) -> Result<Vec<Shape>, String> {
    // Minimal JSON array parser for shapes
    // Expected format: [{"type":"circle","mode":"fill","fillColor":{"r":0,"g":255,"b":136,"a":255},
    //   "strokeColor":{"r":255,"g":255,"b":255,"a":255},"strokeWidth":2,"radius":20}, ...]
    let val: js_sys::Array = js_sys::JSON::parse(json)
        .map_err(|_| "Invalid JSON".to_string())?
        .dyn_into()
        .map_err(|_| "Expected JSON array".to_string())?;

    let mut shapes = Vec::new();
    for i in 0..val.length() {
        let obj = js_sys::Object::from(val.get(i));
        shapes.push(js_shape_to_shape(&obj)?);
    }
    Ok(shapes)
}

fn parse_objects_json(json: &str) -> Result<Vec<SceneObject>, String> {
    let val: js_sys::Array = js_sys::JSON::parse(json)
        .map_err(|_| "Invalid JSON".to_string())?
        .dyn_into()
        .map_err(|_| "Expected JSON array".to_string())?;

    let mut objects = Vec::new();
    for i in 0..val.length() {
        let obj = js_sys::Object::from(val.get(i));
        objects.push(js_obj_to_scene_object(&obj)?);
    }
    Ok(objects)
}

fn js_get_f64(obj: &js_sys::Object, key: &str) -> Result<f64, String> {
    let val = js_sys::Reflect::get(obj, &JsValue::from_str(key))
        .map_err(|_| format!("Missing field: {}", key))?;
    val.as_f64().ok_or(format!("Field '{}' is not a number", key))
}

fn js_get_str(obj: &js_sys::Object, key: &str) -> Result<String, String> {
    let val = js_sys::Reflect::get(obj, &JsValue::from_str(key))
        .map_err(|_| format!("Missing field: {}", key))?;
    val.as_string().ok_or(format!("Field '{}' is not a string", key))
}

fn js_get_obj(obj: &js_sys::Object, key: &str) -> Result<js_sys::Object, String> {
    let val = js_sys::Reflect::get(obj, &JsValue::from_str(key))
        .map_err(|_| format!("Missing field: {}", key))?;
    Ok(js_sys::Object::from(val))
}

fn js_color(obj: &js_sys::Object) -> Result<Color, String> {
    Ok(Color {
        r: js_get_f64(obj, "r")? as u8,
        g: js_get_f64(obj, "g")? as u8,
        b: js_get_f64(obj, "b")? as u8,
        a: js_get_f64(obj, "a")? as u8,
    })
}

fn js_shape_to_shape(obj: &js_sys::Object) -> Result<Shape, String> {
    let type_str = js_get_str(obj, "type")?;
    let mode_str = js_get_str(obj, "mode")?;

    let shape_type = match type_str.as_str() {
        "circle" => ShapeType::Circle,
        "rect" => ShapeType::Rect,
        "path" => ShapeType::Path,
        "text" => ShapeType::Text,
        _ => return Err(format!("Unknown shape type: {}", type_str)),
    };

    let draw_mode = match mode_str.as_str() {
        "fill" => DrawMode::Fill,
        "stroke" => DrawMode::Stroke,
        "both" => DrawMode::Both,
        _ => return Err(format!("Unknown draw mode: {}", mode_str)),
    };

    let fill_color = js_color(&js_get_obj(obj, "fillColor")?)?;
    let stroke_color = js_color(&js_get_obj(obj, "strokeColor")?)?;
    let stroke_width = js_get_f64(obj, "strokeWidth")? as f32;

    let params = match shape_type {
        ShapeType::Circle => {
            ShapeParams::Circle {
                radius: js_get_f64(obj, "radius")? as f32,
            }
        }
        ShapeType::Rect => {
            ShapeParams::Rect {
                width: js_get_f64(obj, "width")? as f32,
                height: js_get_f64(obj, "height")? as f32,
            }
        }
        ShapeType::Path => {
            let cmds_val = js_sys::Reflect::get(obj, &JsValue::from_str("commands"))
                .map_err(|_| "Missing commands".to_string())?;
            let cmds_arr = cmds_val.dyn_into::<js_sys::Array>()
                .map_err(|_| "commands must be array".to_string())?;
            let mut commands = Vec::new();
            for i in 0..cmds_arr.length() {
                let c_obj = js_sys::Object::from(cmds_arr.get(i));
                let c_type = js_get_str(&c_obj, "type")?;
                let cmd = match c_type.as_str() {
                    "move" => PathCmd::MoveTo(js_get_f64(&c_obj, "x")? as f32, js_get_f64(&c_obj, "y")? as f32),
                    "line" => PathCmd::LineTo(js_get_f64(&c_obj, "x")? as f32, js_get_f64(&c_obj, "y")? as f32),
                    "quad" => PathCmd::QuadTo(
                        js_get_f64(&c_obj, "cx")? as f32, js_get_f64(&c_obj, "cy")? as f32,
                        js_get_f64(&c_obj, "x")? as f32, js_get_f64(&c_obj, "y")? as f32
                    ),
                    "close" => PathCmd::Close,
                    "fill" => PathCmd::Fill(Color { r: js_get_f64(&c_obj, "r")? as u8, g: js_get_f64(&c_obj, "g")? as u8, b: js_get_f64(&c_obj, "b")? as u8, a: js_get_f64(&c_obj, "a")? as u8 }),
                    "stroke" => PathCmd::Stroke(Color { r: js_get_f64(&c_obj, "r")? as u8, g: js_get_f64(&c_obj, "g")? as u8, b: js_get_f64(&c_obj, "b")? as u8, a: js_get_f64(&c_obj, "a")? as u8 }, js_get_f64(&c_obj, "w")? as f32),
                    _ => return Err(format!("Unknown path cmd: {}", c_type)),
                };
                commands.push(cmd);
            }
            ShapeParams::Path { commands }
        }
        ShapeType::Text => {
            ShapeParams::Text {
                text: js_get_str(obj, "text")?,
                size: js_get_f64(obj, "size")? as f32,
                align: js_get_f64(obj, "align")? as u8,
            }
        }
    };

    Ok(Shape {
        shape_type,
        draw_mode,
        fill_color,
        stroke_color,
        stroke_width,
        params,
    })
}

fn js_obj_to_scene_object(obj: &js_sys::Object) -> Result<SceneObject, String> {
    Ok(SceneObject {
        shape_index: js_get_f64(obj, "shapeIndex")? as u16,
        anim_index: js_get_f64(obj, "animIndex").unwrap_or(65535.0) as u16, 
        anim_time: 0.0,                                                 
        x: js_get_f64(obj, "x")? as f32,
        y: js_get_f64(obj, "y")? as f32,
        rotation: js_get_f64(obj, "rotation")? as f32,
        scale: js_get_f64(obj, "scale")? as f32,
        visible: js_sys::Reflect::get(obj, &JsValue::from_str("visible")).unwrap_or(JsValue::from_bool(true)).as_bool().unwrap_or(true),
        value: js_sys::Reflect::get(obj, &JsValue::from_str("value")).unwrap_or(JsValue::from_f64(0.0)).as_f64().unwrap_or(0.0) as f32,
        custom: [
            js_sys::Reflect::get(obj, &JsValue::from_str("val1")).unwrap_or(JsValue::from_f64(0.0)).as_f64().unwrap_or(0.0) as f32,
            js_sys::Reflect::get(obj, &JsValue::from_str("val2")).unwrap_or(JsValue::from_f64(0.0)).as_f64().unwrap_or(0.0) as f32,
            js_sys::Reflect::get(obj, &JsValue::from_str("val3")).unwrap_or(JsValue::from_f64(0.0)).as_f64().unwrap_or(0.0) as f32,
            js_sys::Reflect::get(obj, &JsValue::from_str("val4")).unwrap_or(JsValue::from_f64(0.0)).as_f64().unwrap_or(0.0) as f32,
        ],
    })
}

fn parse_animations_json(json: &str) -> Result<Vec<format::Animation>, String> {
    let val: js_sys::Array = js_sys::JSON::parse(json)
        .map_err(|_| "Invalid JSON".to_string())?
        .dyn_into()
        .map_err(|_| "Expected JSON array".to_string())?;

    let mut anims = Vec::new();
    for i in 0..val.length() {
        let obj = js_sys::Object::from(val.get(i));
        let fps = js_get_f64(&obj, "fps")? as f32;
        let frames_val = js_sys::Reflect::get(&obj, &JsValue::from_str("frames")).unwrap();
        let frames_arr = frames_val.dyn_into::<js_sys::Array>().map_err(|_| "Frames must be array".to_string())?;
        
        let mut frames = Vec::new();
        for j in 0..frames_arr.length() {
            frames.push(frames_arr.get(j).as_f64().unwrap() as u16);
        }
        anims.push(format::Animation { frames, fps });
    }
    Ok(anims)
}

#[wasm_bindgen]
pub fn get_scene_json() -> Result<String, JsValue> {
    GLOBAL_STATE.with(|gs| {
        let Some(state_rc) = gs.borrow().as_ref().cloned() else {
            return Err(JsValue::from_str("Runtime not started."));
        };

        let state = state_rc.borrow();
        let mut result = String::from("{\"shapes\":[");

        for (i, shape) in state.game_file.shapes.iter().enumerate() {
            if i > 0 { result.push(','); }
            result.push('{');
            match shape.shape_type {
                ShapeType::Circle => result.push_str("\"type\":\"circle\""),
                ShapeType::Rect => result.push_str("\"type\":\"rect\""),
                ShapeType::Path => result.push_str("\"type\":\"path\""),
                ShapeType::Text => result.push_str("\"type\":\"text\""),
            }
            match shape.draw_mode {
                DrawMode::Fill => result.push_str(",\"mode\":\"fill\""),
                DrawMode::Stroke => result.push_str(",\"mode\":\"stroke\""),
                DrawMode::Both => result.push_str(",\"mode\":\"both\""),
            }
            result.push_str(&format!(
                ",\"fillColor\":{{\"r\":{},\"g\":{},\"b\":{},\"a\":{}}}",
                shape.fill_color.r, shape.fill_color.g, shape.fill_color.b, shape.fill_color.a
            ));
            result.push_str(&format!(
                ",\"strokeColor\":{{\"r\":{},\"g\":{},\"b\":{},\"a\":{}}}",
                shape.stroke_color.r, shape.stroke_color.g, shape.stroke_color.b, shape.stroke_color.a
            ));
            result.push_str(&format!(",\"strokeWidth\":{}", shape.stroke_width));
            match &shape.params {
                ShapeParams::Circle { radius } => {
                    result.push_str(&format!(",\"radius\":{}", radius));
                }
                ShapeParams::Rect { width, height } => {
                    result.push_str(&format!(",\"width\":{},\"height\":{}", width, height));
                }
                ShapeParams::Path { commands } => {
                    result.push_str(",\"commands\":[");
                    for (j, cmd) in commands.iter().enumerate() {
                        if j > 0 { result.push(','); }
                        match cmd {
                            PathCmd::MoveTo(x, y) => result.push_str(&format!("{{\"type\":\"move\",\"x\":{},\"y\":{}}}", x, y)),
                            PathCmd::LineTo(x, y) => result.push_str(&format!("{{\"type\":\"line\",\"x\":{},\"y\":{}}}", x, y)),
                            PathCmd::QuadTo(cx, cy, x, y) => result.push_str(&format!("{{\"type\":\"quad\",\"cx\":{},\"cy\":{},\"x\":{},\"y\":{}}}", cx, cy, x, y)),
                            PathCmd::Close => result.push_str("{\"type\":\"close\"}"),
                            PathCmd::Fill(c) => result.push_str(&format!("{{\"type\":\"fill\",\"r\":{},\"g\":{},\"b\":{},\"a\":{}}}", c.r, c.g, c.b, c.a)),
                            PathCmd::Stroke(c, w) => result.push_str(&format!("{{\"type\":\"stroke\",\"r\":{},\"g\":{},\"b\":{},\"a\":{},\"w\":{}}}", c.r, c.g, c.b, c.a, w)),
                        }
                    }
                    result.push(']');
                }
                ShapeParams::Text { text, size, align } => {
                    // Quick escape for quotes in JSON
                    let safe_text = text.replace("\"", "\\\"");
                    result.push_str(&format!(",\"text\":\"{}\",\"size\":{},\"align\":{}", safe_text, size, align));
                }
            }
            result.push('}');
        }

        result.push_str("],\"animations\":[");
        for (i, anim) in state.game_file.animations.iter().enumerate() {
            if i > 0 { result.push(','); }
            let frames_str: Vec<String> = anim.frames.iter().map(|f| f.to_string()).collect();
            result.push_str(&format!("{{\"fps\":{},\"frames\":[{}]}}", anim.fps, frames_str.join(",")));
        }

        result.push_str("],\"objects\":[");

        for (i, obj) in state.game_file.objects.iter().enumerate() {
            if i > 0 { result.push(','); }
            result.push_str(&format!(
                "{{\"shapeIndex\":{},\"animIndex\":{},\"x\":{},\"y\":{},\"rotation\":{},\"scale\":{},\"visible\":{},\"value\":{},\"val1\":{},\"val2\":{},\"val3\":{},\"val4\":{}}}",
                obj.shape_index, obj.anim_index, obj.x, obj.y, obj.rotation, obj.scale, obj.visible, obj.value, obj.custom[0], obj.custom[1], obj.custom[2], obj.custom[3]
            ));
        }

        result.push_str("]}");
        Ok(result)
    })
}

#[wasm_bindgen]
pub fn set_selected_object(id: i32) {
    GLOBAL_STATE.with(|gs| {
        if let Some(state_rc) = gs.borrow().as_ref().cloned() {
            state_rc.borrow_mut().selected_object = id;
        }
    });
}

#[wasm_bindgen]
pub fn set_scene(shapes_json: &str, anims_json: &str, objects_json: &str) -> Result<(), JsValue> {
    let shapes = parse_shapes_json(shapes_json)
        .map_err(|e| JsValue::from_str(&format!("Bad shapes JSON: {}", e)))?;
    let animations = parse_animations_json(anims_json).map_err(|e| JsValue::from_str(&e))?;
    let objects = parse_objects_json(objects_json)
        .map_err(|e| JsValue::from_str(&format!("Bad objects JSON: {}", e)))?;

    GLOBAL_STATE.with(|gs| {
        let Some(state_rc) = gs.borrow().as_ref().cloned() else {
            return Err(JsValue::from_str("Runtime not started."));
        };

        let mut state = state_rc.borrow_mut();

        state.game_file.shapes = shapes;
        state.game_file.animations = animations;
        state.game_file.objects = objects.clone();
        state.base_objects = objects;

        web_sys::console::log_1(
            &format!(
                "Scene updated: {} shapes, {} objects",
                state.game_file.shapes.len(),
                state.game_file.objects.len(),
            )
            .into(),
        );

        Ok(())
    })
}

async fn run_with(canvas_id: &str, bin_url: &str) -> Result<(), JsValue> {
    let document = window().document().unwrap();

    let canvas = document
        .get_element_by_id(canvas_id)
        .ok_or_else(|| JsValue::from_str(&format!("Canvas element not found: #{}", canvas_id)))?
        .dyn_into::<HtmlCanvasElement>()?;

    // Fetch and parse the binary
    let bytes = fetch_bytes(bin_url).await?;
    let game_file = format::deserialize::decode(&bytes)
        .map_err(|e| JsValue::from_str(&format!("{:?}", e)))?;

    web_sys::console::log_1(
        &format!(
            "Loaded game: {}x{}, {} shapes, {} objects, {} bytes bytecode, {} vars",
            game_file.header.screen_width,
            game_file.header.screen_height,
            game_file.shapes.len(),
            game_file.objects.len(),
            game_file.bytecode.len(),
            game_file.variable_count,
        )
        .into(),
    );

    // Resize canvas to match game file
    canvas.set_width(game_file.header.screen_width as u32);
    canvas.set_height(game_file.header.screen_height as u32);

    let ctx = canvas
        .get_context("2d")?
        .unwrap()
        .dyn_into::<CanvasRenderingContext2d>()?;

    let input = InputState::new(&canvas)?; // <--- Pass canvas

    let vm = VM::new(game_file.variable_count);

    let base_objects = game_file.objects.clone();

    let state = Rc::new(RefCell::new(GameState {
        ctx,
        game_file,
        base_objects,
        input,
        vm,
        time: 0.0,
        frame_count: 0,
        fps_time: 0.0,
        fps: 0.0,
        selected_object: -1,
        show_hud: true,
    }));

    GLOBAL_STATE.with(|gs| {
        *gs.borrow_mut() = Some(state.clone());
    });

    // Game loop
    let callback = Rc::new(RefCell::new(None::<Closure<dyn FnMut(f64)>>));
    let callback_clone = callback.clone();
    let state_clone = state.clone();

    *callback.borrow_mut() = Some(Closure::wrap(Box::new(move |timestamp: f64| {
        let mut game = state_clone.borrow_mut();
        game.update(timestamp);
        game.draw();
        request_animation_frame(callback_clone.borrow().as_ref().unwrap());
    }) as Box<dyn FnMut(f64)>));

    request_animation_frame(callback.borrow().as_ref().unwrap());

    Ok(())
}