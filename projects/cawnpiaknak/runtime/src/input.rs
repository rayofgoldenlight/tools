use std::cell::{Cell, RefCell};
use std::rc::Rc;
use wasm_bindgen::prelude::*;
use wasm_bindgen::JsCast;

/// Common key codes for quick reference.
/// These match KeyboardEvent.keyCode values.
pub mod keys {
    pub const LEFT: u16 = 37;
    pub const UP: u16 = 38;
    pub const RIGHT: u16 = 39;
    pub const DOWN: u16 = 40;
    pub const SPACE: u16 = 32;
    pub const W: u16 = 87;
    pub const A: u16 = 65;
    pub const S: u16 = 83;
    pub const D: u16 = 68;
    pub const ESCAPE: u16 = 27;
    pub const ENTER: u16 = 13;
}

/// Game keys that should not trigger browser default actions (scrolling, etc.)
const CAPTURED_KEYS: &[u16] = &[
    keys::LEFT,
    keys::UP,
    keys::RIGHT,
    keys::DOWN,
    keys::SPACE,
];

/// Tracks which keys are currently held down.
/// Shared via Rc<RefCell<>> because event closures need access.
#[derive(Clone)]
pub struct InputState {
    held: Rc<RefCell<[bool; 256]>>,
    mouse_x: Rc<Cell<f32>>,    
    mouse_y: Rc<Cell<f32>>,     
    mouse_down: Rc<Cell<bool>>,
}

impl InputState {
    /// Create a new InputState and register keydown/keyup listeners on the window.
     pub fn new(canvas: &web_sys::HtmlCanvasElement) -> Result<Self, JsValue> {
        let held = Rc::new(RefCell::new([false; 256]));
        let mouse_x = Rc::new(Cell::new(0.0));
        let mouse_y = Rc::new(Cell::new(0.0));
        let mouse_down = Rc::new(Cell::new(false));
        let window = web_sys::window().unwrap();

        // --- Mouse Move ---
        {
            let mx = mouse_x.clone();
            let my = mouse_y.clone();
            let canvas_clone = canvas.clone();
            let closure = Closure::wrap(Box::new(move |event: web_sys::MouseEvent| {
                let rect = canvas_clone.get_bounding_client_rect();
                let scale_x = canvas_clone.width() as f64 / rect.width();
                let scale_y = canvas_clone.height() as f64 / rect.height();
                
                let new_x = ((event.client_x() as f64 - rect.left()) * scale_x) as f32;
                let new_y = ((event.client_y() as f64 - rect.top()) * scale_y) as f32;
                
                mx.set(new_x);
                my.set(new_y);

                // LOG TO CONSOLE:
                // Uncomment for logging the Mouse:
                // web_sys::console::log_1(&format!("WASM Mouse: {}, {}", new_x, new_y).into());
            }) as Box<dyn FnMut(_)>);
            
            // Note: "mousemove" pairs with web_sys::MouseEvent
            canvas.add_event_listener_with_callback("mousemove", closure.as_ref().unchecked_ref()).unwrap();
            closure.forget();
        }

        // --- Mouse Down / Up ---
        {
            let md = mouse_down.clone();
            let closure = Closure::wrap(Box::new(move |_event: web_sys::MouseEvent| { md.set(true); }) as Box<dyn FnMut(_)>);
            canvas.add_event_listener_with_callback("mousedown", closure.as_ref().unchecked_ref()).unwrap();
            closure.forget();

            let md_up = mouse_down.clone();
            let closure_up = Closure::wrap(Box::new(move |_event: web_sys::MouseEvent| { md_up.set(false); }) as Box<dyn FnMut(_)>);
            window.add_event_listener_with_callback("mouseup", closure_up.as_ref().unchecked_ref()).unwrap();
            closure_up.forget();
        }

        // ── keydown ─────────────────────────
        {
            let held_clone = held.clone();
            let closure = Closure::wrap(Box::new(move |event: web_sys::KeyboardEvent| {
                if let Some(target) = event.target() {
                    if let Ok(el) = target.dyn_into::<web_sys::Element>() {
                        let tag = el.tag_name();
                        if tag == "TEXTAREA" || tag == "INPUT" {
                            return; // Ignore keystrokes while typing in the editor
                        }
                    }
                }
                let code = event.key_code() as u16;

                if CAPTURED_KEYS.contains(&code) {
                    event.prevent_default();
                }

                let idx = code as usize;
                if idx < 256 {
                    let mut keys = held_clone.borrow_mut();
                    if !keys[idx] {
                        keys[idx] = true;
                        web_sys::console::log_1(
                            &format!("🎮 Key DOWN: {} (code {})", event.key(), code).into(),
                        );
                    }
                }
            }) as Box<dyn FnMut(_)>);

            window.add_event_listener_with_callback(
                "keydown",
                closure.as_ref().unchecked_ref(),
            )?;
            closure.forget();
        }

        // ── keyup ───────────────────────────
        {
            let held_clone = held.clone();
            let closure = Closure::wrap(Box::new(move |event: web_sys::KeyboardEvent| {
                if let Some(target) = event.target() {
                    if let Ok(el) = target.dyn_into::<web_sys::Element>() {
                        let tag = el.tag_name();
                        if tag == "TEXTAREA" || tag == "INPUT" {
                            return; // Ignore keystrokes while typing in the editor
                        }
                    }
                }
                let code = event.key_code() as u16;
                let idx = code as usize;
                if idx < 256 {
                    held_clone.borrow_mut()[idx] = false;
                    web_sys::console::log_1(
                        &format!("🎮 Key UP:   {} (code {})", event.key(), code).into(),
                    );
                }
            }) as Box<dyn FnMut(_)>);

            window.add_event_listener_with_callback(
                "keyup",
                closure.as_ref().unchecked_ref(),
            )?;
            closure.forget();
        }

        web_sys::console::log_1(&"🎮 Input system initialized".into());

        Ok(Self { held, mouse_x, mouse_y, mouse_down })
    }

