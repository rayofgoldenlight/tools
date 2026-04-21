use crate::opcodes::OpCode;

// ── Errors ──────────────────────────────────────

#[derive(Debug, PartialEq)]
pub enum VMError {
    StackUnderflow,
    InvalidOpcode(u8),
    BytecodeOverrun,
    DivisionByZero,
    VariableOutOfBounds(u16),
    ObjectOutOfBounds(u16),
    NoSelfBound,
}

// ── Engine Interface ────────────────────────────
// Implemented by the runtime
// For now we test with a dummy.

pub trait EngineContext {
    fn key_down(&self, key_code: u16) -> bool;
    fn get_obj_x(&self, obj_id: u16) -> Result<f32, VMError>;
    fn get_obj_y(&self, obj_id: u16) -> Result<f32, VMError>;
    fn set_obj_x(&mut self, obj_id: u16, val: f32) -> Result<(), VMError>;
    fn set_obj_y(&mut self, obj_id: u16, val: f32) -> Result<(), VMError>;
    fn get_dt(&self) -> f32;
    fn play_anim(&mut self, obj_id: u16, anim_id: u16) -> Result<(), VMError>;
    fn play_sound(&mut self, sound_id: u16) -> Result<(), VMError>;
    fn stop_sound(&mut self, sound_id: u16) -> Result<(), VMError>;
    fn loop_sound(&mut self, sound_id: u16) -> Result<(), VMError>;
    fn set_volume(&mut self, sound_id: u16, vol: f32) -> Result<(), VMError>;
    fn get_obj_visible(&self, obj_id: u16) -> Result<f32, VMError>;
    fn set_obj_visible(&mut self, obj_id: u16, val: f32) -> Result<(), VMError>;
    fn get_obj_value(&self, obj_id: u16) -> Result<f32, VMError>;
    fn set_obj_value(&mut self, obj_id: u16, val: f32) -> Result<(), VMError>;
    fn mouse_x(&self) -> f32;
    fn mouse_y(&self) -> f32;
    fn mouse_down(&self) -> bool;
    fn get_obj_custom(&self, obj_id: u16, slot: u8) -> Result<f32, VMError>;
    fn set_obj_custom(&mut self, obj_id: u16, slot: u8, val: f32) -> Result<(), VMError>;
    fn get_obj_rotation(&self, obj_id: u16) -> Result<f32, VMError>;
    fn set_obj_rotation(&mut self, obj_id: u16, val: f32) -> Result<(), VMError>;
    fn get_obj_scale(&self, obj_id: u16) -> Result<f32, VMError>;
    fn set_obj_scale(&mut self, obj_id: u16, val: f32) -> Result<(), VMError>;
}

// ── VM ──────────────────────────────────────────

pub struct VM {
    pub stack: Vec<f32>,
    pub variables: Vec<f32>,
    pub pc: usize,
    pub halted: bool,
    pub current_object: Option<u16>,
}

impl VM {
    pub fn new(variable_count: u16) -> Self {
        Self {
            stack: Vec::with_capacity(64),
            variables: vec![0.0; variable_count as usize],
            pc: 0,
            halted: false,
            current_object: None,
        }
    }

    /// Run one full frame of bytecode.
    /// Resets pc and stack, keeps variables (they are persistent game state).
    pub fn run_frame(
        &mut self,
        bytecode: &[u8],
        ctx: &mut dyn EngineContext,
    ) -> Result<(), VMError> {
        self.pc = 0;
        self.stack.clear();
        self.halted = false;

        while !self.halted && self.pc < bytecode.len() {
            self.step(bytecode, ctx)?;
        }

        Ok(())
    }

    // ── Stack helpers ───────────────────────

    fn push(&mut self, val: f32) {
        self.stack.push(val);
    }

    fn pop(&mut self) -> Result<f32, VMError> {
        self.stack.pop().ok_or(VMError::StackUnderflow)
    }

    fn peek(&self) -> Result<f32, VMError> {
        self.stack.last().copied().ok_or(VMError::StackUnderflow)
    }

    // ── Bytecode reading helpers ────────────

    fn read_u8(&mut self, bytecode: &[u8]) -> Result<u8, VMError> {
        if self.pc >= bytecode.len() {
            return Err(VMError::BytecodeOverrun);
        }
        let v = bytecode[self.pc];
        self.pc += 1;
        Ok(v)
    }

    fn read_u16(&mut self, bytecode: &[u8]) -> Result<u16, VMError> {
        if self.pc + 2 > bytecode.len() {
            return Err(VMError::BytecodeOverrun);
        }
        let v = u16::from_le_bytes([bytecode[self.pc], bytecode[self.pc + 1]]);
        self.pc += 2;
        Ok(v)
    }

    fn read_f32(&mut self, bytecode: &[u8]) -> Result<f32, VMError> {
        if self.pc + 4 > bytecode.len() {
            return Err(VMError::BytecodeOverrun);
        }
        let v = f32::from_le_bytes([
            bytecode[self.pc],
            bytecode[self.pc + 1],
            bytecode[self.pc + 2],
            bytecode[self.pc + 3],
        ]);
        self.pc += 4;
        Ok(v)
    }

    fn read_u32(&mut self, bytecode: &[u8]) -> Result<u32, VMError> {
        if self.pc + 4 > bytecode.len() { return Err(VMError::BytecodeOverrun); }
        let v = u32::from_le_bytes([bytecode[self.pc], bytecode[self.pc+1], bytecode[self.pc+2], bytecode[self.pc+3]]);
        self.pc += 4;
        Ok(v)
    }


