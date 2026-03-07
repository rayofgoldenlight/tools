use std::collections::HashMap;

use crate::ast::*;
use crate::key_name_to_code;
use crate::parser::{parse_source, ParseError};
use format::opcodes::{BytecodeBuilder, OpCode};

#[derive(Debug, Clone, PartialEq)]
pub struct CompileError {
    pub message: String,
    pub span: crate::token::Span,
}

impl std::fmt::Display for CompileError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "[{}] Compile error: {}", self.span, self.message)
    }
}

impl From<ParseError> for CompileError {
    fn from(e: ParseError) -> Self {
        CompileError { message: e.message, span: e.span }
    }
}

#[derive(Debug, Clone)]
pub struct CompiledProgram {
    pub bytecode: Vec<u8>,
    pub behaviors: Vec<(u16, Vec<u8>)>,
    pub variable_count: u16,
}

pub fn compile_source(source: &str) -> Result<CompiledProgram, CompileError> {
    let program = parse_source(source)?;
    Compiler::new().compile(program)
}

struct Compiler {
    vars: HashMap<String, u16>,
    var_inits: Vec<(u16, Expr)>, // (var_id, init_expr)
    next_var: u16,
}

impl Compiler {
    fn new() -> Self {
        Self {
            vars: HashMap::new(),
            var_inits: Vec::new(),
            next_var: 0,
        }
    }

    fn alloc_var(&mut self, name: &str, span: crate::token::Span) -> Result<u16, CompileError> {
        if self.vars.contains_key(name) {
            return Err(CompileError {
                message: format!("Variable '{}' already declared", name),
                span,
            });
        }
        let id = self.next_var;
        self.next_var += 1;
        self.vars.insert(name.to_string(), id);
        Ok(id)
    }

    fn var_id(&self, name: &str, span: crate::token::Span) -> Result<u16, CompileError> {
        self.vars.get(name).copied().ok_or(CompileError {
            message: format!("Unknown variable '{}'", name),
            span,
        })
    }

    fn compile(mut self, program: Program) -> Result<CompiledProgram, CompileError> {
        let mut update_fn: Option<Vec<Statement>> = None;
        let mut behaviors: Vec<(u16, Vec<Statement>)> = Vec::new();

        for decl in program.declarations {
            match decl {
                Declaration::VarDecl { name, init, span } => {
                    let id = self.alloc_var(&name, span)?;
                    self.var_inits.push((id, init));
                }
                Declaration::FunctionDecl { name, params, body, span } => {
                    if name != "update" {
                        return Err(CompileError { message: "Only function update(dt) is supported".into(), span });
                    }
                    if update_fn.is_some() {
                        return Err(CompileError { message: "Duplicate function 'update'".into(), span });
                    }
                    if params.len() != 1 || params[0] != "dt" {
                        return Err(CompileError { message: "update must be declared as: function update(dt)".into(), span });
                    }
                    update_fn = Some(body);
                }
                Declaration::BehaviorDecl { shape_index, body, span: _ } => {
                    behaviors.push((shape_index, body));
                }
            }
        }

        // 1) Emit global bytecode (Init + Update)
        let mut b = BytecodeBuilder::new();
        let has_inits = !self.var_inits.is_empty();
        let init_flag_id = if has_inits {
            Some(self.alloc_var("__init", crate::token::Span::new(1, 1))?)
        } else {
            None
        };

        if let Some(flag_id) = init_flag_id {
            b.emit_u16(OpCode::Load, flag_id);
            b.emit_f32(OpCode::PushF32, 0.0);
            b.emit(OpCode::Eq);
            let skip_init = b.emit_u32(OpCode::JumpIfNot32, 0);

            for (var_id, init_expr) in &self.var_inits {
                self.emit_expr(&mut b, init_expr)?;
                b.emit_u16(OpCode::Store, *var_id);
            }

            b.emit_f32(OpCode::PushF32, 1.0);
            b.emit_u16(OpCode::Store, flag_id);
            let after_init = b.pos();
            b.patch_u32(skip_init, after_init);
        }

        if let Some(body) = update_fn {
            for stmt in &body {
                self.emit_stmt(&mut b, stmt)?;
            }
        }
        b.emit(OpCode::Halt);

        // 2) Emit behavior bytecodes
        let mut compiled_behaviors = Vec::new();
        for (shape_index, body) in behaviors {
            let mut b_beh = BytecodeBuilder::new();
            for stmt in &body {
                self.emit_stmt(&mut b_beh, stmt)?;
            }
            b_beh.emit(OpCode::Halt);
            compiled_behaviors.push((shape_index, b_beh.finish()));
        }

        Ok(CompiledProgram {
            bytecode: b.finish(),
            behaviors: compiled_behaviors,
            variable_count: self.next_var,
        })
    }

