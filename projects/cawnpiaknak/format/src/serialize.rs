use crate::*;
use flate2::write::ZlibEncoder;
use flate2::Compression;
use std::io::Write;

pub trait WriteBytes {
    fn write_bytes(&self, buf: &mut Vec<u8>);
}

// ── Primitives ──────────────────────────────────

fn write_u8(buf: &mut Vec<u8>, v: u8) {
    buf.push(v);
}

fn write_u16(buf: &mut Vec<u8>, v: u16) {
    buf.extend_from_slice(&v.to_le_bytes());
}

fn write_u32(buf: &mut Vec<u8>, v: u32) {
    buf.extend_from_slice(&v.to_le_bytes());
}

fn write_f32(buf: &mut Vec<u8>, v: f32) {
    buf.extend_from_slice(&v.to_le_bytes());
}

// ── Structs ─────────────────────────────────────

impl WriteBytes for Color {
    fn write_bytes(&self, buf: &mut Vec<u8>) {
        write_u8(buf, self.r);
        write_u8(buf, self.g);
        write_u8(buf, self.b);
        write_u8(buf, self.a);
    }
}

impl WriteBytes for Header {
    fn write_bytes(&self, buf: &mut Vec<u8>) {
        buf.extend_from_slice(&self.magic);
        write_u16(buf, self.version);
        write_u16(buf, self.screen_width);
        write_u16(buf, self.screen_height);
        write_u16(buf, self.section_count);
    }
}

impl WriteBytes for SectionEntry {
    fn write_bytes(&self, buf: &mut Vec<u8>) {
        write_u16(buf, self.section_type);
        write_u32(buf, self.size);
    }
}

impl WriteBytes for Shape {
    fn write_bytes(&self, buf: &mut Vec<u8>) {
        write_u8(buf, self.shape_type as u8);
        write_u8(buf, self.draw_mode as u8);
        self.fill_color.write_bytes(buf);
        self.stroke_color.write_bytes(buf);
        write_f32(buf, self.stroke_width);
        match &self.params {
            ShapeParams::Circle { radius } => {
                write_f32(buf, *radius);
            }
            ShapeParams::Rect { width, height } => {
                write_f32(buf, *width);
                write_f32(buf, *height);
            }
            ShapeParams::Path { commands } => {
                write_u16(buf, commands.len() as u16);
                for cmd in commands {
                    match cmd {
                        PathCmd::MoveTo(x, y) => { write_u8(buf, 1); write_f32(buf, *x); write_f32(buf, *y); }
                        PathCmd::LineTo(x, y) => { write_u8(buf, 2); write_f32(buf, *x); write_f32(buf, *y); }
                        PathCmd::QuadTo(cx, cy, x, y) => { 
                            write_u8(buf, 3); 
                            write_f32(buf, *cx); write_f32(buf, *cy); 
                            write_f32(buf, *x); write_f32(buf, *y); 
                        }
                        PathCmd::Close => { write_u8(buf, 4); }
                        PathCmd::Fill(c) => { write_u8(buf, 5); c.write_bytes(buf); }
                        PathCmd::Stroke(c, w) => { write_u8(buf, 6); c.write_bytes(buf); write_f32(buf, *w); }
                    }
                }
            }
            ShapeParams::Text { text, size, align } => {
                let bytes = text.as_bytes();
                write_u16(buf, bytes.len() as u16);
                buf.extend_from_slice(bytes);
                write_f32(buf, *size);
                write_u8(buf, *align);
            }
        }
    }
}

impl WriteBytes for SceneObject {
    fn write_bytes(&self, buf: &mut Vec<u8>) {
        write_u16(buf, self.shape_index);
        write_u16(buf, self.anim_index);
        write_f32(buf, self.anim_time);
        write_f32(buf, self.x);
        write_f32(buf, self.y);
        write_f32(buf, self.rotation);
        write_f32(buf, self.scale);
        write_u8(buf, if self.visible { 1 } else { 0 });
        write_f32(buf, self.value);
        write_f32(buf, self.custom[0]);
        write_f32(buf, self.custom[1]);
        write_f32(buf, self.custom[2]);
        write_f32(buf, self.custom[3]);
    }
}