    // ── Variable access ─────────────────────

    fn load_var(&self, id: u16) -> Result<f32, VMError> {
        self.variables
            .get(id as usize)
            .copied()
            .ok_or(VMError::VariableOutOfBounds(id))
    }

    fn store_var(&mut self, id: u16, val: f32) -> Result<(), VMError> {
        if (id as usize) < self.variables.len() {
            self.variables[id as usize] = val;
            Ok(())
        } else {
            Err(VMError::VariableOutOfBounds(id))
        }
    }

    // ── Execute one instruction ─────────────

    fn step(
        &mut self,
        bytecode: &[u8],
        ctx: &mut dyn EngineContext,
    ) -> Result<(), VMError> {
        let op_byte = self.read_u8(bytecode)?;
        let op = OpCode::from_byte(op_byte).ok_or(VMError::InvalidOpcode(op_byte))?;

        match op {
            // ── VM Control ──────────────────
            OpCode::Halt => {
                self.halted = true;
            }
            OpCode::Nop => {}

            // ── Stack Operations ────────────
            OpCode::PushF32 => {
                let val = self.read_f32(bytecode)?;
                self.push(val);
            }
            OpCode::Pop => {
                self.pop()?;
            }
            OpCode::Dup => {
                let val = self.peek()?;
                self.push(val);
            }

            // ── Arithmetic ──────────────────
            OpCode::Add => {
                let b = self.pop()?;
                let a = self.pop()?;
                self.push(a + b);
            }
            OpCode::Sub => {
                let b = self.pop()?;
                let a = self.pop()?;
                self.push(a - b);
            }
            OpCode::Mul => {
                let b = self.pop()?;
                let a = self.pop()?;
                self.push(a * b);
            }
            OpCode::Div => {
                let b = self.pop()?;
                let a = self.pop()?;
                if b == 0.0 {
                    return Err(VMError::DivisionByZero);
                }
                self.push(a / b);
            }
            OpCode::Neg => {
                let a = self.pop()?;
                self.push(-a);
            }

            OpCode::Abs => { let a = self.pop()?; self.push(a.abs()); }
            OpCode::Sign => { 
                let a = self.pop()?; 
                self.push(if a == 0.0 { 0.0 } else { a.signum() }); 
            }

            // ── Comparison ──────────────────
            OpCode::Gt => {
                let b = self.pop()?;
                let a = self.pop()?;
                self.push(if a > b { 1.0 } else { 0.0 });
            }
            OpCode::Lt => {
                let b = self.pop()?;
                let a = self.pop()?;
                self.push(if a < b { 1.0 } else { 0.0 });
            }
            OpCode::Eq => {
                let b = self.pop()?;
                let a = self.pop()?;
                self.push(if (a - b).abs() < f32::EPSILON { 1.0 } else { 0.0 });
            }
            OpCode::Not => {
                let a = self.pop()?;
                self.push(if a == 0.0 { 1.0 } else { 0.0 });
            }

            // ── Variables ───────────────────
            OpCode::Load => {
                let var_id = self.read_u16(bytecode)?;
                let val = self.load_var(var_id)?;
                self.push(val);
            }
            OpCode::Store => {
                let var_id = self.read_u16(bytecode)?;
                let val = self.pop()?;
                self.store_var(var_id, val)?;
            }

            // ── Flow Control ────────────────
            OpCode::Jump => {
                let addr = self.read_u16(bytecode)? as usize;
                if addr >= bytecode.len() { return Err(VMError::BytecodeOverrun); }
                self.pc = addr;
            }
            OpCode::JumpIf => {
                let addr = self.read_u16(bytecode)? as usize;
                let val = self.pop()?;
                if val != 0.0 {
                    if addr >= bytecode.len() { return Err(VMError::BytecodeOverrun); }
                    self.pc = addr;
                }
            }
            OpCode::JumpIfNot => {
                let addr = self.read_u16(bytecode)? as usize;
                let val = self.pop()?;
                if val == 0.0 {
                    if addr >= bytecode.len() { return Err(VMError::BytecodeOverrun); }
                    self.pc = addr;
                }
            }
            OpCode::Jump32 => {
                let addr = self.read_u32(bytecode)? as usize;
                if addr >= bytecode.len() { return Err(VMError::BytecodeOverrun); }
                self.pc = addr;
            }
            OpCode::JumpIf32 => {
                let addr = self.read_u32(bytecode)? as usize;
                let val = self.pop()?;
                if val != 0.0 {
                    if addr >= bytecode.len() { return Err(VMError::BytecodeOverrun); }
                    self.pc = addr;
                }
            }
            OpCode::JumpIfNot32 => {
                let addr = self.read_u32(bytecode)? as usize;
                let val = self.pop()?;
                if val == 0.0 {
                    if addr >= bytecode.len() { return Err(VMError::BytecodeOverrun); }
                    self.pc = addr;
                }
            }

            // ── Engine: Input ───────────────
            OpCode::KeyDown => {
                let key_code = self.read_u16(bytecode)?;
                let pressed = ctx.key_down(key_code);
                self.push(if pressed { 1.0 } else { 0.0 });
            }

            OpCode::MouseX => { self.push(ctx.mouse_x()); }
            OpCode::MouseY => { self.push(ctx.mouse_y()); }
            OpCode::MouseDown => { self.push(if ctx.mouse_down() { 1.0 } else { 0.0 }); }

            // ── Engine: Object Access ───────
            OpCode::GetObjX => {
                let obj_id = self.read_u16(bytecode)?;
                let val = ctx.get_obj_x(obj_id)?;
                self.push(val);
            }
            OpCode::GetObjY => {
                let obj_id = self.read_u16(bytecode)?;
                let val = ctx.get_obj_y(obj_id)?;
                self.push(val);
            }
            OpCode::SetObjX => {
                let obj_id = self.read_u16(bytecode)?;
                let val = self.pop()?;
                ctx.set_obj_x(obj_id, val)?;
            }
            OpCode::SetObjY => {
                let obj_id = self.read_u16(bytecode)?;
                let val = self.pop()?;
                ctx.set_obj_y(obj_id, val)?;
            }


            OpCode::GetSelfX => {
                let obj_id = self.current_object.ok_or(VMError::NoSelfBound)?;
                let val = ctx.get_obj_x(obj_id)?;
                self.push(val);
            }
            OpCode::GetSelfY => {
                let obj_id = self.current_object.ok_or(VMError::NoSelfBound)?;
                let val = ctx.get_obj_y(obj_id)?;
                self.push(val);
            }
            OpCode::SetSelfX => {
                let obj_id = self.current_object.ok_or(VMError::NoSelfBound)?;
                let val = self.pop()?;
                ctx.set_obj_x(obj_id, val)?;
            }
            OpCode::SetSelfY => {
                let obj_id = self.current_object.ok_or(VMError::NoSelfBound)?;
                let val = self.pop()?;
                ctx.set_obj_y(obj_id, val)?;
            }

            OpCode::PlayAnim => {
                let anim_id = self.pop()? as u16;
                let obj_id = self.current_object.ok_or(VMError::NoSelfBound)?;
                ctx.play_anim(obj_id, anim_id)?;
            }

            OpCode::PlayObjAnim => {
                let anim_id = self.pop()? as u16;
                let obj_id = self.pop()? as u16;
                ctx.play_anim(obj_id, anim_id)?;
            }

            OpCode::PlaySound => {
                let sound_id = self.read_u16(bytecode)?;
                ctx.play_sound(sound_id)?;
            }
            OpCode::StopSound => {
                let sound_id = self.read_u16(bytecode)?;
                ctx.stop_sound(sound_id)?;
            }

            OpCode::LoopSound => {
                let sound_id = self.read_u16(bytecode)?;
                ctx.loop_sound(sound_id)?;
            }
            OpCode::SetVolume => {
                let sound_id = self.read_u16(bytecode)?;
                let vol = self.pop()?;
                ctx.set_volume(sound_id, vol)?;
            }

            OpCode::GetObjVisible => { let id = self.read_u16(bytecode)?; let val = ctx.get_obj_visible(id)?; self.push(val); }
            OpCode::SetObjVisible => { let id = self.read_u16(bytecode)?; let val = self.pop()?; ctx.set_obj_visible(id, val)?; }
            OpCode::GetObjValue   => { let id = self.read_u16(bytecode)?; let val = ctx.get_obj_value(id)?; self.push(val); }
            OpCode::SetObjValue   => { let id = self.read_u16(bytecode)?; let val = self.pop()?; ctx.set_obj_value(id, val)?; }

            OpCode::GetSelfVisible => { let id = self.current_object.ok_or(VMError::NoSelfBound)?; let val = ctx.get_obj_visible(id)?; self.push(val); }
            OpCode::SetSelfVisible => { let id = self.current_object.ok_or(VMError::NoSelfBound)?; let val = self.pop()?; ctx.set_obj_visible(id, val)?; }
            OpCode::GetSelfValue   => { let id = self.current_object.ok_or(VMError::NoSelfBound)?; let val = ctx.get_obj_value(id)?; self.push(val); }
            OpCode::SetSelfValue   => { let id = self.current_object.ok_or(VMError::NoSelfBound)?; let val = self.pop()?; ctx.set_obj_value(id, val)?; }

            OpCode::GetObjCustom => { 
                let id = self.read_u16(bytecode)?; 
                let slot = self.read_u8(bytecode)?; 
                let val = ctx.get_obj_custom(id, slot)?; self.push(val); 
            }
            OpCode::SetObjCustom => { 
                let id = self.read_u16(bytecode)?; 
                let slot = self.read_u8(bytecode)?; 
                let val = self.pop()?; ctx.set_obj_custom(id, slot, val)?; 
            }
            OpCode::GetSelfCustom => { 
                let id = self.current_object.ok_or(VMError::NoSelfBound)?; 
                let slot = self.read_u8(bytecode)?; 
                let val = ctx.get_obj_custom(id, slot)?; self.push(val); 
            }
            OpCode::SetSelfCustom => { 
                let id = self.current_object.ok_or(VMError::NoSelfBound)?; 
                let slot = self.read_u8(bytecode)?; 
                let val = self.pop()?; ctx.set_obj_custom(id, slot, val)?; 
            }

            OpCode::GetObjRotation => { let id = self.read_u16(bytecode)?; let val = ctx.get_obj_rotation(id)?; self.push(val); }
            OpCode::SetObjRotation => { let id = self.read_u16(bytecode)?; let val = self.pop()?; ctx.set_obj_rotation(id, val)?; }
            OpCode::GetObjScale   => { let id = self.read_u16(bytecode)?; let val = ctx.get_obj_scale(id)?; self.push(val); }
            OpCode::SetObjScale   => { let id = self.read_u16(bytecode)?; let val = self.pop()?; ctx.set_obj_scale(id, val)?; }

            OpCode::GetSelfRotation => { let id = self.current_object.ok_or(VMError::NoSelfBound)?; let val = ctx.get_obj_rotation(id)?; self.push(val); }
            OpCode::SetSelfRotation => { let id = self.current_object.ok_or(VMError::NoSelfBound)?; let val = self.pop()?; ctx.set_obj_rotation(id, val)?; }
            OpCode::GetSelfScale   => { let id = self.current_object.ok_or(VMError::NoSelfBound)?; let val = ctx.get_obj_scale(id)?; self.push(val); }
            OpCode::SetSelfScale   => { let id = self.current_object.ok_or(VMError::NoSelfBound)?; let val = self.pop()?; ctx.set_obj_scale(id, val)?; }

            // ── Engine: Time ────────────────
            OpCode::PushDt => {
                self.push(ctx.get_dt());
            }
        }

        Ok(())
    }
}

