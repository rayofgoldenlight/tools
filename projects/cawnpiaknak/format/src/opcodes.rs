/// Bytecode opcodes for the game VM.
/// Each opcode is one byte, some followed by immediate arguments.

#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum OpCode {
    // ── VM Control ──────────────────────────
    Halt        = 0x00,  // stop execution
    Nop         = 0x01,  // do nothing

    // ── Stack Operations ────────────────────
    PushF32     = 0x10,  // [f32]  → push float
    Pop         = 0x12,  //        → discard top
    Dup         = 0x13,  //        → duplicate top

    // ── Arithmetic ──────────────────────────
    Add         = 0x20,  // (a b → a+b)
    Sub         = 0x21,  // (a b → a-b)
    Mul         = 0x22,  // (a b → a*b)
    Div         = 0x23,  // (a b → a/b)
    Neg         = 0x24,  // (a   → -a)
    Abs         = 0x25,  // (a   → |a|)
    Sign        = 0x26,  // (a   → sign(a): -1, 0, or 1)

    // ── Comparison → push 1.0 or 0.0 ───────
    Gt          = 0x30,  // (a b → a>b)
    Lt          = 0x31,  // (a b → a<b)
    Eq          = 0x32,  // (a b → a==b)  (epsilon compare)
    Not         = 0x33,  // (a   → !a)    (0→1, nonzero→0)

    // ── Variables ───────────────────────────
    Load        = 0x40,  // [u16 var_id] → push vars[id]
    Store       = 0x41,  // [u16 var_id] → pop into vars[id]

    // ── Flow Control ────────────────────────
    Jump        = 0x50,  // [u16 addr]   → pc = addr
    JumpIf      = 0x51,  // [u16 addr]   → if pop != 0, pc = addr
    JumpIfNot   = 0x52,  // [u16 addr]   → if pop == 0, pc = addr

    // ── Engine: Input ───────────────────────
    KeyDown     = 0x60,  // [u16 key_code] → push 1.0 if held, else 0.0
    MouseX      = 0x61,  // → push mouse_x
    MouseY      = 0x62,  // → push mouse_y
    MouseDown   = 0x63,  // → push 1.0 if held, else 0.0

    // ── Engine: Object Access ───────────────
    GetObjX     = 0x70,  // [u16 obj_id] → push objects[id].x
    GetObjY     = 0x71,  // [u16 obj_id] → push objects[id].y
    SetObjX     = 0x72,  // [u16 obj_id] → pop value, set objects[id].x
    SetObjY     = 0x73,  // [u16 obj_id] → pop value, set objects[id].y

    GetSelfX    = 0x74,  // [] -> push self.x
    GetSelfY    = 0x75,  // [] -> push self.y
    SetSelfX    = 0x76,  // [val] -> pop value, set self.x
    SetSelfY    = 0x77,  // [val] -> pop value, set self.y

    PlayAnim    = 0x78,  // [anim_id] -> pop anim_id, play animation
    
    PlaySound   = 0x79,

    StopSound   = 0x7A,

    LoopSound   = 0x7B,
    SetVolume   = 0x7C,

    GetObjVisible = 0x81,
    SetObjVisible = 0x82,
    GetObjValue   = 0x83,
    SetObjValue   = 0x84,
    GetSelfVisible = 0x85,
    SetSelfVisible = 0x86,
    GetSelfValue   = 0x87,
    SetSelfValue   = 0x88,

    GetObjCustom = 0x89,  // [id, slot] → push obj[id].custom[slot]
    SetObjCustom = 0x8A,  // [id, slot] → pop val, set obj[id].custom[slot]
    GetSelfCustom = 0x8B, // [slot] → push self.custom[slot]
    SetSelfCustom = 0x8C, // [slot] → pop val, set self.custom[slot]

    GetObjRotation = 0x8D,
    SetObjRotation = 0x8E,
    GetSelfRotation = 0x8F,
    SetSelfRotation = 0x90,
    GetObjScale    = 0x91,
    SetObjScale    = 0x92,
    GetSelfScale   = 0x93,
    SetSelfScale   = 0x94,

    PlayObjAnim    = 0x95,  // (obj_id, anim_id) -> pops both, plays animation globally

    Jump32      = 0x96,  // [u32 addr]   → pc = addr
    JumpIf32    = 0x97,  // [u32 addr]   → if pop != 0, pc = addr
    JumpIfNot32 = 0x98,  // [u32 addr]   → if pop == 0, pc = addr

    // ── Engine: Time ────────────────────────
    PushDt      = 0x80,  // → push delta time (seconds)
}

