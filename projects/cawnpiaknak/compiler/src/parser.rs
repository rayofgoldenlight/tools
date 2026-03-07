use crate::ast::*;
use crate::lexer::{LexError, Lexer};
use crate::token::{Span, Token, TokenKind};

#[derive(Debug, Clone, PartialEq)]
pub struct ParseError {
    pub message: String,
    pub span: Span,
}

impl std::fmt::Display for ParseError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "[{}] Parse error: {}", self.span, self.message)
    }
}

impl From<LexError> for ParseError {
    fn from(e: LexError) -> Self {
        ParseError {
            message: e.message,
            span: e.span,
        }
    }
}

/// Convenience: parse source directly (lexer + parser).
pub fn parse_source(source: &str) -> Result<Program, ParseError> {
    let mut lexer = Lexer::new(source);
    let tokens = lexer.tokenize()?;
    let mut parser = Parser::new(tokens);
    parser.parse_program()
}

pub struct Parser {
    tokens: Vec<Token>,
    i: usize,
}

impl Parser {
    pub fn new(tokens: Vec<Token>) -> Self {
        Self { tokens, i: 0 }
    }

    // ─────────────────────────────────────────────
    // Core helpers
    // ─────────────────────────────────────────────

    fn current(&self) -> &Token {
        // Lexer always appends EOF, but guard for safety
        self.tokens.get(self.i).unwrap_or_else(|| self.tokens.last().unwrap())
    }

    fn peek(&self) -> &Token {
        self.tokens
            .get(self.i + 1)
            .unwrap_or_else(|| self.tokens.last().unwrap())
    }

    fn is_eof(&self) -> bool {
        matches!(self.current().kind, TokenKind::Eof)
    }

    fn advance(&mut self) -> &Token {
        if !self.is_eof() {
            self.i += 1;
        }
        self.tokens
            .get(self.i.saturating_sub(1))
            .unwrap_or_else(|| self.tokens.last().unwrap())
    }

    fn skip_newlines(&mut self) {
        while matches!(self.current().kind, TokenKind::Newline) {
            self.advance();
        }
    }

    fn same_variant(a: &TokenKind, b: &TokenKind) -> bool {
        std::mem::discriminant(a) == std::mem::discriminant(b)
    }

    fn consume(&mut self, kind: &TokenKind) -> bool {
        if Self::same_variant(&self.current().kind, kind) {
            self.advance();
            true
        } else {
            false
        }
    }

    fn expect(&mut self, kind: &TokenKind) -> Result<Token, ParseError> {
        if Self::same_variant(&self.current().kind, kind) {
            Ok(self.advance().clone())
        } else {
            Err(ParseError {
                message: format!("Expected {}, found {}", kind, self.current().kind),
                span: self.current().span,
            })
        }
    }

    fn error_here(&self, msg: impl Into<String>) -> ParseError {
        ParseError {
            message: msg.into(),
            span: self.current().span,
        }
    }

    // ─────────────────────────────────────────────
    // Program / Declarations
    // ─────────────────────────────────────────────

    pub fn parse_program(&mut self) -> Result<Program, ParseError> {
        let mut declarations = Vec::new();

        self.skip_newlines();

        while !self.is_eof() {
            self.skip_newlines();

            if self.is_eof() {
                break;
            }

            let decl = match self.current().kind {
                TokenKind::Var => self.parse_var_decl()?,
                TokenKind::Function => self.parse_function_decl()?,
                TokenKind::Behavior => self.parse_behavior_decl()?,
                _ => {
                    return Err(self.error_here(
                        "Only 'var', 'function', and 'behavior' declarations are allowed at top level",
                    ));
                }
            };
            declarations.push(decl);

            // optional separators
            self.skip_newlines();
        }

        Ok(Program { declarations })
    }

    fn parse_var_decl(&mut self) -> Result<Declaration, ParseError> {
        let var_tok = self.expect(&TokenKind::Var)?;
        self.skip_newlines();

        let name = match &self.current().kind {
            TokenKind::Ident(s) => {
                let s = s.clone();
                self.advance();
                s
            }
            _ => return Err(self.error_here("Expected identifier after 'var'")),
        };

        self.skip_newlines();
        self.expect(&TokenKind::Assign)?;
        self.skip_newlines();

        let init = self.parse_expr(0)?;
        Ok(Declaration::VarDecl {
            name,
            init,
            span: var_tok.span,
        })
    }