// ═══════════════════════════════════════════════════
// TESTS
// ═══════════════════════════════════════════════════

#[cfg(test)]
mod tests {
    use super::*;
    use crate::opcodes::BytecodeBuilder;

    // ── Dummy engine for pure VM tests ──────

    struct DummyEngine {
        dt: f32,
        objects: Vec<(f32, f32)>, // (x, y) pairs
        keys: Vec<u16>,           // currently "held" key codes
    }

    impl DummyEngine {
        fn new() -> Self {
            Self {
                dt: 1.0 / 60.0,
                objects: Vec::new(),
                keys: Vec::new(),
            }
        }
    }

    impl EngineContext for DummyEngine {
        fn key_down(&self, key_code: u16) -> bool {
            self.keys.contains(&key_code)
        }

        fn get_obj_x(&self, obj_id: u16) -> Result<f32, VMError> {
            self.objects
                .get(obj_id as usize)
                .map(|o| o.0)
                .ok_or(VMError::ObjectOutOfBounds(obj_id))
        }

        fn get_obj_y(&self, obj_id: u16) -> Result<f32, VMError> {
            self.objects
                .get(obj_id as usize)
                .map(|o| o.1)
                .ok_or(VMError::ObjectOutOfBounds(obj_id))
        }

        fn set_obj_x(&mut self, obj_id: u16, val: f32) -> Result<(), VMError> {
            self.objects
                .get_mut(obj_id as usize)
                .map(|o| o.0 = val)
                .ok_or(VMError::ObjectOutOfBounds(obj_id))
        }