impl OpCode {
    /// Decode a byte into an opcode, if valid
    pub fn from_byte(b: u8) -> Option<OpCode> {
        match b {
            0x00 => Some(OpCode::Halt),
            0x01 => Some(OpCode::Nop),
            0x10 => Some(OpCode::PushF32),
            0x12 => Some(OpCode::Pop),
            0x13 => Some(OpCode::Dup),
            0x20 => Some(OpCode::Add),
            0x21 => Some(OpCode::Sub),
            0x22 => Some(OpCode::Mul),
            0x23 => Some(OpCode::Div),
            0x24 => Some(OpCode::Neg),
            0x25 => Some(OpCode::Abs),
            0x26 => Some(OpCode::Sign),
            0x30 => Some(OpCode::Gt),
            0x31 => Some(OpCode::Lt),
            0x32 => Some(OpCode::Eq),
            0x33 => Some(OpCode::Not),
            0x40 => Some(OpCode::Load),
            0x41 => Some(OpCode::Store),
            0x50 => Some(OpCode::Jump),
            0x51 => Some(OpCode::JumpIf),
            0x52 => Some(OpCode::JumpIfNot),
            0x60 => Some(OpCode::KeyDown),
            0x61 => Some(OpCode::MouseX),
            0x62 => Some(OpCode::MouseY),
            0x63 => Some(OpCode::MouseDown),
            0x70 => Some(OpCode::GetObjX),
            0x71 => Some(OpCode::GetObjY),
            0x72 => Some(OpCode::SetObjX),
            0x73 => Some(OpCode::SetObjY),
            0x74 => Some(OpCode::GetSelfX),
            0x75 => Some(OpCode::GetSelfY),
            0x76 => Some(OpCode::SetSelfX),
            0x77 => Some(OpCode::SetSelfY),
            0x78 => Some(OpCode::PlayAnim),
            0x79 => Some(OpCode::PlaySound),
            0x7A => Some(OpCode::StopSound),
            0x7B => Some(OpCode::LoopSound),
            0x7C => Some(OpCode::SetVolume),
            0x80 => Some(OpCode::PushDt),
            0x81 => Some(OpCode::GetObjVisible),
            0x82 => Some(OpCode::SetObjVisible),
            0x83 => Some(OpCode::GetObjValue),
            0x84 => Some(OpCode::SetObjValue),
            0x85 => Some(OpCode::GetSelfVisible),
            0x86 => Some(OpCode::SetSelfVisible),
            0x87 => Some(OpCode::GetSelfValue),
            0x88 => Some(OpCode::SetSelfValue),
            0x89 => Some(OpCode::GetObjCustom),
            0x8A => Some(OpCode::SetObjCustom),
            0x8B => Some(OpCode::GetSelfCustom),
            0x8C => Some(OpCode::SetSelfCustom),
            0x8D => Some(OpCode::GetObjRotation),
            0x8E => Some(OpCode::SetObjRotation),
            0x8F => Some(OpCode::GetSelfRotation),
            0x90 => Some(OpCode::SetSelfRotation),
            0x91 => Some(OpCode::GetObjScale),
            0x92 => Some(OpCode::SetObjScale),
            0x93 => Some(OpCode::GetSelfScale),
            0x94 => Some(OpCode::SetSelfScale),
            0x95 => Some(OpCode::PlayObjAnim), 
            0x95 => Some(OpCode::PlayObjAnim),
            0x96 => Some(OpCode::Jump32),      
            0x97 => Some(OpCode::JumpIf32),  
            0x98 => Some(OpCode::JumpIfNot32),
            
            _    => None,
        }
    }

