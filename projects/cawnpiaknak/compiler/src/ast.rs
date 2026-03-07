use crate::token::Span;

/// The top-level compilation unit
#[derive(Debug, Clone)]
pub struct Program {
    pub declarations: Vec<Declaration>,
}

/// Top-level declarations.
#[derive(Debug, Clone)]
pub enum Declaration {
    /// `var name = expr`
    VarDecl {
        name: String,
        init: Expr,
        span: Span,
    },

    /// `function name(params...) ... end`
    FunctionDecl {
        name: String,
        params: Vec<String>,
        body: Vec<Statement>,
        span: Span,
    },

    /// `behavior 0 ... end`
    BehaviorDecl {
        shape_index: u16,
        body: Vec<Statement>,
        span: Span,
    },
}

/// Statements inside function bodies.
#[derive(Debug, Clone)]
pub enum Statement {
    /// `name = expr`  or  `obj[N].x = expr`
    Assignment {
        target: AssignTarget,
        value: Expr,
        span: Span,
    },

    /// `if cond then ... [elseif cond then ...] [else ...] end`
    If {
        condition: Expr,
        then_body: Vec<Statement>,
        elseif_clauses: Vec<ElseIfClause>,
        else_body: Vec<Statement>,
        span: Span,
    },

    /// `while cond do ... end`
    While {
        condition: Expr,
        body: Vec<Statement>,
        span: Span,
    },

    /// `return expr` (for future use)
    Return {
        value: Option<Expr>,
        span: Span,
    },

    /// Bare expression as a statement (e.g., function calls)
    ExprStatement {
        expr: Expr,
        span: Span,
    },
}

/// An `elseif` branch.
#[derive(Debug, Clone)]
pub struct ElseIfClause {
    pub condition: Expr,
    pub body: Vec<Statement>,
    pub span: Span,
}

/// What can appear on the left side of `=`.
#[derive(Debug, Clone)]
pub enum AssignTarget {
    /// Simple variable: `speed = ...`
    Variable(String),

    /// Object field: `obj[0].x = ...` or `obj[0].y = ...`
    ObjField {
        obj_index: Expr,
        field: ObjFieldKind,
    },

    /// `self.x = ...` or `self.y = ...`
    SelfField(ObjFieldKind),
}

/// Which field of an object is being accessed.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ObjFieldKind { X, Y, Visible, Value, Rotation, Scale, Val1, Val2, Val3, Val4 }
/// Expressions.
#[derive(Debug, Clone)]
pub enum Expr {
    /// Numeric literal: `42`, `3.14`
    Number {
        value: f32,
        span: Span,
    },

    /// Boolean literal: `true`, `false`
    Bool {
        value: bool,
        span: Span,
    },

    /// String literal: `"right"` (used in key_down calls)
    StringLit {
        value: String,
        span: Span,
    },

    /// Variable reference: `speed`, `score`, `dt`
    Ident {
        name: String,
        span: Span,
    },

    /// Binary operation: `a + b`, `x * dt`, `score < 10`
    BinaryOp {
        op: BinOp,
        left: Box<Expr>,
        right: Box<Expr>,
        span: Span,
    },

    /// Unary operation: `-x`, `not flag`
    UnaryOp {
        op: UnOp,
        operand: Box<Expr>,
        span: Span,
    },

    /// Function call: `key_down("right")`
    Call {
        name: String,
        args: Vec<Expr>,
        span: Span,
    },

    /// Object field access (in expressions): `obj[0].x`
    ObjField {
        obj_index: Box<Expr>,
        field: ObjFieldKind,
        span: Span,
    },

    SelfField {
        field: ObjFieldKind,
        span: Span,
    },
}

impl Expr {
    /// Get the span of this expression.
    pub fn span(&self) -> Span {
        match self {
            Expr::Number { span, .. }    => *span,
            Expr::Bool { span, .. }      => *span,
            Expr::StringLit { span, .. } => *span,
            Expr::Ident { span, .. }     => *span,
            Expr::BinaryOp { span, .. }  => *span,
            Expr::UnaryOp { span, .. }   => *span,
            Expr::Call { span, .. }      => *span,
            Expr::ObjField { span, .. }  => *span,
            Expr::SelfField { span, .. } => *span,
        }
    }
}

/// Binary operators.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum BinOp {
    Add,          // +
    Sub,          // -
    Mul,          // *
    Div,          // /
    Eq,           // ==
    NotEq,        // !=
    Less,         // <
    Greater,      // >
    LessEq,       // <=
    GreaterEq,    // >=
    And,          // and
    Or,           // or
}

impl BinOp {
    /// Precedence level (higher = binds tighter).
    /// Used by the Pratt parser / precedence climbing.
    pub fn precedence(self) -> u8 {
        match self {
            BinOp::Or                                => 1,
            BinOp::And                               => 2,
            BinOp::Eq | BinOp::NotEq                 => 3,
            BinOp::Less | BinOp::Greater |
            BinOp::LessEq | BinOp::GreaterEq         => 4,
            BinOp::Add | BinOp::Sub                  => 5,
            BinOp::Mul | BinOp::Div                  => 6,
        }
    }
}