impl WriteBytes for GameFile {
    fn write_bytes(&self, buf: &mut Vec<u8>) {

        // ── Build section data blobs first ──────────
        // Shapes section: [u16 count] [shape data...]
        let mut shapes_data = Vec::new();
        write_u16(&mut shapes_data, self.shapes.len() as u16);
        for shape in &self.shapes {
            shape.write_bytes(&mut shapes_data);
        }

        // Objects section: [u16 count] [object data...]
        let mut objects_data = Vec::new();
        write_u16(&mut objects_data, self.objects.len() as u16);
        for object in &self.objects {
            object.write_bytes(&mut objects_data);
        }

        // Bytecode section: [u16 var_count] [u32 global_len] [global_bytecode...] [u16 beh_count] [behaviors...]
        let mut bytecode_data = Vec::new();
        write_u16(&mut bytecode_data, self.variable_count);
        
        write_u32(&mut bytecode_data, self.bytecode.len() as u32);
        bytecode_data.extend_from_slice(&self.bytecode);
        
        write_u16(&mut bytecode_data, self.behaviors.len() as u16);
        for (shape_idx, code) in &self.behaviors {
            write_u16(&mut bytecode_data, *shape_idx);
            write_u32(&mut bytecode_data, code.len() as u32);
            bytecode_data.extend_from_slice(code);
        }

        // Script section: [raw UTF-8 bytes]
        let script_data = self.script_source.as_bytes();

        // Animations section: [u16 count] [animations...]
        let mut anims_data = Vec::new();
        write_u16(&mut anims_data, self.animations.len() as u16);
        for anim in &self.animations {
            write_f32(&mut anims_data, anim.fps);
            write_u16(&mut anims_data, anim.frames.len() as u16);
            for &frame in &anim.frames {
                write_u16(&mut anims_data, frame);
            }
        }

        // Audio section: [u16 count] [format_byte, u32_length, data...]
        let mut audio_data = Vec::new();
        write_u16(&mut audio_data, self.audio.len() as u16);
        for asset in &self.audio {
            let name_bytes = asset.name.as_bytes();
            write_u16(&mut audio_data, name_bytes.len() as u16);
            audio_data.extend_from_slice(name_bytes);
            
            write_u8(&mut audio_data, asset.format as u8);
            write_u32(&mut audio_data, asset.data.len() as u32);
            audio_data.extend_from_slice(&asset.data);
        }

        // ── Write section table ─────────────────────

        let sections: Vec<SectionEntry> = vec![
            SectionEntry { section_type: crate::SECTION_SHAPES, size: shapes_data.len() as u32 },
            SectionEntry { section_type: crate::SECTION_ANIMATIONS, size: anims_data.len() as u32 },
            SectionEntry { section_type: crate::SECTION_AUDIO, size: audio_data.len() as u32 },
            SectionEntry { section_type: crate::SECTION_OBJECTS, size: objects_data.len() as u32 },
            SectionEntry { section_type: crate::SECTION_BYTECODE, size: bytecode_data.len() as u32 },
            SectionEntry { section_type: crate::SECTION_SCRIPT, size: script_data.len() as u32 },
        ];

        let mut safe_header = self.header.clone();
        safe_header.section_count = sections.len() as u16;
        safe_header.write_bytes(buf);

        for section in &sections {
            section.write_bytes(buf);
        }
        // ── Write section data in order ─────────────
        buf.extend_from_slice(&shapes_data);
        buf.extend_from_slice(&anims_data);
        buf.extend_from_slice(&audio_data);
        buf.extend_from_slice(&objects_data);
        buf.extend_from_slice(&bytecode_data);
        buf.extend_from_slice(script_data);
    }
}
/// Convenience: serialize a GameFile to a Vec<u8> and compress it
pub fn encode(game: &GameFile) -> Vec<u8> {
    // 1. Write the standard uncompressed payload
    let mut buf = Vec::new();
    game.write_bytes(&mut buf);

    // 2. Compress the entire payload using Zlib (Maximum compression)
    let mut encoder = ZlibEncoder::new(Vec::new(), Compression::best());
    encoder.write_all(&buf).unwrap();
    let compressed = encoder.finish().unwrap();

    // 3. Wrap in our compressed format: [MAGIC_Z] + [compressed_bytes]
    let mut final_out = Vec::new();
    final_out.extend_from_slice(&crate::MAGIC_Z);
    final_out.extend_from_slice(&compressed);
    
    final_out
}