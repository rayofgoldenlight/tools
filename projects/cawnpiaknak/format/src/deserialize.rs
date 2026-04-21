use crate::*;
use flate2::read::ZlibDecoder;
use std::io::Read;

#[derive(Debug)]
pub enum ParseError {
    TooShort,
    BadMagic,
    UnknownShapeType(u8),
    UnknownDrawMode(u8),
    BadSection(u16),
    BadUTF8,
    DecompressionFailed,
}

struct Reader<'a> {
    data: &'a [u8],
    pos: usize,
}

impl<'a> Reader<'a> {
    fn new(data: &'a [u8]) -> Self {
        Self { data, pos: 0 }
    }

    fn remaining(&self) -> usize {
        self.data.len() - self.pos
    }

    fn read_u8(&mut self) -> Result<u8, ParseError> {
        if self.remaining() < 1 { return Err(ParseError::TooShort); }
        let v = self.data[self.pos];
        self.pos += 1;
        Ok(v)
    }

    fn read_u16(&mut self) -> Result<u16, ParseError> {
        if self.remaining() < 2 { return Err(ParseError::TooShort); }
        let v = u16::from_le_bytes([self.data[self.pos], self.data[self.pos + 1]]);
        self.pos += 2;
        Ok(v)
    }

    fn read_u32(&mut self) -> Result<u32, ParseError> {
        if self.remaining() < 4 { return Err(ParseError::TooShort); }
        let v = u32::from_le_bytes([
            self.data[self.pos],
            self.data[self.pos + 1],
            self.data[self.pos + 2],
            self.data[self.pos + 3],
        ]);
        self.pos += 4;
        Ok(v)
    }

    fn read_f32(&mut self) -> Result<f32, ParseError> {
        if self.remaining() < 4 { return Err(ParseError::TooShort); }
        let bytes = [
            self.data[self.pos],
            self.data[self.pos + 1],
            self.data[self.pos + 2],
            self.data[self.pos + 3],
        ];
        self.pos += 4;
        Ok(f32::from_le_bytes(bytes))
    }

    fn read_bytes(&mut self, n: usize) -> Result<&'a [u8], ParseError> {
        if self.remaining() < n { return Err(ParseError::TooShort); }
        let slice = &self.data[self.pos..self.pos + n];
        self.pos += n;
        Ok(slice)
    }
}

fn read_color(r: &mut Reader) -> Result<Color, ParseError> {
    Ok(Color {
        r: r.read_u8()?,
        g: r.read_u8()?,
        b: r.read_u8()?,
        a: r.read_u8()?,
    })
}

fn read_header(r: &mut Reader) -> Result<Header, ParseError> {
    let magic_bytes = r.read_bytes(4)?;
    let magic: [u8; 4] = [magic_bytes[0], magic_bytes[1], magic_bytes[2], magic_bytes[3]];
    if magic != MAGIC {
        return Err(ParseError::BadMagic);
    }
    Ok(Header {
        magic,
        version: r.read_u16()?,
        screen_width: r.read_u16()?,
        screen_height: r.read_u16()?,
        section_count: r.read_u16()?,
    })
}

fn read_shape(r: &mut Reader) -> Result<Shape, ParseError> {
    let type_byte = r.read_u8()?;
    let shape_type = match type_byte {
        1 => ShapeType::Circle,
        2 => ShapeType::Rect,
        3 => ShapeType::Path,
        4 => ShapeType::Text,
        _ => return Err(ParseError::UnknownShapeType(type_byte)),
    };

    let mode_byte = r.read_u8()?;
    let draw_mode = match mode_byte {
        1 => DrawMode::Fill,
        2 => DrawMode::Stroke,
        3 => DrawMode::Both,
        _ => return Err(ParseError::UnknownDrawMode(mode_byte)),
    };

    let fill_color = read_color(r)?;
    let stroke_color = read_color(r)?;
    let stroke_width = r.read_f32()?;

    let params = match shape_type {
        ShapeType::Circle => ShapeParams::Circle { radius: r.read_f32()? },
        ShapeType::Rect => ShapeParams::Rect { width: r.read_f32()?, height: r.read_f32()? },
        ShapeType::Path => {
            let cmd_count = r.read_u16()?;
            let mut commands = Vec::with_capacity(cmd_count as usize);
            for _ in 0..cmd_count {
                let cmd_type = r.read_u8()?;
                commands.push(match cmd_type {
                    1 => PathCmd::MoveTo(r.read_f32()?, r.read_f32()?),
                    2 => PathCmd::LineTo(r.read_f32()?, r.read_f32()?),
                    3 => PathCmd::QuadTo(r.read_f32()?, r.read_f32()?, r.read_f32()?, r.read_f32()?),
                    4 => PathCmd::Close,
                    5 => PathCmd::Fill(read_color(r)?),
                    6 => PathCmd::Stroke(read_color(r)?, r.read_f32()?),
                    _ => return Err(ParseError::BadSection(cmd_type as u16)),
                });
            }
            ShapeParams::Path { commands }
        }
        ShapeType::Text => {
            let len = r.read_u16()?;
            let bytes = r.read_bytes(len as usize)?;
            let text = String::from_utf8(bytes.to_vec()).map_err(|_| ParseError::BadUTF8)?;
            let size = r.read_f32()?;
            let align = r.read_u8()?;
            ShapeParams::Text { text, size, align }
        }
    };

    Ok(Shape { shape_type, draw_mode, fill_color, stroke_color, stroke_width, params })
}