    fn parse_function_decl(&mut self) -> Result<Declaration, ParseError> {
        let fn_tok = self.expect(&TokenKind::Function)?;
        self.skip_newlines();

        let name = match &self.current().kind {
            TokenKind::Ident(s) => {
                let s = s.clone();
                self.advance();
                s
            }
            _ => return Err(self.error_here("Expected function name after 'function'")),
        };

        self.skip_newlines();
        self.expect(&TokenKind::LParen)?;
        self.skip_newlines();

        let mut params = Vec::new();
        if !matches!(self.current().kind, TokenKind::RParen) {
            loop {
                self.skip_newlines();
                match &self.current().kind {
                    TokenKind::Ident(s) => {
                        params.push(s.clone());
                        self.advance();
                    }
                    _ => return Err(self.error_here("Expected parameter name")),
                }
                self.skip_newlines();
                if self.consume(&TokenKind::Comma) {
                    continue;
                }
                break;
            }
        }

        self.skip_newlines();
        self.expect(&TokenKind::RParen)?;
        self.skip_newlines();

        let body = self.parse_block_until(&[TokenKind::End])?;
        self.skip_newlines();
        self.expect(&TokenKind::End)?;

        Ok(Declaration::FunctionDecl {
            name,
            params,
            body,
            span: fn_tok.span,
        })
    }

    // Parse statements until we hit one of the stop tokens (by variant).
    fn parse_block_until(&mut self, stop: &[TokenKind]) -> Result<Vec<Statement>, ParseError> {
        let mut stmts = Vec::new();

        self.skip_newlines();

        while !self.is_eof()
            && !stop.iter().any(|k| Self::same_variant(&self.current().kind, k))
        {
            let stmt = self.parse_statement()?;
            stmts.push(stmt);
            self.skip_newlines();
        }

        Ok(stmts)
    }

    // ─────────────────────────────────────────────
    // Statements
    // ─────────────────────────────────────────────

    fn parse_statement(&mut self) -> Result<Statement, ParseError> {
        self.skip_newlines();

        match self.current().kind {
            TokenKind::If => self.parse_if_stmt(),
            TokenKind::While => self.parse_while_stmt(),
            TokenKind::Return => self.parse_return_stmt(),
            _ => self.parse_assignment_or_expr_stmt(),
        }
    }

    fn parse_if_stmt(&mut self) -> Result<Statement, ParseError> {
        let if_tok = self.expect(&TokenKind::If)?;
        self.skip_newlines();

        let condition = self.parse_expr(0)?;
        self.skip_newlines();
        self.expect(&TokenKind::Then)?;
        self.skip_newlines();

        // then-body stops at elseif/else/end
        let then_body = self.parse_block_until(&[TokenKind::ElseIf, TokenKind::Else, TokenKind::End])?;

        // elseif*
        let mut elseif_clauses = Vec::new();
        while matches!(self.current().kind, TokenKind::ElseIf) {
            let elseif_tok = self.expect(&TokenKind::ElseIf)?;
            self.skip_newlines();
            let cond = self.parse_expr(0)?;
            self.skip_newlines();
            self.expect(&TokenKind::Then)?;
            self.skip_newlines();

            let body = self.parse_block_until(&[TokenKind::ElseIf, TokenKind::Else, TokenKind::End])?;

            elseif_clauses.push(ElseIfClause {
                condition: cond,
                body,
                span: elseif_tok.span,
            });

            self.skip_newlines();
        }

        // else?
        let else_body = if matches!(self.current().kind, TokenKind::Else) {
            self.expect(&TokenKind::Else)?;
            self.skip_newlines();
            self.parse_block_until(&[TokenKind::End])?
        } else {
            Vec::new()
        };

        self.skip_newlines();
        self.expect(&TokenKind::End)?;

        Ok(Statement::If {
            condition,
            then_body,
            elseif_clauses,
            else_body,
            span: if_tok.span,
        })
    }

    fn parse_while_stmt(&mut self) -> Result<Statement, ParseError> {
        let while_tok = self.expect(&TokenKind::While)?;
        self.skip_newlines();

        let condition = self.parse_expr(0)?;
        self.skip_newlines();
        self.expect(&TokenKind::Do)?;
        self.skip_newlines();

        let body = self.parse_block_until(&[TokenKind::End])?;
        self.skip_newlines();
        self.expect(&TokenKind::End)?;

        Ok(Statement::While {
            condition,
            body,
            span: while_tok.span,
        })
    }