    // ─────────────────────────────────────────────
    // Statement emission
    // ─────────────────────────────────────────────

    fn emit_stmt(&self, b: &mut BytecodeBuilder, stmt: &Statement) -> Result<(), CompileError> {
        match stmt {
            Statement::Assignment { target, value, span: _ } => {
                self.emit_assignment(b, target, value)?;
            }
            Statement::ExprStatement { expr, .. } => {
                self.emit_expr(b, expr)?;
                b.emit(OpCode::Pop); // discard result
            }
            Statement::If { condition, then_body, elseif_clauses, else_body, .. } => {
                self.emit_if(b, condition, then_body, elseif_clauses, else_body)?;
            }
            Statement::While { condition, body, .. } => {
                self.emit_while(b, condition, body)?;
            }
            Statement::Return { value, .. } => {
                if let Some(v) = value {
                    self.emit_expr(b, v)?;
                    b.emit(OpCode::Pop);
                }
                b.emit(OpCode::Halt);
            }
        }
        Ok(())
    }

    fn emit_assignment(
        &self,
        b: &mut BytecodeBuilder,
        target: &AssignTarget,
        value: &Expr,
    ) -> Result<(), CompileError> {
        match target {
            AssignTarget::Variable(name) => {
                let var_id = self.var_id(name, value.span())?;
                self.emit_expr(b, value)?;
                b.emit_u16(OpCode::Store, var_id);
                Ok(())
            }
            AssignTarget::ObjField { obj_index, field } => {
                let obj_id = self.const_obj_index(obj_index)?;
                self.emit_expr(b, value)?;
                match field {
                    ObjFieldKind::X => { b.emit_u16(OpCode::SetObjX, obj_id); }
                    ObjFieldKind::Y => { b.emit_u16(OpCode::SetObjY, obj_id); }
                    ObjFieldKind::Visible => { b.emit_u16(OpCode::SetObjVisible, obj_id); }
                    ObjFieldKind::Value => { b.emit_u16(OpCode::SetObjValue, obj_id); }
                    ObjFieldKind::Rotation => { b.emit_u16(OpCode::SetObjRotation, obj_id); }
                    ObjFieldKind::Scale => { b.emit_u16(OpCode::SetObjScale, obj_id); }
                    ObjFieldKind::Val1 => { b.emit_u16(OpCode::SetObjCustom, obj_id); b.code.push(0); }
                    ObjFieldKind::Val2 => { b.emit_u16(OpCode::SetObjCustom, obj_id); b.code.push(1); }
                    ObjFieldKind::Val3 => { b.emit_u16(OpCode::SetObjCustom, obj_id); b.code.push(2); }
                    ObjFieldKind::Val4 => { b.emit_u16(OpCode::SetObjCustom, obj_id); b.code.push(3); }
                };
                Ok(())
            }
            AssignTarget::SelfField(field) => {
                self.emit_expr(b, value)?;
                match field {
                    ObjFieldKind::X => { b.emit(OpCode::SetSelfX); }
                    ObjFieldKind::Y => { b.emit(OpCode::SetSelfY); }
                    ObjFieldKind::Visible => { b.emit(OpCode::SetSelfVisible); }
                    ObjFieldKind::Value => { b.emit(OpCode::SetSelfValue); }
                    ObjFieldKind::Rotation => { b.emit(OpCode::SetSelfRotation); }
                    ObjFieldKind::Scale => { b.emit(OpCode::SetSelfScale); }
                    ObjFieldKind::Val1 => { b.emit(OpCode::SetSelfCustom); b.code.push(0); }
                    ObjFieldKind::Val2 => { b.emit(OpCode::SetSelfCustom); b.code.push(1); }
                    ObjFieldKind::Val3 => { b.emit(OpCode::SetSelfCustom); b.code.push(2); }
                    ObjFieldKind::Val4 => { b.emit(OpCode::SetSelfCustom); b.code.push(3); }
                };
                Ok(())
            }
        }
    }