    /// How many extra bytes follow this opcode
    pub fn arg_size(self) -> usize {
        match self {
            OpCode::PushF32  => 4,  // f32
            OpCode::Load     => 2,  // u16
            OpCode::Store    => 2,  // u16
            OpCode::Jump     => 2,  // u16
            OpCode::JumpIf   => 2,  // u16
            OpCode::JumpIfNot=> 2,  // u16
            OpCode::KeyDown  => 2,  // u16
            OpCode::GetObjX  => 2,  // u16
            OpCode::GetObjY  => 2,  // u16
            OpCode::SetObjX  => 2,  // u16
            OpCode::SetObjY  => 2,  // u16
            OpCode::PlaySound => 2, // u16
            OpCode::StopSound => 2, // u16
            OpCode::LoopSound => 2, // u16
            OpCode::SetVolume => 2, // (id is u16, plus pops stack)
            // Obj ones take 3 extra bytes: u16 (id) + u8 (slot)
            OpCode::GetObjCustom | OpCode::SetObjCustom => 3, 
            // Self ones take 1 extra byte: u8 (slot)
            OpCode::GetSelfCustom | OpCode::SetSelfCustom => 1,
            OpCode::GetObjVisible | OpCode::SetObjVisible => 2,
            OpCode::GetObjValue | OpCode::SetObjValue => 2,
            OpCode::GetObjRotation | OpCode::SetObjRotation | OpCode::GetObjScale | OpCode::SetObjScale => 2,
            OpCode::Jump32 | OpCode::JumpIf32 | OpCode::JumpIfNot32 => 4,
            _                => 0,
        }
    }
}

// ── Bytecode Builder (for tools / compiler) ─────

/// Helper to assemble bytecode by hand or from a compiler
pub struct BytecodeBuilder {
    pub code: Vec<u8>,
}

impl BytecodeBuilder {
    pub fn new() -> Self {
        Self { code: Vec::new() }
    }

    /// Emit a bare opcode (no arguments)
    pub fn emit(&mut self, op: OpCode) -> usize {
        let pos = self.code.len();
        self.code.push(op as u8);
        pos
    }

    /// Emit opcode + f32 argument
    pub fn emit_f32(&mut self, op: OpCode, val: f32) -> usize {
        let pos = self.code.len();
        self.code.push(op as u8);
        self.code.extend_from_slice(&val.to_le_bytes());
        pos
    }

    /// Emit opcode + u16 argument
    pub fn emit_u16(&mut self, op: OpCode, val: u16) -> usize {
        let pos = self.code.len();
        self.code.push(op as u8);
        self.code.extend_from_slice(&val.to_le_bytes());
        pos
    }

    pub fn emit_u32(&mut self, op: OpCode, val: u32) -> usize {
        let pos = self.code.len();
        self.code.push(op as u8);
        self.code.extend_from_slice(&val.to_le_bytes());
        pos
    }

    /// Current write position (useful for jump targets)
    pub fn pos(&self) -> u32 {
        self.code.len() as u32
    }

    /// Patch a u16 argument at a previously emitted instruction
    /// `instr_pos` is the position returned by emit_u16; the u16 starts at instr_pos+1
    pub fn patch_u16(&mut self, instr_pos: usize, val: u16) {
        let bytes = val.to_le_bytes();
        self.code[instr_pos + 1] = bytes[0];
        self.code[instr_pos + 2] = bytes[1];
    }

    pub fn patch_u32(&mut self, instr_pos: usize, val: u32) {
        let bytes = val.to_le_bytes();
        self.code[instr_pos + 1] = bytes[0];
        self.code[instr_pos + 2] = bytes[1];
        self.code[instr_pos + 3] = bytes[2];
        self.code[instr_pos + 4] = bytes[3];
    }

    /// Return the finished bytecode
    pub fn finish(self) -> Vec<u8> {
        self.code
    }
}