    fn parse_return_stmt(&mut self) -> Result<Statement, ParseError> {
        let ret_tok = self.expect(&TokenKind::Return)?;
        self.skip_newlines();

        // return <expr> is optional
        let value = match self.current().kind {
            TokenKind::End | TokenKind::Else | TokenKind::ElseIf | TokenKind::Eof | TokenKind::Newline => None,
            _ => Some(self.parse_expr(0)?),
        };

        Ok(Statement::Return {
            value,
            span: ret_tok.span,
        })
    }

    fn parse_assignment_or_expr_stmt(&mut self) -> Result<Statement, ParseError> {
        let start_span = self.current().span;
        let checkpoint = self.i;

        // Try parsing assignment target + '='
        if let Ok(target) = self.try_parse_assign_target() {
            self.skip_newlines();
            if self.consume(&TokenKind::Assign) {
                self.skip_newlines();
                let value = self.parse_expr(0)?;
                return Ok(Statement::Assignment {
                    target,
                    value,
                    span: start_span,
                });
            }
        }

        // Not an assignment, rewind and parse as expression statement
        self.i = checkpoint;
        let expr = self.parse_expr(0)?;
        Ok(Statement::ExprStatement {
            expr,
            span: start_span,
        })
    }

    fn try_parse_assign_target(&mut self) -> Result<AssignTarget, ParseError> {
        self.skip_newlines();

        // Check for `self.x` / `self.y`
        if self.consume(&TokenKind::SelfKw) {
            self.skip_newlines();
            self.expect(&TokenKind::Dot)?;
            self.skip_newlines();

            let field_name = match &self.current().kind {
                TokenKind::Ident(s) => {
                    let s = s.clone();
                    self.advance();
                    s
                }
                _ => return Err(self.error_here("Expected field name after '.'")),
            };

            let field = match field_name.as_str() {
                "x" => ObjFieldKind::X,
                "y" => ObjFieldKind::Y,
                "visible" => ObjFieldKind::Visible, 
                "value" => ObjFieldKind::Value, 
                "rotation" => ObjFieldKind::Rotation,
                "scale" => ObjFieldKind::Scale,
                "val1" => ObjFieldKind::Val1,
                "val2" => ObjFieldKind::Val2,
                "val3" => ObjFieldKind::Val3,
                "val4" => ObjFieldKind::Val4,
                _ => return Err(self.error_here("Only self.x and self.y are supported")),
            };

            return Ok(AssignTarget::SelfField(field));
        }

        // Target must start with an identifier
        let name = match &self.current().kind {
            TokenKind::Ident(s) => {
                let s = s.clone();
                self.advance();
                s
            }
            _ => return Err(self.error_here("Expected assignment target")),
        };

        self.skip_newlines();

        // obj[expr].x / obj[expr].y
        if name == "obj" && matches!(self.current().kind, TokenKind::LBracket) {
            self.expect(&TokenKind::LBracket)?;
            self.skip_newlines();
            let index_expr = self.parse_expr(0)?;
            self.skip_newlines();
            self.expect(&TokenKind::RBracket)?;
            self.skip_newlines();
            self.expect(&TokenKind::Dot)?;
            self.skip_newlines();

            let field_name = match &self.current().kind {
                TokenKind::Ident(s) => {
                    let s = s.clone();
                    self.advance();
                    s
                }
                _ => return Err(self.error_here("Expected field name after '.'")),
            };

            let field = match field_name.as_str() {
                "x" => ObjFieldKind::X,
                "y" => ObjFieldKind::Y,
                "visible" => ObjFieldKind::Visible, 
                "value" => ObjFieldKind::Value, 
                "rotation" => ObjFieldKind::Rotation,
                "scale" => ObjFieldKind::Scale,
                "val1" => ObjFieldKind::Val1,
                "val2" => ObjFieldKind::Val2,
                "val3" => ObjFieldKind::Val3,
                "val4" => ObjFieldKind::Val4,
                _ => {
                    return Err(ParseError {
                        message: "Only obj[].x and obj[].y are supported".into(),
                        span: self.current().span,
                    })
                }
            };

            Ok(AssignTarget::ObjField {
                obj_index: index_expr,
                field,
            })
        } else {
            Ok(AssignTarget::Variable(name))
        }
    }

    // ─────────────────────────────────────────────
    // Expressions (precedence climbing)
    // ─────────────────────────────────────────────