    fn emit_if(
        &self,
        b: &mut BytecodeBuilder,
        condition: &Expr,
        then_body: &[Statement],
        elseif_clauses: &[ElseIfClause],
        else_body: &[Statement],
    ) -> Result<(), CompileError> {
        // if cond then ... [elseif ...] [else ...] end
        self.emit_expr(b, condition)?;
        let jmp_to_next = b.emit_u32(OpCode::JumpIfNot32, 0); 

        for stmt in then_body { self.emit_stmt(b, stmt)?; }

        let has_tail = !elseif_clauses.is_empty() || !else_body.is_empty();
        let jmp_to_end = if has_tail {
            Some(b.emit_u32(OpCode::Jump32, 0))
        } else { None };

        let after_then = b.pos();
        b.patch_u32(jmp_to_next, after_then);

        let mut end_jumps: Vec<usize> = Vec::new();
        if let Some(j) = jmp_to_end { end_jumps.push(j); }

        for clause in elseif_clauses {
            self.emit_expr(b, &clause.condition)?;
            let jmp_next_elseif = b.emit_u32(OpCode::JumpIfNot32, 0); 

            for stmt in &clause.body { self.emit_stmt(b, stmt)?; }

            end_jumps.push(b.emit_u32(OpCode::Jump32, 0));

            let after_clause = b.pos();
            b.patch_u32(jmp_next_elseif, after_clause);
        }

        // else body
        for stmt in else_body {
            self.emit_stmt(b, stmt)?;
        }

        // patch all end jumps
        let end = b.pos();
        for j in end_jumps {
            b.patch_u32(j, end);
        }

        Ok(())
    }

    fn emit_while(
        &self,
        b: &mut BytecodeBuilder,
        condition: &Expr,
        body: &[Statement],
    ) -> Result<(), CompileError> {
        let loop_start = b.pos();

        self.emit_expr(b, condition)?;
        let exit_jump = b.emit_u32(OpCode::JumpIfNot32, 0);

        for stmt in body {
            self.emit_stmt(b, stmt)?;
        }

        b.emit_u32(OpCode::Jump32, loop_start);

        let loop_end = b.pos();
        b.patch_u32(exit_jump, loop_end);

        Ok(())
    }

    // ─────────────────────────────────────────────
    // Expression emission
    // ─────────────────────────────────────────────

