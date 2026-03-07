/// Source location for error reporting.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Span {
    pub line: u32,
    pub col: u32,
}

impl Span {
    pub fn new(line: u32, col: u32) -> Self {
        Self { line, col }
    }
}

impl std::fmt::Display for Span {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "line {}, col {}", self.line, self.col)
    }
}

/// Every distinct token the lexer can produce.
#[derive(Debug, Clone, PartialEq)]
pub enum TokenKind {
    // ── Literals ────────────────────────
    Number(f32),         // 42, 3.14, 0.5
    StringLit(String),   // "right", "hello"
    True,                // true
    False,               // false

    // ── Identifiers ────────────────────
    Ident(String),       // speed, score, x, myVar

    // ── Keywords ───────────────────────
    Var,                 // var
    Function,            // function
    End,                 // end
    If,                  // if
    Then,                // then
    Else,                // else
    ElseIf,              // elseif
    While,               // while
    Do,                  // do
    Return,              // return

    // ── Operators ──────────────────────
    Plus,                // +
    Minus,               // -
    Star,                // *
    Slash,               // /
    Assign,              // =
    EqualEqual,          // ==
    NotEqual,            // !=
    Less,                // <
    Greater,             // >
    LessEqual,           // <=
    GreaterEqual,        // >=

    // ── Logical ────────────────────────
    And,                 // and
    Or,                  // or
    Not,                 // not

    // ── Punctuation ────────────────────
    LParen,              // (
    RParen,              // )
    LBracket,            // [
    RBracket,            // ]
    Dot,                 // .
    Comma,               // ,

    // ── Special ────────────────────────
    Newline,             // statement separator (optional, for error recovery)
    Eof,                 // end of input
    Behavior,            // behavior
    SelfKw,              // self
}

impl std::fmt::Display for TokenKind {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            TokenKind::Number(n)     => write!(f, "{}", n),
            TokenKind::StringLit(s)  => write!(f, "\"{}\"", s),
            TokenKind::True          => write!(f, "true"),
            TokenKind::False         => write!(f, "false"),
            TokenKind::Ident(s)      => write!(f, "{}", s),
            TokenKind::Var           => write!(f, "var"),
            TokenKind::Function      => write!(f, "function"),
            TokenKind::End           => write!(f, "end"),
            TokenKind::If            => write!(f, "if"),
            TokenKind::Then          => write!(f, "then"),
            TokenKind::Else          => write!(f, "else"),
            TokenKind::ElseIf        => write!(f, "elseif"),
            TokenKind::While         => write!(f, "while"),
            TokenKind::Do            => write!(f, "do"),
            TokenKind::Return        => write!(f, "return"),
            TokenKind::Plus          => write!(f, "+"),
            TokenKind::Minus         => write!(f, "-"),
            TokenKind::Star          => write!(f, "*"),
            TokenKind::Slash         => write!(f, "/"),
            TokenKind::Assign        => write!(f, "="),
            TokenKind::EqualEqual    => write!(f, "=="),
            TokenKind::NotEqual      => write!(f, "!="),
            TokenKind::Less          => write!(f, "<"),
            TokenKind::Greater       => write!(f, ">"),
            TokenKind::LessEqual     => write!(f, "<="),
            TokenKind::GreaterEqual  => write!(f, ">="),
            TokenKind::And           => write!(f, "and"),
            TokenKind::Or            => write!(f, "or"),
            TokenKind::Not           => write!(f, "not"),
            TokenKind::LParen        => write!(f, "("),
            TokenKind::RParen        => write!(f, ")"),
            TokenKind::LBracket      => write!(f, "["),
            TokenKind::RBracket      => write!(f, "]"),
            TokenKind::Dot           => write!(f, "."),
            TokenKind::Comma         => write!(f, ","),
            TokenKind::Newline       => write!(f, "\\n"),
            TokenKind::Eof           => write!(f, "EOF"),
            TokenKind::Behavior      => write!(f, "behavior"),
            TokenKind::SelfKw        => write!(f, "self"),
        }
    }
}

/// A token with its source location.
#[derive(Debug, Clone, PartialEq)]
pub struct Token {
    pub kind: TokenKind,
    pub span: Span,
}

impl Token {
    pub fn new(kind: TokenKind, span: Span) -> Self {
        Self { kind, span }
    }