    fn parse_expr(&mut self, min_prec: u8) -> Result<Expr, ParseError> {
        self.skip_newlines();

        let mut left = self.parse_unary()?;
        loop {
            self.skip_newlines();

            let (op, op_span) = match self.current().kind {
                TokenKind::Plus => (BinOp::Add, self.current().span),
                TokenKind::Minus => (BinOp::Sub, self.current().span),
                TokenKind::Star => (BinOp::Mul, self.current().span),
                TokenKind::Slash => (BinOp::Div, self.current().span),

                TokenKind::EqualEqual => (BinOp::Eq, self.current().span),
                TokenKind::NotEqual => (BinOp::NotEq, self.current().span),
                TokenKind::Less => (BinOp::Less, self.current().span),
                TokenKind::Greater => (BinOp::Greater, self.current().span),
                TokenKind::LessEqual => (BinOp::LessEq, self.current().span),
                TokenKind::GreaterEqual => (BinOp::GreaterEq, self.current().span),

                TokenKind::And => (BinOp::And, self.current().span),
                TokenKind::Or => (BinOp::Or, self.current().span),

                _ => break,
            };

            let prec = op.precedence();
            if prec < min_prec {
                break;
            }

            // left-associative: next min prec is prec + 1
            self.advance(); // consume operator
            let right = self.parse_expr(prec + 1)?;

            let span = op_span; // only store a point-span for now
            left = Expr::BinaryOp {
                op,
                left: Box::new(left),
                right: Box::new(right),
                span,
            };
        }

        Ok(left)
    }

    fn parse_unary(&mut self) -> Result<Expr, ParseError> {
        self.skip_newlines();

        match self.current().kind {
            TokenKind::Minus => {
                let tok = self.advance().clone();
                let operand = self.parse_unary()?;
                Ok(Expr::UnaryOp {
                    op: UnOp::Neg,
                    operand: Box::new(operand),
                    span: tok.span,
                })
            }
            TokenKind::Not => {
                let tok = self.advance().clone();
                let operand = self.parse_unary()?;
                Ok(Expr::UnaryOp {
                    op: UnOp::Not,
                    operand: Box::new(operand),
                    span: tok.span,
                })
            }
            _ => self.parse_primary(),
        }
    }