    fn emit_expr(&self, b: &mut BytecodeBuilder, expr: &Expr) -> Result<(), CompileError> {
        match expr {
            Expr::Number { value, .. } => {
                b.emit_f32(OpCode::PushF32, *value);
            }
            Expr::Bool { value, .. } => {
                b.emit_f32(OpCode::PushF32, if *value { 1.0 } else { 0.0 });
            }
            Expr::StringLit { .. } => {
                return Err(CompileError {
                    message: "String literals are only allowed as arguments to built-in calls (e.g. key_down(\"right\"))".into(),
                    span: expr.span(),
                });
            }
            Expr::Ident { name, span } => {
                if name == "dt" {
                    b.emit(OpCode::PushDt);
                } else if name == "mouse_x" {
                    b.emit(OpCode::MouseX);
                } else if name == "mouse_y" {
                    b.emit(OpCode::MouseY);
                } else {
                    let id = self.var_id(name, *span)?;
                    b.emit_u16(OpCode::Load, id);
                }
            }
            Expr::UnaryOp { op, operand, span } => {
                self.emit_expr(b, operand)?;
                match op {
                    UnOp::Neg => b.emit(OpCode::Neg),
                    UnOp::Not => b.emit(OpCode::Not),
                };
                // span currently unused in bytecode emission
                let _ = span;
            }
            Expr::BinaryOp { op, left, right, .. } => {
                self.emit_expr(b, left)?;
                self.emit_expr(b, right)?;
                self.emit_binop(b, *op, expr.span())?;
            }
            Expr::Call { name, args, span } => {
                self.emit_call(b, name, args, *span)?;
            }
            Expr::SelfField { field, span: _ } => {
                match field {
                    ObjFieldKind::X => { b.emit(OpCode::GetSelfX); }
                    ObjFieldKind::Y => { b.emit(OpCode::GetSelfY); }
                    ObjFieldKind::Visible => { b.emit(OpCode::GetSelfVisible); }
                    ObjFieldKind::Value => { b.emit(OpCode::GetSelfValue); }
                    ObjFieldKind::Rotation => { b.emit(OpCode::SetSelfRotation); }
                    ObjFieldKind::Scale => { b.emit(OpCode::SetSelfScale); }
                    ObjFieldKind::Val1 => { b.emit(OpCode::GetSelfCustom); b.code.push(0); }
                    ObjFieldKind::Val2 => { b.emit(OpCode::GetSelfCustom); b.code.push(1); }
                    ObjFieldKind::Val3 => { b.emit(OpCode::GetSelfCustom); b.code.push(2); }
                    ObjFieldKind::Val4 => { b.emit(OpCode::GetSelfCustom); b.code.push(3); }
                };
            }
            Expr::ObjField { obj_index, field, span: _ } => {
                let obj_id = self.const_obj_index(obj_index)?;
                match field {
                    ObjFieldKind::X => { b.emit_u16(OpCode::GetObjX, obj_id); }
                    ObjFieldKind::Y => { b.emit_u16(OpCode::GetObjY, obj_id); }
                    ObjFieldKind::Visible => { b.emit_u16(OpCode::GetObjVisible, obj_id); }
                    ObjFieldKind::Value => { b.emit_u16(OpCode::GetObjValue, obj_id); }
                    ObjFieldKind::Rotation => { b.emit_u16(OpCode::GetObjRotation, obj_id); }
                    ObjFieldKind::Scale => { b.emit_u16(OpCode::GetObjScale, obj_id); }
                    ObjFieldKind::Val1 => { b.emit_u16(OpCode::GetObjCustom, obj_id); b.code.push(0); }
                    ObjFieldKind::Val2 => { b.emit_u16(OpCode::GetObjCustom, obj_id); b.code.push(1); }
                    ObjFieldKind::Val3 => { b.emit_u16(OpCode::GetObjCustom, obj_id); b.code.push(2); }
                    ObjFieldKind::Val4 => { b.emit_u16(OpCode::GetObjCustom, obj_id); b.code.push(3); }
                };
            }
        }
        Ok(())
    }