        fn set_obj_y(&mut self, obj_id: u16, val: f32) -> Result<(), VMError> {
            self.objects
                .get_mut(obj_id as usize)
                .map(|o| o.1 = val)
                .ok_or(VMError::ObjectOutOfBounds(obj_id))
        }

        fn play_anim(&mut self, _o: u16, _a: u16) -> Result<(), VMError> { Ok(()) }
        
        fn play_sound(&mut self, _id: u16) -> Result<(), VMError> { Ok(()) }
        fn stop_sound(&mut self, _id: u16) -> Result<(), VMError> { Ok(()) }

        fn loop_sound(&mut self, _id: u16) -> Result<(), VMError> { Ok(()) }
        fn set_volume(&mut self, _id: u16, _v: f32) -> Result<(), VMError> { Ok(()) }

        fn get_obj_visible(&self, obj_id: u16) -> Result<f32, VMError> { Ok(1.0) }
        fn set_obj_visible(&mut self, obj_id: u16, val: f32) -> Result<(), VMError> { Ok(()) }
        fn get_obj_value(&self, obj_id: u16) -> Result<f32, VMError> { Ok(1.0) }
        fn set_obj_value(&mut self, obj_id: u16, val: f32) -> Result<(), VMError> { Ok(()) }

        fn mouse_x(&self) -> f32 { 0.0 }
        fn mouse_y(&self) -> f32 { 0.0 }
        fn mouse_down(&self) -> bool { false }

        fn get_obj_custom(&self, obj_id: u16, slot: u8) -> Result<f32, VMError> { Ok(1.0) }
        fn set_obj_custom(&mut self, obj_id: u16, slot: u8, val: f32) -> Result<(), VMError> { Ok(()) }

        fn get_obj_rotation(&self, obj_id: u16) -> Result<f32, VMError> { Ok(1.0) }
        fn set_obj_rotation(&mut self, obj_id: u16, val: f32) -> Result<(), VMError> { Ok(()) }
        fn get_obj_scale(&self, obj_id: u16) -> Result<f32, VMError> { Ok(1.0) }
        fn set_obj_scale(&mut self, obj_id: u16, val: f32) -> Result<(), VMError> { Ok(()) }

        fn get_dt(&self) -> f32 {
            self.dt
        }
    }

    // ── Helper ──────────────────────────────

    fn run(builder: &BytecodeBuilder, vars: u16) -> VM {
        run_with_engine(builder, vars, &mut DummyEngine::new())
    }

    fn run_with_engine(builder: &BytecodeBuilder, vars: u16, ctx: &mut dyn EngineContext) -> VM {
        let mut vm = VM::new(vars);
        vm.run_frame(&builder.code, ctx).expect("VM error");
        vm
    }

    // ── Halt & Nop ──────────────────────────