    /// Returns true if the given key code is currently held down.
    pub fn is_key_down(&self, key_code: u16) -> bool {
        let idx = key_code as usize;
        if idx < 256 { self.held.borrow()[idx] } else { false }
    }

    pub fn held_keys(&self) -> Vec<u16> {
        let keys = self.held.borrow();
        let mut result = Vec::new();
        for i in 0..256 {
            if keys[i] {
                result.push(i as u16);
            }
        }
        result
    }

    pub fn held_count(&self) -> usize {
        self.held.borrow().iter().filter(|&&v| v).count()
    }

    pub fn mouse_x(&self) -> f32 { self.mouse_x.get() }
    pub fn mouse_y(&self) -> f32 { self.mouse_y.get() }
    pub fn mouse_down(&self) -> bool { self.mouse_down.get() }

    /// Translates a key code to a readable name for debug display.
    pub fn key_name(code: u16) -> &'static str {
        match code {
            // ── Letters ─────────────────────
            keys::A => "A", 66 => "B", 67 => "C",
            keys::D => "D", 69 => "E", 70 => "F",
            71 => "G", 72 => "H", 73 => "I",
            74 => "J", 75 => "K", 76 => "L",
            77 => "M", 78 => "N", 79 => "O",
            80 => "P", 81 => "Q", 82 => "R",
            keys::S => "S", 84 => "T", 85 => "U",
            86 => "V", keys::W => "W", 88 => "X",
            89 => "Y", 90 => "Z",

            // ── Numbers ─────────────────────
            48 => "0", 49 => "1", 50 => "2", 51 => "3",
            52 => "4", 53 => "5", 54 => "6", 55 => "7",
            56 => "8", 57 => "9",

            // ── Arrows ──────────────────────
            keys::LEFT => "Left", keys::UP => "Up",
            keys::RIGHT => "Right", keys::DOWN => "Down",

            // ── Common ──────────────────────
            keys::SPACE => "Space", keys::ENTER => "Enter",
            keys::ESCAPE => "Esc", 16 => "Shift",
            17 => "Ctrl", 18 => "Alt",
            8 => "Backspace", 9 => "Tab",
            46 => "Delete",

            // ── Punctuation ─────────────────
            186 => ";", 187 => "=", 188 => ",",
            189 => "-", 190 => ".", 191 => "/",
            192 => "`", 219 => "[", 220 => "\\",
            221 => "]", 222 => "'",

            _ => "?",
        }
    }
}