    fn emit_call(
        &self,
        b: &mut BytecodeBuilder,
        name: &str,
        args: &[Expr],
        span: crate::token::Span,
    ) -> Result<(), CompileError> {
        match name {
            "key_down" => {
                if args.len() != 1 {
                    return Err(CompileError {
                        message: "key_down expects exactly 1 argument: key_down(\"right\")".into(),
                        span,
                    });
                }
                let key_name = match &args[0] {
                    Expr::StringLit { value, .. } => value.as_str(),
                    _ => {
                        return Err(CompileError {
                            message: "key_down argument must be a string literal, e.g. key_down(\"right\")".into(),
                            span: args[0].span(),
                        })
                    }
                };
                let code = key_name_to_code(key_name).ok_or(CompileError {
                    message: format!("Unknown key name '{}'", key_name),
                    span: args[0].span(),
                })?;
                b.emit_u16(OpCode::KeyDown, code);
                Ok(())
            }
            "play_anim" => {
                if args.len() == 1 {
                    // Legacy/Behavior mode: play_anim(anim_id) -> targets 'self'
                    self.emit_expr(b, &args[0])?;
                    b.emit(OpCode::PlayAnim);
                    b.emit_f32(OpCode::PushF32, 0.0);
                    Ok(())
                } else if args.len() == 2 {
                    // Global mode: play_anim(obj_id, anim_id)
                    self.emit_expr(b, &args[0])?; // push obj_id to stack
                    self.emit_expr(b, &args[1])?; // push anim_id to stack
                    b.emit(OpCode::PlayObjAnim);
                    b.emit_f32(OpCode::PushF32, 0.0); // dummy return
                    Ok(())
                } else {
                    return Err(CompileError { 
                        message: "play_anim expects 1 or 2 arguments: play_anim([obj_index], anim_index)".into(), 
                        span 
                    });
                }
            }
            "play_sound" => {
                if args.len() != 1 {
                    return Err(CompileError {
                        message: "play_sound expects exactly 1 argument: play_sound(0)".into(),
                        span,
                    });
                }
                
                // For now, audio IDs must be constant numbers
                let sound_id = match &args[0] {
                    Expr::Number { value, .. } => {
                        if *value < 0.0 || value.fract() != 0.0 {
                            return Err(CompileError {
                                message: "Sound ID must be a non-negative integer (e.g. play_sound(0))".into(),
                                span: args[0].span(),
                            });
                        }
                        if *value > u16::MAX as f32 {
                            return Err(CompileError {
                                message: "Sound ID is too large".into(),
                                span: args[0].span(),
                            });
                        }
                        *value as u16
                    }
                    _ => {
                        return Err(CompileError {
                            message: "Sound ID must be a constant number for now".into(),
                            span: args[0].span(),
                        })
                    }
                };

                b.emit_u16(OpCode::PlaySound, sound_id);
                
                // Push a dummy return value so the ExprStatement's OpCode::Pop doesn't underflow the stack
                b.emit_f32(OpCode::PushF32, 0.0);
                Ok(())
            }
            "stop_sound" => {
                if args.len() != 1 {
                    return Err(CompileError { message: "stop_sound expects exactly 1 argument: stop_sound(0)".into(), span });
                }
                let sound_id = match &args[0] {
                    Expr::Number { value, .. } => *value as u16,
                    _ => return Err(CompileError { message: "Sound ID must be a constant number".into(), span: args[0].span() })
                };
                b.emit_u16(OpCode::StopSound, sound_id);
                b.emit_f32(OpCode::PushF32, 0.0); // dummy return
                Ok(())
            }
            "loop_sound" => {
                if args.len() != 1 { return Err(CompileError { message: "loop_sound expects 1 arg".into(), span }); }
                let sound_id = match &args[0] {
                    Expr::Number { value, .. } => *value as u16,
                    _ => return Err(CompileError { message: "ID must be constant".into(), span: args[0].span() })
                };
                b.emit_u16(OpCode::LoopSound, sound_id);
                b.emit_f32(OpCode::PushF32, 0.0); // dummy return
                Ok(())
            }
            "set_volume" => {
                if args.len() != 2 { return Err(CompileError { message: "set_volume(id, vol) expects 2 args".into(), span }); }
                let sound_id = match &args[0] {
                    Expr::Number { value, .. } => *value as u16,
                    _ => return Err(CompileError { message: "ID must be constant".into(), span: args[0].span() })
                };
                self.emit_expr(b, &args[1])?; // Evaluate volume expr and push to stack
                b.emit_u16(OpCode::SetVolume, sound_id); // Consumes the volume
                b.emit_f32(OpCode::PushF32, 0.0); // dummy return
                Ok(())
            }
            "mouse_down" => {
                b.emit(OpCode::MouseDown);
                Ok(())
            }
            "abs" => {
                if args.len() != 1 { return Err(CompileError { message: "abs expects 1 argument".into(), span }); }
                self.emit_expr(b, &args[0])?;
                b.emit(OpCode::Abs);
                Ok(())
            }
            "sign" => {
                if args.len() != 1 { return Err(CompileError { message: "sign expects 1 argument".into(), span }); }
                self.emit_expr(b, &args[0])?;
                b.emit(OpCode::Sign);
                Ok(())
            }
            _ => Err(CompileError {
                message: format!("Unknown function '{}'", name),
                span,
            }),
        }
    }