    #[test]
    fn test_empty_program() {
        let b = BytecodeBuilder::new();
        let vm = run(&b, 0);
        assert!(vm.stack.is_empty());
    }

    #[test]
    fn test_halt() {
        let mut b = BytecodeBuilder::new();
        b.emit_f32(OpCode::PushF32, 42.0);
        b.emit(OpCode::Halt);
        b.emit_f32(OpCode::PushF32, 99.0); // should never execute
        let vm = run(&b, 0);
        assert_eq!(vm.stack.len(), 1);
        assert_eq!(vm.stack[0], 42.0);
    }

    #[test]
    fn test_nop() {
        let mut b = BytecodeBuilder::new();
        b.emit(OpCode::Nop);
        b.emit_f32(OpCode::PushF32, 7.0);
        b.emit(OpCode::Halt);
        let vm = run(&b, 0);
        assert_eq!(vm.stack[0], 7.0);
    }

    // ── Stack Operations ────────────────────

    #[test]
    fn test_push() {
        let mut b = BytecodeBuilder::new();
        b.emit_f32(OpCode::PushF32, 3.14);
        b.emit(OpCode::Halt);
        let vm = run(&b, 0);
        assert_eq!(vm.stack.len(), 1);
        assert!((vm.stack[0] - 3.14).abs() < 0.001);
    }

    #[test]
    fn test_push_multiple() {
        let mut b = BytecodeBuilder::new();
        b.emit_f32(OpCode::PushF32, 1.0);
        b.emit_f32(OpCode::PushF32, 2.0);
        b.emit_f32(OpCode::PushF32, 3.0);
        b.emit(OpCode::Halt);
        let vm = run(&b, 0);
        assert_eq!(vm.stack, vec![1.0, 2.0, 3.0]);
    }

    #[test]
    fn test_pop() {
        let mut b = BytecodeBuilder::new();
        b.emit_f32(OpCode::PushF32, 1.0);
        b.emit_f32(OpCode::PushF32, 2.0);
        b.emit(OpCode::Pop);
        b.emit(OpCode::Halt);
        let vm = run(&b, 0);
        assert_eq!(vm.stack, vec![1.0]);
    }

    #[test]
    fn test_dup() {
        let mut b = BytecodeBuilder::new();
        b.emit_f32(OpCode::PushF32, 5.0);
        b.emit(OpCode::Dup);
        b.emit(OpCode::Halt);
        let vm = run(&b, 0);
        assert_eq!(vm.stack, vec![5.0, 5.0]);
    }

    #[test]
    fn test_pop_empty_stack() {
        let mut b = BytecodeBuilder::new();
        b.emit(OpCode::Pop);
        let mut vm = VM::new(0);
        let result = vm.run_frame(&b.code, &mut DummyEngine::new());
        assert_eq!(result, Err(VMError::StackUnderflow));
    }

    // ── Arithmetic ──────────────────────────

    #[test]
    fn test_add() {
        let mut b = BytecodeBuilder::new();
        b.emit_f32(OpCode::PushF32, 3.0);
        b.emit_f32(OpCode::PushF32, 4.0);
        b.emit(OpCode::Add);
        b.emit(OpCode::Halt);
        let vm = run(&b, 0);
        assert_eq!(vm.stack[0], 7.0);
    }

    #[test]
    fn test_sub() {
        let mut b = BytecodeBuilder::new();
        b.emit_f32(OpCode::PushF32, 10.0);
        b.emit_f32(OpCode::PushF32, 3.0);
        b.emit(OpCode::Sub);
        b.emit(OpCode::Halt);
        let vm = run(&b, 0);
        assert_eq!(vm.stack[0], 7.0);
    }

    #[test]
    fn test_mul() {
        let mut b = BytecodeBuilder::new();
        b.emit_f32(OpCode::PushF32, 6.0);
        b.emit_f32(OpCode::PushF32, 7.0);
        b.emit(OpCode::Mul);
        b.emit(OpCode::Halt);
        let vm = run(&b, 0);
        assert_eq!(vm.stack[0], 42.0);
    }

    #[test]
    fn test_div() {
        let mut b = BytecodeBuilder::new();
        b.emit_f32(OpCode::PushF32, 20.0);
        b.emit_f32(OpCode::PushF32, 4.0);
        b.emit(OpCode::Div);
        b.emit(OpCode::Halt);
        let vm = run(&b, 0);
        assert_eq!(vm.stack[0], 5.0);
    }

    #[test]
    fn test_div_by_zero() {
        let mut b = BytecodeBuilder::new();
        b.emit_f32(OpCode::PushF32, 1.0);
        b.emit_f32(OpCode::PushF32, 0.0);
        b.emit(OpCode::Div);
        let mut vm = VM::new(0);
        let result = vm.run_frame(&b.code, &mut DummyEngine::new());
        assert_eq!(result, Err(VMError::DivisionByZero));
    }

    #[test]
    fn test_neg() {
        let mut b = BytecodeBuilder::new();
        b.emit_f32(OpCode::PushF32, 5.0);
        b.emit(OpCode::Neg);
        b.emit(OpCode::Halt);
        let vm = run(&b, 0);
        assert_eq!(vm.stack[0], -5.0);
    }

    #[test]
    fn test_neg_negative() {
        let mut b = BytecodeBuilder::new();
        b.emit_f32(OpCode::PushF32, -3.0);
        b.emit(OpCode::Neg);
        b.emit(OpCode::Halt);
        let vm = run(&b, 0);
        assert_eq!(vm.stack[0], 3.0);
    }