    /// Convenience: check if this token matches a specific kind.
    pub fn is(&self, kind: &TokenKind) -> bool {
        std::mem::discriminant(&self.kind) == std::mem::discriminant(kind)
    }

    /// Check if this is an identifier with a specific name.
    pub fn is_ident(&self, name: &str) -> bool {
        matches!(&self.kind, TokenKind::Ident(s) if s == name)
    }
}

/// Maps keyword strings to token kinds.
/// Returns None for non-keyword identifiers.
pub fn keyword_lookup(word: &str) -> Option<TokenKind> {
    match word {
        "var"      => Some(TokenKind::Var),
        "function" => Some(TokenKind::Function),
        "end"      => Some(TokenKind::End),
        "if"       => Some(TokenKind::If),
        "then"     => Some(TokenKind::Then),
        "else"     => Some(TokenKind::Else),
        "elseif"   => Some(TokenKind::ElseIf),
        "while"    => Some(TokenKind::While),
        "do"       => Some(TokenKind::Do),
        "return"   => Some(TokenKind::Return),
        "and"      => Some(TokenKind::And),
        "or"       => Some(TokenKind::Or),
        "not"      => Some(TokenKind::Not),
        "true"     => Some(TokenKind::True),
        "false"    => Some(TokenKind::False),
        "behavior" => Some(TokenKind::Behavior),
        "self"     => Some(TokenKind::SelfKw),
        _          => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_keyword_lookup() {
        assert_eq!(keyword_lookup("var"), Some(TokenKind::Var));
        assert_eq!(keyword_lookup("function"), Some(TokenKind::Function));
        assert_eq!(keyword_lookup("end"), Some(TokenKind::End));
        assert_eq!(keyword_lookup("if"), Some(TokenKind::If));
        assert_eq!(keyword_lookup("then"), Some(TokenKind::Then));
        assert_eq!(keyword_lookup("else"), Some(TokenKind::Else));
        assert_eq!(keyword_lookup("elseif"), Some(TokenKind::ElseIf));
        assert_eq!(keyword_lookup("while"), Some(TokenKind::While));
        assert_eq!(keyword_lookup("do"), Some(TokenKind::Do));
        assert_eq!(keyword_lookup("return"), Some(TokenKind::Return));
        assert_eq!(keyword_lookup("and"), Some(TokenKind::And));
        assert_eq!(keyword_lookup("or"), Some(TokenKind::Or));
        assert_eq!(keyword_lookup("not"), Some(TokenKind::Not));
        assert_eq!(keyword_lookup("true"), Some(TokenKind::True));
        assert_eq!(keyword_lookup("false"), Some(TokenKind::False));
    }

    #[test]
    fn test_non_keywords() {
        assert_eq!(keyword_lookup("speed"), None);
        assert_eq!(keyword_lookup("player"), None);
        assert_eq!(keyword_lookup("x"), None);
        assert_eq!(keyword_lookup("obj"), None);
        assert_eq!(keyword_lookup("key_down"), None);
    }

    #[test]
    fn test_span_display() {
        let span = Span::new(5, 12);
        assert_eq!(format!("{}", span), "line 5, col 12");
    }

    #[test]
    fn test_token_is() {
        let tok = Token::new(TokenKind::Plus, Span::new(1, 1));
        assert!(tok.is(&TokenKind::Plus));
        assert!(!tok.is(&TokenKind::Minus));
    }

    #[test]
    fn test_token_is_ident() {
        let tok = Token::new(TokenKind::Ident("speed".into()), Span::new(1, 1));
        assert!(tok.is_ident("speed"));
        assert!(!tok.is_ident("score"));
    }

    #[test]
    fn test_token_kind_display() {
        assert_eq!(format!("{}", TokenKind::Plus), "+");
        assert_eq!(format!("{}", TokenKind::EqualEqual), "==");
        assert_eq!(format!("{}", TokenKind::Number(3.14)), "3.14");
        assert_eq!(format!("{}", TokenKind::StringLit("hello".into())), "\"hello\"");
        assert_eq!(format!("{}", TokenKind::Ident("speed".into())), "speed");
        assert_eq!(format!("{}", TokenKind::Var), "var");
        assert_eq!(format!("{}", TokenKind::Eof), "EOF");
    }
}