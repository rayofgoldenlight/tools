pub mod serialize;
pub mod deserialize;
pub mod opcodes;
pub mod vm;

/// File magic bytes: "GBIN"
pub const MAGIC: [u8; 4] = [b'G', b'B', b'I', b'N'];

/// File magic bytes: "GBIZ" (Zlib Compressed)
pub const MAGIC_Z: [u8; 4] = [b'G', b'B', b'I', b'Z'];

pub const VERSION: u16 = 5; // section-based format

// ── Section Types ───────────────────────────────
pub const SECTION_SHAPES: u16 = 1;
pub const SECTION_OBJECTS: u16 = 2;
pub const SECTION_BYTECODE: u16 = 3;
pub const SECTION_SCRIPT: u16 = 4;
pub const SECTION_AUDIO: u16 = 5;
pub const SECTION_ANIMATIONS: u16 = 6;
pub const SECTION_TEMPLATES: u16 = 7;

// ── Header ──────────────────────────────────────

#[derive(Debug, Clone)]
pub struct Header {
    pub magic: [u8; 4],       // 4 bytes
    pub version: u16,          // 2 bytes
    pub screen_width: u16,     // 2 bytes
    pub screen_height: u16,    // 2 bytes
    pub section_count: u16,    // 2 bytes
}
// Total: 12 bytes

#[derive(Debug, Clone)]
pub struct SectionEntry {
    pub section_type: u16,    // 2 bytes
    pub size: u32,            // 4 bytes
}
// 6 bytes per entry

// ── Color ───────────────────────────────────────

#[derive(Debug, Clone, Copy)]
pub struct Color {
    pub r: u8,
    pub g: u8,
    pub b: u8,
    pub a: u8,
}

impl Color {
    pub fn rgba(r: u8, g: u8, b: u8, a: u8) -> Self {
        Self { r, g, b, a }
    }

    pub fn rgb(r: u8, g: u8, b: u8) -> Self {
        Self { r, g, b, a: 255 }
    }

    pub fn to_css(&self) -> String {
        format!("rgba({},{},{},{:.2})", self.r, self.g, self.b, self.a as f64 / 255.0)
    }
}

// ── Shapes ──────────────────────────────────────

#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
pub enum ShapeType {
    Circle = 1,
    Rect = 2,
    Path = 3,
    Text = 4,
}

#[derive(Debug, Clone)]
pub enum PathCmd {
    MoveTo(f32, f32),
    LineTo(f32, f32),
    QuadTo(f32, f32, f32, f32), // ControlX, ControlY, TargetX, TargetY
    Close,
    Fill(Color),
    Stroke(Color, f32),
}

#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
pub enum DrawMode {
    Fill = 1,
    Stroke = 2,
    Both = 3,
}

#[derive(Debug, Clone)]
pub struct Shape {
    pub shape_type: ShapeType,
    pub draw_mode: DrawMode,
    pub fill_color: Color,
    pub stroke_color: Color,
    pub stroke_width: f32,
    pub params: ShapeParams,
}

#[derive(Debug, Clone)]
pub enum ShapeParams {
    Circle { radius: f32 },
    Rect { width: f32, height: f32 },
    Path { commands: Vec<PathCmd> },
    Text { text: String, size: f32, align: u8 }, // (align: 0=left, 1=center, 2=right)
}

// ── Scene Objects ───────────────────────────────

#[derive(Debug, Clone)]
pub struct SceneObject {
    pub shape_index: u16,  // The currently visible shape
    pub anim_index: u16,   // 0xFFFF means no animation
    pub anim_time: f32,    // Internal timer for frame advancement
    pub x: f32,
    pub y: f32,
    pub rotation: f32,
    pub scale: f32,
    pub visible: bool,
    pub value: f32,
    pub custom: [f32; 4], // <--- val1, val2, val3, val4
}

#[derive(Debug, Clone)]
pub struct Animation {
    pub frames: Vec<u16>, // List of shape indices
    pub fps: f32,
}

// ── Audio ───────────────────────────────────────

#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
pub enum AudioFormat {
    Mp3 = 1,
    Opus = 2,
    Synth = 3, // Custom ultra-tiny format
}

#[derive(Debug, Clone)]
pub struct AudioAsset {
    pub name: String,
    pub format: AudioFormat,
    pub data: Vec<u8>, // Raw file bytes, or synth parameters
}

// ── Game File ───────────────────────────────────

#[derive(Debug, Clone)]
pub struct GameFile {
    pub header: Header,
    pub shapes: Vec<Shape>,
    pub animations: Vec<Animation>,
    pub audio: Vec<AudioAsset>,
    pub objects: Vec<SceneObject>,
    pub bytecode: Vec<u8>,
    pub behaviors: Vec<(u16, Vec<u8>)>,
    pub variable_count: u16,
    pub script_source: String,
}