    #[test]
    fn test_chained_arithmetic() {
        // (2 + 3) * 4 = 20
        let mut b = BytecodeBuilder::new();
        b.emit_f32(OpCode::PushF32, 2.0);
        b.emit_f32(OpCode::PushF32, 3.0);
        b.emit(OpCode::Add);
        b.emit_f32(OpCode::PushF32, 4.0);
        b.emit(OpCode::Mul);
        b.emit(OpCode::Halt);
        let vm = run(&b, 0);
        assert_eq!(vm.stack[0], 20.0);
    }

    // ── Comparison ──────────────────────────

    #[test]
    fn test_gt_true() {
        let mut b = BytecodeBuilder::new();
        b.emit_f32(OpCode::PushF32, 5.0);
        b.emit_f32(OpCode::PushF32, 3.0);
        b.emit(OpCode::Gt);
        b.emit(OpCode::Halt);
        let vm = run(&b, 0);
        assert_eq!(vm.stack[0], 1.0);
    }

    #[test]
    fn test_gt_false() {
        let mut b = BytecodeBuilder::new();
        b.emit_f32(OpCode::PushF32, 2.0);
        b.emit_f32(OpCode::PushF32, 3.0);
        b.emit(OpCode::Gt);
        b.emit(OpCode::Halt);
        let vm = run(&b, 0);
        assert_eq!(vm.stack[0], 0.0);
    }

    #[test]
    fn test_lt_true() {
        let mut b = BytecodeBuilder::new();
        b.emit_f32(OpCode::PushF32, 2.0);
        b.emit_f32(OpCode::PushF32, 5.0);
        b.emit(OpCode::Lt);
        b.emit(OpCode::Halt);
        let vm = run(&b, 0);
        assert_eq!(vm.stack[0], 1.0);
    }

    #[test]
    fn test_eq_true() {
        let mut b = BytecodeBuilder::new();
        b.emit_f32(OpCode::PushF32, 7.0);
        b.emit_f32(OpCode::PushF32, 7.0);
        b.emit(OpCode::Eq);
        b.emit(OpCode::Halt);
        let vm = run(&b, 0);
        assert_eq!(vm.stack[0], 1.0);
    }

    #[test]
    fn test_eq_false() {
        let mut b = BytecodeBuilder::new();
        b.emit_f32(OpCode::PushF32, 7.0);
        b.emit_f32(OpCode::PushF32, 8.0);
        b.emit(OpCode::Eq);
        b.emit(OpCode::Halt);
        let vm = run(&b, 0);
        assert_eq!(vm.stack[0], 0.0);
    }

    #[test]
    fn test_not_zero() {
        let mut b = BytecodeBuilder::new();
        b.emit_f32(OpCode::PushF32, 0.0);
        b.emit(OpCode::Not);
        b.emit(OpCode::Halt);
        let vm = run(&b, 0);
        assert_eq!(vm.stack[0], 1.0);
    }

    #[test]
    fn test_not_nonzero() {
        let mut b = BytecodeBuilder::new();
        b.emit_f32(OpCode::PushF32, 42.0);
        b.emit(OpCode::Not);
        b.emit(OpCode::Halt);
        let vm = run(&b, 0);
        assert_eq!(vm.stack[0], 0.0);
    }

    // ── Variables ───────────────────────────

    #[test]
    fn test_store_and_load() {
        let mut b = BytecodeBuilder::new();
        b.emit_f32(OpCode::PushF32, 99.0);
        b.emit_u16(OpCode::Store, 0);   // vars[0] = 99.0
        b.emit_u16(OpCode::Load, 0);    // push vars[0]
        b.emit(OpCode::Halt);
        let vm = run(&b, 4);
        assert_eq!(vm.stack[0], 99.0);
        assert_eq!(vm.variables[0], 99.0);
    }

    #[test]
    fn test_multiple_variables() {
        let mut b = BytecodeBuilder::new();
        b.emit_f32(OpCode::PushF32, 10.0);
        b.emit_u16(OpCode::Store, 0);
        b.emit_f32(OpCode::PushF32, 20.0);
        b.emit_u16(OpCode::Store, 1);
        b.emit_u16(OpCode::Load, 0);    // push 10
        b.emit_u16(OpCode::Load, 1);    // push 20
        b.emit(OpCode::Add);            // 30
        b.emit(OpCode::Halt);
        let vm = run(&b, 4);
        assert_eq!(vm.stack[0], 30.0);
    }

    #[test]
    fn test_variable_out_of_bounds() {
        let mut b = BytecodeBuilder::new();
        b.emit_u16(OpCode::Load, 10); // only 2 vars allocated
        let mut vm = VM::new(2);
        let result = vm.run_frame(&b.code, &mut DummyEngine::new());
        assert_eq!(result, Err(VMError::VariableOutOfBounds(10)));
    }

    #[test]
    fn test_variables_persist_across_frames() {
        let mut b1 = BytecodeBuilder::new();
        b1.emit_f32(OpCode::PushF32, 42.0);
        b1.emit_u16(OpCode::Store, 0);
        b1.emit(OpCode::Halt);

        let mut b2 = BytecodeBuilder::new();
        b2.emit_u16(OpCode::Load, 0);
        b2.emit(OpCode::Halt);

        let mut vm = VM::new(4);
        let mut ctx = DummyEngine::new();

        // Frame 1: store 42 into var 0
        vm.run_frame(&b1.code, &mut ctx).unwrap();
        assert_eq!(vm.variables[0], 42.0);

        // Frame 2: load var 0 — should still be 42
        vm.run_frame(&b2.code, &mut ctx).unwrap();
        assert_eq!(vm.stack[0], 42.0);
    }