    fn emit_binop(
        &self,
        b: &mut BytecodeBuilder,
        op: BinOp,
        span: crate::token::Span,
    ) -> Result<(), CompileError> {
        match op {
            BinOp::Add => {
                b.emit(OpCode::Add);
            }
            BinOp::Sub => {
                b.emit(OpCode::Sub);
            }
            BinOp::Mul => {
                b.emit(OpCode::Mul);
            }
            BinOp::Div => {
                b.emit(OpCode::Div);
            }

            BinOp::Less => {
                b.emit(OpCode::Lt);
            }
            BinOp::Greater => {
                b.emit(OpCode::Gt);
            }
            BinOp::Eq => {
                b.emit(OpCode::Eq);
            }

            BinOp::NotEq => {
                b.emit(OpCode::Eq);
                b.emit(OpCode::Not);
            }
            BinOp::LessEq => {
                // a <= b  <=>  !(a > b)
                b.emit(OpCode::Gt);
                b.emit(OpCode::Not);
            }
            BinOp::GreaterEq => {
                // a >= b  <=>  !(a < b)
                b.emit(OpCode::Lt);
                b.emit(OpCode::Not);
            }

            BinOp::And => {
                // normalize(a) * normalize(b)
                // normalize(x) := !!x  (Not; Not)
                b.emit(OpCode::Not);
                b.emit(OpCode::Not);
                b.emit(OpCode::Not);
                b.emit(OpCode::Not);
                b.emit(OpCode::Mul);
            }
            BinOp::Or => {
                // normalize(a) + normalize(b) > 0
                b.emit(OpCode::Not);
                b.emit(OpCode::Not);
                b.emit(OpCode::Not);
                b.emit(OpCode::Not);
                b.emit(OpCode::Add);
                b.emit_f32(OpCode::PushF32, 0.0);
                b.emit(OpCode::Gt);
            }
        }

        let _ = span;
        Ok(())
    }

    // ─────────────────────────────────────────────
    // Helpers
    // ─────────────────────────────────────────────