impl std::fmt::Display for BinOp {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            BinOp::Add       => write!(f, "+"),
            BinOp::Sub       => write!(f, "-"),
            BinOp::Mul       => write!(f, "*"),
            BinOp::Div       => write!(f, "/"),
            BinOp::Eq        => write!(f, "=="),
            BinOp::NotEq     => write!(f, "!="),
            BinOp::Less      => write!(f, "<"),
            BinOp::Greater   => write!(f, ">"),
            BinOp::LessEq    => write!(f, "<="),
            BinOp::GreaterEq => write!(f, ">="),
            BinOp::And       => write!(f, "and"),
            BinOp::Or        => write!(f, "or"),
        }
    }
}

/// Unary operators.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum UnOp {
    Neg,    // -
    Not,    // not
}

impl std::fmt::Display for UnOp {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            UnOp::Neg => write!(f, "-"),
            UnOp::Not => write!(f, "not"),
        }
    }
}

// ═══════════════════════════════════════════════════
// TESTS — verify AST construction (not parsing yet)
// ═══════════════════════════════════════════════════

#[cfg(test)]
mod tests {
    use super::*;

    fn s() -> Span {
        Span::new(1, 1)
    }

    #[test]
    fn test_build_simple_program() {
        // var speed = 200
        // function update(dt) end
        let program = Program {
            declarations: vec![
                Declaration::VarDecl {
                    name: "speed".into(),
                    init: Expr::Number { value: 200.0, span: s() },
                    span: s(),
                },
                Declaration::FunctionDecl {
                    name: "update".into(),
                    params: vec!["dt".into()],
                    body: vec![],
                    span: s(),
                },
            ],
        };

        assert_eq!(program.declarations.len(), 2);
    }

    #[test]
    fn test_build_if_statement() {
        let if_stmt = Statement::If {
            condition: Expr::Call {
                name: "key_down".into(),
                args: vec![Expr::StringLit { value: "right".into(), span: s() }],
                span: s(),
            },
            then_body: vec![
                Statement::Assignment {
                    target: AssignTarget::ObjField {
                        obj_index: Expr::Number { value: 0.0, span: s() },
                        field: ObjFieldKind::X,
                    },
                    value: Expr::BinaryOp {
                        op: BinOp::Add,
                        left: Box::new(Expr::ObjField {
                            obj_index: Box::new(Expr::Number { value: 0.0, span: s() }),
                            field: ObjFieldKind::X,
                            span: s(),
                        }),
                        right: Box::new(Expr::BinaryOp {
                            op: BinOp::Mul,
                            left: Box::new(Expr::Ident { name: "speed".into(), span: s() }),
                            right: Box::new(Expr::Ident { name: "dt".into(), span: s() }),
                            span: s(),
                        }),
                        span: s(),
                    },
                    span: s(),
                },
            ],
            elseif_clauses: vec![],
            else_body: vec![],
            span: s(),
        };

        // Verify structure
        if let Statement::If { then_body, .. } = &if_stmt {
            assert_eq!(then_body.len(), 1);
        } else {
            panic!("Expected If statement");
        }
    }

    #[test]
    fn test_build_while_statement() {
        let while_stmt = Statement::While {
            condition: Expr::BinaryOp {
                op: BinOp::Less,
                left: Box::new(Expr::Ident { name: "score".into(), span: s() }),
                right: Box::new(Expr::Number { value: 10.0, span: s() }),
                span: s(),
            },
            body: vec![
                Statement::Assignment {
                    target: AssignTarget::Variable("score".into()),
                    value: Expr::BinaryOp {
                        op: BinOp::Add,
                        left: Box::new(Expr::Ident { name: "score".into(), span: s() }),
                        right: Box::new(Expr::Number { value: 1.0, span: s() }),
                        span: s(),
                    },
                    span: s(),
                },
            ],
            span: s(),
        };

        if let Statement::While { body, .. } = &while_stmt {
            assert_eq!(body.len(), 1);
        } else {
            panic!("Expected While statement");
        }
    }

    #[test]
    fn test_binop_precedence() {
        // Mul/Div bind tighter than Add/Sub
        assert!(BinOp::Mul.precedence() > BinOp::Add.precedence());
        assert!(BinOp::Div.precedence() > BinOp::Sub.precedence());

        // Add/Sub bind tighter than comparisons
        assert!(BinOp::Add.precedence() > BinOp::Less.precedence());

        // Comparisons bind tighter than equality
        assert!(BinOp::Less.precedence() > BinOp::Eq.precedence());

        // Equality binds tighter than and
        assert!(BinOp::Eq.precedence() > BinOp::And.precedence());

        // And binds tighter than or
        assert!(BinOp::And.precedence() > BinOp::Or.precedence());
    }

    #[test]
    fn test_expr_span() {
        let span = Span::new(5, 10);
        let expr = Expr::Number { value: 42.0, span };
        assert_eq!(expr.span(), span);
    }

    #[test]
    fn test_binop_display() {
        assert_eq!(format!("{}", BinOp::Add), "+");
        assert_eq!(format!("{}", BinOp::Eq), "==");
        assert_eq!(format!("{}", BinOp::And), "and");
    }

    #[test]
    fn test_unop_display() {
        assert_eq!(format!("{}", UnOp::Neg), "-");
        assert_eq!(format!("{}", UnOp::Not), "not");
    }

    #[test]
    fn test_obj_field_kinds() {
        assert_ne!(ObjFieldKind::X, ObjFieldKind::Y);
    }

    #[test]
    fn test_elseif_clause() {
        let clause = ElseIfClause {
            condition: Expr::Bool { value: true, span: s() },
            body: vec![],
            span: s(),
        };
        assert!(clause.body.is_empty());
    }
}