    #[test]
    fn test_stack_clears_between_frames() {
        let mut b = BytecodeBuilder::new();
        b.emit_f32(OpCode::PushF32, 1.0);
        b.emit_f32(OpCode::PushF32, 2.0);
        b.emit(OpCode::Halt);

        let mut vm = VM::new(0);
        let mut ctx = DummyEngine::new();

        vm.run_frame(&b.code, &mut ctx).unwrap();
        assert_eq!(vm.stack.len(), 2);

        vm.run_frame(&b.code, &mut ctx).unwrap();
        assert_eq!(vm.stack.len(), 2); // not 4
    }

    // ── Flow Control ────────────────────────

    #[test]
    fn test_jump() {
        // jump over a push, land on another push
        let mut b = BytecodeBuilder::new();
        let jmp = b.emit_u16(OpCode::Jump, 0); // placeholder
        b.emit_f32(OpCode::PushF32, 999.0);    // skipped
        let target = b.pos();
        b.emit_f32(OpCode::PushF32, 1.0);      // lands here
        b.emit(OpCode::Halt);
        b.patch_u32(jmp, target);

        let vm = run(&b, 0);
        assert_eq!(vm.stack, vec![1.0]);
    }

    #[test]
    fn test_jump_if_taken() {
        let mut b = BytecodeBuilder::new();
        b.emit_f32(OpCode::PushF32, 1.0);       // truthy
        let jmp = b.emit_u16(OpCode::JumpIf, 0); // placeholder
        b.emit_f32(OpCode::PushF32, 999.0);      // skipped
        b.emit(OpCode::Halt);
        let target = b.pos();
        b.emit_f32(OpCode::PushF32, 42.0);       // lands here
        b.emit(OpCode::Halt);
        b.patch_u32(jmp, target);

        let vm = run(&b, 0);
        assert_eq!(vm.stack, vec![42.0]);
    }

    #[test]
    fn test_jump_if_not_taken() {
        let mut b = BytecodeBuilder::new();
        b.emit_f32(OpCode::PushF32, 0.0);        // falsy
        let jmp = b.emit_u32(OpCode::JumpIf32, 0);
        b.emit_f32(OpCode::PushF32, 42.0);       // not skipped
        b.emit(OpCode::Halt);
        let target = b.pos();
        b.emit_f32(OpCode::PushF32, 999.0);      // never reached
        b.emit(OpCode::Halt);
        b.patch_u32(jmp, target);

        let vm = run(&b, 0);
        assert_eq!(vm.stack, vec![42.0]);
    }

    #[test]
    fn test_jump_if_not_taken_branch() {
        let mut b = BytecodeBuilder::new();
        b.emit_f32(OpCode::PushF32, 0.0);           // falsy
        let jmp = b.emit_u16(OpCode::JumpIfNot, 0);
        b.emit_f32(OpCode::PushF32, 999.0);          // skipped
        b.emit(OpCode::Halt);
        let target = b.pos();
        b.emit_f32(OpCode::PushF32, 42.0);
        b.emit(OpCode::Halt);
        b.patch_u32(jmp, target);

        let vm = run(&b, 0);
        assert_eq!(vm.stack, vec![42.0]);
    }

    #[test]
    fn test_jump_if_not_falls_through() {
        let mut b = BytecodeBuilder::new();
        b.emit_f32(OpCode::PushF32, 1.0);            // truthy
        let jmp = b.emit_u32(OpCode::JumpIfNot32, 0);
        b.emit_f32(OpCode::PushF32, 42.0);            // not skipped
        b.emit(OpCode::Halt);
        let target = b.pos();
        b.emit_f32(OpCode::PushF32, 999.0);
        b.emit(OpCode::Halt);
        b.patch_u32(jmp, target);

        let vm = run(&b, 0);
        assert_eq!(vm.stack, vec![42.0]);
    }

    // ── Loop test ───────────────────────────

    #[test]
    fn test_counting_loop() {
        // var[0] = counter, count from 0 to 5
        //
        // Pseudocode:
        //   var[0] = 0
        // loop:
        //   if var[0] >= 5 → jump to end
        //   var[0] = var[0] + 1
        //   jump loop
        // end:
        //   halt

        let mut b = BytecodeBuilder::new();

        // var[0] = 0
        b.emit_f32(OpCode::PushF32, 0.0);
        b.emit_u16(OpCode::Store, 0);

        // loop: (record position)
        let loop_start = b.pos();

        // push var[0], push 5, compare: var[0] < 5?
        b.emit_u16(OpCode::Load, 0);
        b.emit_f32(OpCode::PushF32, 5.0);
        b.emit(OpCode::Lt);
        // if not less than 5, jump to end
        let exit_jmp = b.emit_u32(OpCode::JumpIfNot32, 0);  // placeholder

        // var[0] = var[0] + 1
        b.emit_u16(OpCode::Load, 0);
        b.emit_f32(OpCode::PushF32, 1.0);
        b.emit(OpCode::Add);
        b.emit_u16(OpCode::Store, 0);

        // jump back to loop
        b.emit_u32(OpCode::Jump32, loop_start);

        // end:
        let end = b.pos();
        b.emit(OpCode::Halt);

        b.patch_u32(exit_jmp, end);

        let vm = run(&b, 4);
        assert_eq!(vm.variables[0], 5.0);
    }