fn read_object(r: &mut Reader) -> Result<SceneObject, ParseError> {
    Ok(SceneObject {
        shape_index: r.read_u16()?,
        anim_index: r.read_u16()?,
        anim_time: r.read_f32()?,
        x: r.read_f32()?,
        y: r.read_f32()?,
        rotation: r.read_f32()?,
        scale: r.read_f32()?,
        visible: r.read_u8().unwrap_or(1) != 0,
        value: r.read_f32().unwrap_or(0.0),
        custom: [
            r.read_f32().unwrap_or(0.0),
            r.read_f32().unwrap_or(0.0),
            r.read_f32().unwrap_or(0.0),
            r.read_f32().unwrap_or(0.0),
        ],
    })
}

/// Parse a full GameFile from raw bytes
pub fn decode(data: &[u8]) -> Result<GameFile, ParseError> {
    if data.len() < 4 {
        return Err(ParseError::TooShort);
    }

    let magic = [data[0], data[1], data[2], data[3]];
    let mut uncompressed = Vec::new();

    // Determine if we need to decompress the payload first
    let payload = if magic == crate::MAGIC_Z {
        let mut decoder = ZlibDecoder::new(&data[4..]);
        decoder.read_to_end(&mut uncompressed).map_err(|_| ParseError::DecompressionFailed)?;
        &uncompressed
    } else {
        // Fallback for older uncompressed "GBIN" files
        data
    };

    // Proceed with normal reading on the uncompressed payload
    let mut r = Reader::new(payload);
    let header = read_header(&mut r)?;

    // Read section table
    let mut section_table = Vec::with_capacity(header.section_count as usize);
    for _ in 0..header.section_count {
        let section_type = r.read_u16()?;
        let size = r.read_u32()?;
        section_table.push(SectionEntry { section_type, size });
    }

    // Defaults
    let mut shapes = Vec::new();
    let mut objects = Vec::new();
    let mut bytecode = Vec::new();
    let mut behaviors = Vec::new();
    let mut variable_count: u16 = 0;
    let mut script_source = String::new();
    let mut animations = Vec::new();
    let mut audio = Vec::new();

    // Read each section's data in order
    for entry in &section_table {
        let section_bytes = r.read_bytes(entry.size as usize)?;

        match entry.section_type {
            SECTION_SHAPES => {
                let mut sr = Reader::new(section_bytes);
                let count = sr.read_u16()?;
                shapes = Vec::with_capacity(count as usize);
                for _ in 0..count {
                    shapes.push(read_shape(&mut sr)?);
                }
            }
            SECTION_OBJECTS => {
                let mut sr = Reader::new(section_bytes);
                let count = sr.read_u16()?;
                objects = Vec::with_capacity(count as usize);
                for _ in 0..count {
                    objects.push(read_object(&mut sr)?);
                }
            }
            SECTION_BYTECODE => {
                let mut sr = Reader::new(section_bytes);
                variable_count = sr.read_u16()?;
                
                let global_len = sr.read_u32()?;
                bytecode = sr.read_bytes(global_len as usize)?.to_vec();
                
                let beh_count = sr.read_u16()?;
                let mut decoded_behaviors = Vec::new();
                for _ in 0..beh_count {
                    let shape_idx = sr.read_u16()?;
                    let code_len = sr.read_u32()?;
                    let code = sr.read_bytes(code_len as usize)?.to_vec();
                    decoded_behaviors.push((shape_idx, code));
                }
                behaviors = decoded_behaviors;
            }
            SECTION_SCRIPT => {
                script_source = String::from_utf8(section_bytes.to_vec())
                    .map_err(|_| ParseError::BadUTF8)?;
            }
            SECTION_ANIMATIONS => {
                let mut sr = Reader::new(section_bytes);
                let count = sr.read_u16()?;
                let mut decoded_anims = Vec::with_capacity(count as usize);
                for _ in 0..count {
                    let fps = sr.read_f32()?;
                    let frame_count = sr.read_u16()?;
                    let mut frames = Vec::with_capacity(frame_count as usize);
                    for _ in 0..frame_count {
                        frames.push(sr.read_u16()?);
                    }
                    decoded_anims.push(Animation { frames, fps });
                }
                animations = decoded_anims;
            }
            SECTION_AUDIO => {
                let mut sr = Reader::new(section_bytes);
                let count = sr.read_u16()?;
                let mut decoded_audio = Vec::with_capacity(count as usize);
                for i in 0..count {
                    let mut name = format!("Audio_{}", i);
                    if header.version >= 5 {
                        let name_len = sr.read_u16()?;
                        let name_bytes = sr.read_bytes(name_len as usize)?;
                        if let Ok(s) = String::from_utf8(name_bytes.to_vec()) {
                            name = s;
                        }
                    }
                    let format = match sr.read_u8()? {
                        1 => AudioFormat::Mp3,
                        2 => AudioFormat::Opus,
                        _ => AudioFormat::Synth,
                    };
                    let len = sr.read_u32()?;
                    let data = sr.read_bytes(len as usize)?.to_vec();
                    decoded_audio.push(AudioAsset { name, format, data });
                }
                audio = decoded_audio;
            }
            _ => {
                // Unknown section — skip silently (forward compatibility)
            }
        }
    }

    Ok(GameFile {
        header,
        shapes,
        objects,
        bytecode,
        behaviors,
        variable_count,
        script_source,
        animations,
        audio,
    })
}