    fn parse_primary(&mut self) -> Result<Expr, ParseError> {
        self.skip_newlines();

        // Clone the token up front to avoid borrow conflicts.
        let tok = self.current().clone();

        match tok.kind {
            TokenKind::Number(n) => {
                self.advance(); // consume
                Ok(Expr::Number {
                    value: n,
                    span: tok.span,
                })
            }
            TokenKind::StringLit(s) => {
                self.advance(); // consume
                Ok(Expr::StringLit {
                    value: s,
                    span: tok.span,
                })
            }
            TokenKind::True => {
                self.advance();
                Ok(Expr::Bool {
                    value: true,
                    span: tok.span,
                })
            }
            TokenKind::False => {
                self.advance();
                Ok(Expr::Bool {
                    value: false,
                    span: tok.span,
                })
            }
            TokenKind::Ident(name) => {
                let start_span = tok.span;
                self.advance(); // consume identifier

                self.skip_newlines();

                // Function call: name(...)
                if matches!(self.current().kind, TokenKind::LParen) {
                    self.expect(&TokenKind::LParen)?;
                    self.skip_newlines();

                    let mut args = Vec::new();
                    if !matches!(self.current().kind, TokenKind::RParen) {
                        loop {
                            let arg = self.parse_expr(0)?;
                            args.push(arg);
                            self.skip_newlines();
                            if self.consume(&TokenKind::Comma) {
                                self.skip_newlines();
                                continue;
                            }
                            break;
                        }
                    }

                    self.skip_newlines();
                    self.expect(&TokenKind::RParen)?;

                    return Ok(Expr::Call {
                        name,
                        args,
                        span: start_span,
                    });
                }

                // Object field expression: obj[expr].x / obj[expr].y
                if name == "obj" && matches!(self.current().kind, TokenKind::LBracket) {
                    self.expect(&TokenKind::LBracket)?;
                    self.skip_newlines();
                    let idx = self.parse_expr(0)?;
                    self.skip_newlines();
                    self.expect(&TokenKind::RBracket)?;
                    self.skip_newlines();
                    self.expect(&TokenKind::Dot)?;
                    self.skip_newlines();

                    let field_tok = self.current().clone();
                    let field_name = match field_tok.kind {
                        TokenKind::Ident(s) => {
                            self.advance();
                            s
                        }
                        _ => return Err(self.error_here("Expected field name after '.'")),
                    };

                    let field = match field_name.as_str() {
                        "x" => ObjFieldKind::X,
                        "y" => ObjFieldKind::Y,
                        "visible" => ObjFieldKind::Visible, 
                        "value" => ObjFieldKind::Value, 
                        "rotation" => ObjFieldKind::Rotation,
                        "scale" => ObjFieldKind::Scale,
                        "val1" => ObjFieldKind::Val1,
                        "val2" => ObjFieldKind::Val2,
                        "val3" => ObjFieldKind::Val3,
                        "val4" => ObjFieldKind::Val4,
                        _ => return Err(self.error_here("Only obj[].x and obj[].y are supported")),
                    };

                    return Ok(Expr::ObjField {
                        obj_index: Box::new(idx),
                        field,
                        span: start_span,
                    });
                }

                Ok(Expr::Ident {
                    name,
                    span: start_span,
                })
            }
            TokenKind::LParen => {
                self.advance(); // consume '('
                let expr = self.parse_expr(0)?;
                self.skip_newlines();
                self.expect(&TokenKind::RParen)?;
                Ok(expr)
            }
            TokenKind::SelfKw => {
                let start_span = tok.span;
                self.advance(); // consume 'self'
                self.skip_newlines();
                self.expect(&TokenKind::Dot)?;
                self.skip_newlines();

                let field_tok = self.current().clone();
                let field_name = match field_tok.kind {
                    TokenKind::Ident(s) => {
                        self.advance();
                        s
                    }
                    _ => return Err(self.error_here("Expected field name after '.'")),
                };

                let field = match field_name.as_str() {
                    "x" => ObjFieldKind::X,
                    "y" => ObjFieldKind::Y,
                    "visible" => ObjFieldKind::Visible,
                    "value" => ObjFieldKind::Value, 
                    "rotation" => ObjFieldKind::Rotation,
                    "scale" => ObjFieldKind::Scale,
                    "val1" => ObjFieldKind::Val1,
                    "val2" => ObjFieldKind::Val2,
                    "val3" => ObjFieldKind::Val3,
                    "val4" => ObjFieldKind::Val4,
                    _ => return Err(self.error_here("Only self.x and self.y are supported")),
                };

                Ok(Expr::SelfField {
                    field,
                    span: start_span,
                })
            }
            _ => Err(ParseError {
                message: format!("Unexpected token in expression: {}", tok.kind),
                span: tok.span,
            }),
        }
    }

    fn parse_behavior_decl(&mut self) -> Result<Declaration, ParseError> {
        let span = self.expect(&TokenKind::Behavior)?.span;
        self.skip_newlines();

        let shape_index = match self.current().kind {
            TokenKind::Number(n) => {
                self.advance();
                n as u16
            }
            _ => return Err(self.error_here("Expected shape index (number) after 'behavior'")),
        };

        self.skip_newlines();
        let body = self.parse_block_until(&[TokenKind::End])?;
        self.skip_newlines();
        self.expect(&TokenKind::End)?;

        Ok(Declaration::BehaviorDecl {
            shape_index,
            body,
            span,
        })
    }
}

// ═══════════════════════════════════════════════════
// TESTS
// ═══════════════════════════════════════════════════

#[cfg(test)]
mod tests {
    use super::*;

    fn parse_ok(src: &str) -> Program {
        parse_source(src).expect("Parse error")
    }

    #[test]
    fn test_parse_var_decl() {
        let p = parse_ok("var speed = 200");
        assert_eq!(p.declarations.len(), 1);

        match &p.declarations[0] {
            Declaration::VarDecl { name, init, .. } => {
                assert_eq!(name, "speed");
                match init {
                    Expr::Number { value, .. } => assert_eq!(*value, 200.0),
                    _ => panic!("Expected number init"),
                }
            }
            _ => panic!("Expected VarDecl"),
        }
    }

    #[test]
    fn test_parse_function_empty() {
        let p = parse_ok("function update(dt) end");
        assert_eq!(p.declarations.len(), 1);

        match &p.declarations[0] {
            Declaration::FunctionDecl { name, params, body, .. } => {
                assert_eq!(name, "update");
                assert_eq!(params, &vec!["dt".to_string()]);
                assert!(body.is_empty());
            }
            _ => panic!("Expected FunctionDecl"),
        }
    }