    // ── Engine Context tests ────────────────

    #[test]
    fn test_push_dt() {
        let mut b = BytecodeBuilder::new();
        b.emit(OpCode::PushDt);
        b.emit(OpCode::Halt);

        let mut ctx = DummyEngine::new();
        ctx.dt = 0.016;
        let vm = run_with_engine(&b, 0, &mut ctx);
        assert!((vm.stack[0] - 0.016).abs() < 0.0001);
    }

    #[test]
    fn test_object_get_set() {
        let mut b = BytecodeBuilder::new();
        // get obj 0 x, add 10, set obj 0 x
        b.emit_u16(OpCode::GetObjX, 0);
        b.emit_f32(OpCode::PushF32, 10.0);
        b.emit(OpCode::Add);
        b.emit_u16(OpCode::SetObjX, 0);
        b.emit(OpCode::Halt);

        let mut ctx = DummyEngine::new();
        ctx.objects.push((100.0, 200.0));

        run_with_engine(&b, 0, &mut ctx);
        assert_eq!(ctx.objects[0].0, 110.0);
        assert_eq!(ctx.objects[0].1, 200.0);
    }

    #[test]
    fn test_key_down() {
        let mut b = BytecodeBuilder::new();
        b.emit_u16(OpCode::KeyDown, 39); // right arrow
        b.emit(OpCode::Halt);

        // Key not pressed
        let mut ctx = DummyEngine::new();
        let vm = run_with_engine(&b, 0, &mut ctx);
        assert_eq!(vm.stack[0], 0.0);

        // Key pressed
        let mut ctx2 = DummyEngine::new();
        ctx2.keys.push(39);
        let vm2 = run_with_engine(&b, 0, &mut ctx2);
        assert_eq!(vm2.stack[0], 1.0);
    }

    #[test]
    fn test_object_out_of_bounds() {
        let mut b = BytecodeBuilder::new();
        b.emit_u16(OpCode::GetObjX, 5);
        let mut vm = VM::new(0);
        let mut ctx = DummyEngine::new();
        let result = vm.run_frame(&b.code, &mut ctx);
        assert_eq!(result, Err(VMError::ObjectOutOfBounds(5)));
    }

    // ── Invalid opcode test ─────────────────

    #[test]
    fn test_invalid_opcode() {
        let code = vec![0xFF]; // not a valid opcode
        let mut vm = VM::new(0);
        let mut ctx = DummyEngine::new();
        let result = vm.run_frame(&code, &mut ctx);
        assert_eq!(result, Err(VMError::InvalidOpcode(0xFF)));
    }

    // ── Complex program ─────────────────────

    #[test]
    fn test_conditional_movement_pattern() {
        // Simulates: if key_right then obj[0].x += 200 * dt
        //
        // This is the exact pattern used for the circle-move test

        let mut b = BytecodeBuilder::new();

        // Check right arrow (keycode 39)
        b.emit_u16(OpCode::KeyDown, 39);
        let skip = b.emit_u32(OpCode::JumpIfNot32, 0); // skip if not pressed

        // obj[0].x = obj[0].x + 200.0 * dt
        b.emit_u16(OpCode::GetObjX, 0);   // push obj.x
        b.emit_f32(OpCode::PushF32, 200.0); // push speed
        b.emit(OpCode::PushDt);             // push dt
        b.emit(OpCode::Mul);                // speed * dt
        b.emit(OpCode::Add);                // obj.x + speed*dt
        b.emit_u16(OpCode::SetObjX, 0);    // write back

        let end = b.pos();
        b.emit(OpCode::Halt);
        b.patch_u32(skip, end);

        // Test without key pressed — x should not change
        let mut ctx = DummyEngine::new();
        ctx.dt = 1.0 / 60.0;
        ctx.objects.push((400.0, 300.0));

        run_with_engine(&b, 0, &mut ctx);
        assert_eq!(ctx.objects[0].0, 400.0); // unchanged

        // Test with key pressed — x should increase
        let mut ctx2 = DummyEngine::new();
        ctx2.dt = 1.0 / 60.0;
        ctx2.objects.push((400.0, 300.0));
        ctx2.keys.push(39);

        run_with_engine(&b, 0, &mut ctx2);
        let expected = 400.0 + 200.0 * (1.0 / 60.0);
        assert!((ctx2.objects[0].0 - expected).abs() < 0.001);
    }

    #[test]
    fn test_self_opcodes() {
        let mut b = BytecodeBuilder::new();
        // self.x = self.x + 50
        b.emit(OpCode::GetSelfX);
        b.emit_f32(OpCode::PushF32, 50.0);
        b.emit(OpCode::Add);
        b.emit(OpCode::SetSelfX);
        b.emit(OpCode::Halt);

        let mut ctx = DummyEngine::new();
        ctx.objects.push((100.0, 200.0)); // obj[0]

        let mut vm = VM::new(0);
        
        // Try to run without binding self (should fail)
        assert_eq!(vm.run_frame(&b.code, &mut ctx), Err(VMError::NoSelfBound));

        // Bind self to obj 0 and run
        vm.current_object = Some(0);
        vm.run_frame(&b.code, &mut ctx).unwrap();

        assert_eq!(ctx.objects[0].0, 150.0);
    }
}