    fn const_obj_index(&self, expr: &Expr) -> Result<u16, CompileError> {
        match expr {
            Expr::Number { value, span } => {
                if *value < 0.0 || value.fract() != 0.0 {
                    return Err(CompileError {
                        message: "Object index must be a non-negative integer (e.g. obj[0])".into(),
                        span: *span,
                    });
                }
                let v = *value as i64;
                if v > u16::MAX as i64 {
                    return Err(CompileError {
                        message: "Object index is too large for current VM (u16)".into(),
                        span: *span,
                    });
                }
                Ok(v as u16)
            }
            _ => Err(CompileError {
                message: "Object index must be a constant number for now (e.g. obj[0].x). Variable indexing will come later.".into(),
                span: expr.span(),
            }),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use format::vm::{EngineContext, VM, VMError};

    struct DummyEngine {
        dt: f32,
    }

        impl EngineContext for DummyEngine {
        fn key_down(&self, _key_code: u16) -> bool { false }
        fn get_obj_x(&self, _obj_id: u16) -> Result<f32, VMError> { Ok(0.0) }
        fn get_obj_y(&self, _obj_id: u16) -> Result<f32, VMError> { Ok(0.0) }
        fn set_obj_x(&mut self, _obj_id: u16, _val: f32) -> Result<(), VMError> { Ok(()) }
        fn set_obj_y(&mut self, _obj_id: u16, _val: f32) -> Result<(), VMError> { Ok(()) }
        fn get_dt(&self) -> f32 { self.dt }

        // --- Stubs for tests ---
        fn play_anim(&mut self, _obj_id: u16, _anim_id: u16) -> Result<(), VMError> { Ok(()) }
        fn play_sound(&mut self, _sound_id: u16) -> Result<(), VMError> { Ok(()) }
        fn stop_sound(&mut self, _sound_id: u16) -> Result<(), VMError> { Ok(()) }
        fn loop_sound(&mut self, _sound_id: u16) -> Result<(), VMError> { Ok(()) }
        fn set_volume(&mut self, _sound_id: u16, _vol: f32) -> Result<(), VMError> { Ok(()) }
        fn get_obj_visible(&self, _obj_id: u16) -> Result<f32, VMError> { Ok(1.0) }
        fn set_obj_visible(&mut self, _obj_id: u16, _val: f32) -> Result<(), VMError> { Ok(()) }
        fn get_obj_value(&self, _obj_id: u16) -> Result<f32, VMError> { Ok(0.0) }
        fn set_obj_value(&mut self, _obj_id: u16, _val: f32) -> Result<(), VMError> { Ok(()) }
        fn mouse_x(&self) -> f32 { 0.0 }
        fn mouse_y(&self) -> f32 { 0.0 }
        fn mouse_down(&self) -> bool { false }
        fn get_obj_custom(&self, _obj_id: u16, _slot: u8) -> Result<f32, VMError> { Ok(0.0) }
        fn set_obj_custom(&mut self, _obj_id: u16, _slot: u8, _val: f32) -> Result<(), VMError> { Ok(()) }
        fn get_obj_rotation(&self, _obj_id: u16) -> Result<f32, VMError> { Ok(0.0) }
        fn set_obj_rotation(&mut self, _obj_id: u16, _val: f32) -> Result<(), VMError> { Ok(()) }
        fn get_obj_scale(&self, _obj_id: u16) -> Result<f32, VMError> { Ok(1.0) }
        fn set_obj_scale(&mut self, _obj_id: u16, _val: f32) -> Result<(), VMError> { Ok(()) }
    }

    #[test]
    fn test_compile_without_update_is_ok() {
        let compiled = compile_source("var x = 1").expect("Should compile fine without update");
        assert_eq!(compiled.variable_count, 2); // 'x' + the '__init' flag
    }

    #[test]
    fn test_globals_init_only_once() {
        // If init ran every frame, speed would be:
        // frame1: speed = 201 -> speed+1 => 202
        // frame2: speed = 201 -> speed+1 => 202  (stuck)
        //
        // With init guard, speed should increment:
        // frame1: 202
        // frame2: 203
        let src = r#"
var speed = 201

function update(dt)
    speed = speed + 1
end
"#;

        let compiled = compile_source(src).expect("compile failed");
        assert!(compiled.variable_count >= 2); // speed + __init

        let mut vm = VM::new(compiled.variable_count);
        let mut ctx = DummyEngine { dt: 1.0 / 60.0 };

        // frame 1
        vm.run_frame(&compiled.bytecode, &mut ctx).unwrap();
        let speed_id = 0; // first declared var is speed
        assert_eq!(vm.variables[speed_id], 202.0);

        // frame 2
        vm.run_frame(&compiled.bytecode, &mut ctx).unwrap();
        assert_eq!(vm.variables[speed_id], 203.0);
    }

    #[test]
    fn test_obj_index_must_be_constant() {
        let src = r#"
var i = 0
function update(dt)
    obj[i].x = 1
end
"#;
        let err = compile_source(src).unwrap_err();
        assert!(err.message.contains("constant"));
    }
}