    #[test]
    fn test_parse_assignment_var() {
        let src = r#"
function update(dt)
    speed = 100
end
"#;
        let p = parse_ok(src);

        let Declaration::FunctionDecl { body, .. } = &p.declarations[0] else {
            panic!("Expected function decl");
        };

        assert_eq!(body.len(), 1);
        match &body[0] {
            Statement::Assignment { target, value, .. } => {
                match target {
                    AssignTarget::Variable(n) => assert_eq!(n, "speed"),
                    _ => panic!("Expected variable target"),
                }
                match value {
                    Expr::Number { value, .. } => assert_eq!(*value, 100.0),
                    _ => panic!("Expected numeric value"),
                }
            }
            _ => panic!("Expected assignment"),
        }
    }

    #[test]
    fn test_parse_assignment_obj_field() {
        let src = r#"
function update(dt)
    obj[0].x = obj[0].x + 1
end
"#;
        let p = parse_ok(src);
        let Declaration::FunctionDecl { body, .. } = &p.declarations[0] else {
            panic!("Expected function decl");
        };
        assert_eq!(body.len(), 1);

        match &body[0] {
            Statement::Assignment { target, .. } => match target {
                AssignTarget::ObjField { field, .. } => assert_eq!(*field, ObjFieldKind::X),
                _ => panic!("Expected obj field target"),
            },
            _ => panic!("Expected assignment"),
        }
    }

    #[test]
    fn test_expression_precedence() {
        // 1 + 2 * 3 => + (1, *(2,3))
        let src = r#"
function update(dt)
    x = 1 + 2 * 3
end
"#;
        let p = parse_ok(src);
        let Declaration::FunctionDecl { body, .. } = &p.declarations[0] else {
            panic!("Expected function decl");
        };

        let Statement::Assignment { value, .. } = &body[0] else {
            panic!("Expected assignment");
        };

        match value {
            Expr::BinaryOp { op: BinOp::Add, left, right, .. } => {
                matches!(**left, Expr::Number { value: 1.0, .. });
                match &**right {
                    Expr::BinaryOp { op: BinOp::Mul, .. } => {}
                    _ => panic!("Expected mul on right"),
                }
            }
            _ => panic!("Expected add at top"),
        }
    }

    #[test]
    fn test_parse_if_elseif_else() {
        let src = r#"
function update(dt)
    if x > 0 then
        y = 1
    elseif x < 0 then
        y = 2
    else
        y = 0
    end
end
"#;
        let p = parse_ok(src);
        let Declaration::FunctionDecl { body, .. } = &p.declarations[0] else {
            panic!("Expected function decl");
        };

        assert_eq!(body.len(), 1);
        match &body[0] {
            Statement::If { then_body, elseif_clauses, else_body, .. } => {
                assert_eq!(then_body.len(), 1);
                assert_eq!(elseif_clauses.len(), 1);
                assert_eq!(else_body.len(), 1);
            }
            _ => panic!("Expected if statement"),
        }
    }

    #[test]
    fn test_parse_while() {
        let src = r#"
function update(dt)
    while score < 10 do
        score = score + 1
    end
end
"#;
        let p = parse_ok(src);
        let Declaration::FunctionDecl { body, .. } = &p.declarations[0] else {
            panic!("Expected function decl");
        };

        assert_eq!(body.len(), 1);
        match &body[0] {
            Statement::While { body, .. } => assert_eq!(body.len(), 1),
            _ => panic!("Expected while statement"),
        }
    }

    #[test]
    fn test_parse_call_expr_statement() {
        let src = r#"
function update(dt)
    key_down("right")
end
"#;
        let p = parse_ok(src);
        let Declaration::FunctionDecl { body, .. } = &p.declarations[0] else {
            panic!("Expected function decl");
        };

        assert_eq!(body.len(), 1);
        match &body[0] {
            Statement::ExprStatement { expr, .. } => match expr {
                Expr::Call { name, args, .. } => {
                    assert_eq!(name, "key_down");
                    assert_eq!(args.len(), 1);
                }
                _ => panic!("Expected call expr"),
            },
            _ => panic!("Expected expr statement"),
        }
    }

    #[test]
    fn test_top_level_rejects_statement() {
        let err = parse_source("x = 1").unwrap_err();
        assert!(err.message.contains("top level"));
    }
}