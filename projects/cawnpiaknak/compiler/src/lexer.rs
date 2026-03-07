use crate::token::{keyword_lookup, Span, Token, TokenKind};

/// Errors that can occur during lexing.
#[derive(Debug, Clone, PartialEq)]
pub struct LexError {
    pub message: String,
    pub span: Span,
}

impl std::fmt::Display for LexError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "[{}] Lex error: {}", self.span, self.message)
    }
}

/// The lexer. Holds source text and current position.
pub struct Lexer {
    source: Vec<char>,
    pos: usize,
    line: u32,
    col: u32,
}

impl Lexer {
    pub fn new(source: &str) -> Self {
        Self {
            source: source.chars().collect(),
            pos: 0,
            line: 1,
            col: 1,
        }
    }

    /// Tokenize the entire source, returning all tokens or the first error.
    pub fn tokenize(&mut self) -> Result<Vec<Token>, LexError> {
        let mut tokens = Vec::new();

        loop {
            let tok = self.next_token()?;
            let is_eof = tok.kind == TokenKind::Eof;
            tokens.push(tok);
            if is_eof {
                break;
            }
        }

        Ok(tokens)
    }

    // ── Character helpers ───────────────────

    fn current(&self) -> Option<char> {
        self.source.get(self.pos).copied()
    }

    fn peek(&self) -> Option<char> {
        self.source.get(self.pos + 1).copied()
    }

    fn advance(&mut self) -> Option<char> {
        let ch = self.current()?;
        self.pos += 1;
        if ch == '\n' {
            self.line += 1;
            self.col = 1;
        } else {
            self.col += 1;
        }
        Some(ch)
    }

    fn span(&self) -> Span {
        Span::new(self.line, self.col)
    }

    fn error(&self, msg: impl Into<String>) -> LexError {
        LexError {
            message: msg.into(),
            span: self.span(),
        }
    }

    // ── Skip whitespace and comments ────────

    fn skip_whitespace_and_comments(&mut self) {
        loop {
            // Skip whitespace (but NOT newlines — we emit those)
            while let Some(ch) = self.current() {
                if ch == ' ' || ch == '\t' || ch == '\r' {
                    self.advance();
                } else {
                    break;
                }
            }

            // Skip line comments: -- to end of line
            if self.current() == Some('-') && self.peek() == Some('-') {
                while let Some(ch) = self.current() {
                    if ch == '\n' {
                        break;
                    }
                    self.advance();
                }
                // Don't consume the newline — let the main loop emit it
                continue;
            }

            break;
        }
    }

    // ── Main token dispatch ─────────────────

    fn next_token(&mut self) -> Result<Token, LexError> {
        self.skip_whitespace_and_comments();

        let Some(ch) = self.current() else {
            return Ok(Token::new(TokenKind::Eof, self.span()));
        };

        // ── Newlines ────────────────────────
        if ch == '\n' {
            let span = self.span();
            self.advance();
            return Ok(Token::new(TokenKind::Newline, span));
        }

        // ── Numbers ─────────────────────────
        if ch.is_ascii_digit() {
            return self.lex_number();
        }

        // ── Strings ─────────────────────────
        if ch == '"' {
            return self.lex_string();
        }

        // ── Identifiers & keywords ──────────
        if ch.is_ascii_alphabetic() || ch == '_' {
            return self.lex_identifier();
        }

        // ── Two-character operators ─────────
        let span = self.span();

        if ch == '=' && self.peek() == Some('=') {
            self.advance();
            self.advance();
            return Ok(Token::new(TokenKind::EqualEqual, span));
        }

        if ch == '!' && self.peek() == Some('=') {
            self.advance();
            self.advance();
            return Ok(Token::new(TokenKind::NotEqual, span));
        }

        if ch == '<' && self.peek() == Some('=') {
            self.advance();
            self.advance();
            return Ok(Token::new(TokenKind::LessEqual, span));
        }

        if ch == '>' && self.peek() == Some('=') {
            self.advance();
            self.advance();
            return Ok(Token::new(TokenKind::GreaterEqual, span));
        }

        // ── Single-character tokens ─────────
        self.advance();

        let kind = match ch {
            '+' => TokenKind::Plus,
            '-' => TokenKind::Minus,
            '*' => TokenKind::Star,
            '/' => TokenKind::Slash,
            '=' => TokenKind::Assign,
            '<' => TokenKind::Less,
            '>' => TokenKind::Greater,
            '(' => TokenKind::LParen,
            ')' => TokenKind::RParen,
            '[' => TokenKind::LBracket,
            ']' => TokenKind::RBracket,
            '.' => TokenKind::Dot,
            ',' => TokenKind::Comma,
            _ => return Err(LexError {
                message: format!("Unexpected character: '{}'", ch),
                span,  // ← use the span captured before advance()
            }),
        };

        Ok(Token::new(kind, span))
    }

    // ── Number lexing ───────────────────────

    fn lex_number(&mut self) -> Result<Token, LexError> {
        let span = self.span();
        let start = self.pos;

        // Integer part
        while let Some(ch) = self.current() {
            if ch.is_ascii_digit() {
                self.advance();
            } else {
                break;
            }
        }

        // Fractional part
        if self.current() == Some('.') && self.peek().map_or(false, |c| c.is_ascii_digit()) {
            self.advance(); // consume '.'
            while let Some(ch) = self.current() {
                if ch.is_ascii_digit() {
                    self.advance();
                } else {
                    break;
                }
            }
        }

        let text: String = self.source[start..self.pos].iter().collect();
        let value: f32 = text
            .parse()
            .map_err(|_| self.error(format!("Invalid number: '{}'", text)))?;

        Ok(Token::new(TokenKind::Number(value), span))
    }

    // ── String lexing ───────────────────────

    fn lex_string(&mut self) -> Result<Token, LexError> {
        let span = self.span();
        self.advance(); // consume opening '"'

        let mut value = String::new();

        loop {
            match self.current() {
                None => return Err(LexError {
                    message: "Unterminated string".into(),
                    span,
                }),
                Some('\n') => return Err(LexError {
                    message: "Unterminated string (hit newline)".into(),
                    span,
                }),
                Some('"') => {
                    self.advance(); // consume closing '"'
                    break;
                }
                Some('\\') => {
                    self.advance(); // consume '\'
                    match self.current() {
                        Some('n')  => { value.push('\n'); self.advance(); }
                        Some('t')  => { value.push('\t'); self.advance(); }
                        Some('\\') => { value.push('\\'); self.advance(); }
                        Some('"')  => { value.push('"');  self.advance(); }
                        Some(c) => return Err(self.error(format!("Invalid escape: '\\{}'", c))),
                        None => return Err(self.error("Unterminated escape sequence")),
                    }
                }
                Some(ch) => {
                    value.push(ch);
                    self.advance();
                }
            }
        }

        Ok(Token::new(TokenKind::StringLit(value), span))
    }

    // ── Identifier / keyword lexing ─────────

    fn lex_identifier(&mut self) -> Result<Token, LexError> {
        let span = self.span();
        let start = self.pos;

        while let Some(ch) = self.current() {
            if ch.is_ascii_alphanumeric() || ch == '_' {
                self.advance();
            } else {
                break;
            }
        }

        let text: String = self.source[start..self.pos].iter().collect();

        let kind = keyword_lookup(&text).unwrap_or(TokenKind::Ident(text));

        Ok(Token::new(kind, span))
    }
}

/// Convenience: tokenize a string, stripping Newline tokens.
/// Useful for tests that don't care about newlines.
pub fn tokenize_clean(source: &str) -> Result<Vec<Token>, LexError> {
    let mut lexer = Lexer::new(source);
    let tokens = lexer.tokenize()?;
    Ok(tokens
        .into_iter()
        .filter(|t| t.kind != TokenKind::Newline)
        .collect())
}

// ═══════════════════════════════════════════════════
// TESTS
// ═══════════════════════════════════════════════════

#[cfg(test)]
mod tests {
    use super::*;

    /// Helper: tokenize and return just the kinds (stripping newlines and EOF).
    fn kinds(source: &str) -> Vec<TokenKind> {
        let tokens = tokenize_clean(source).expect("Lex error");
        tokens
            .into_iter()
            .filter(|t| t.kind != TokenKind::Eof)
            .map(|t| t.kind)
            .collect()
    }

    /// Helper: tokenize and return kinds INCLUDING newlines (but not EOF).
    fn kinds_with_newlines(source: &str) -> Vec<TokenKind> {
        let mut lexer = Lexer::new(source);
        let tokens = lexer.tokenize().expect("Lex error");
        tokens
            .into_iter()
            .filter(|t| t.kind != TokenKind::Eof)
            .map(|t| t.kind)
            .collect()
    }

    // ── Numbers ─────────────────────────────

    #[test]
    fn test_integer() {
        assert_eq!(kinds("42"), vec![TokenKind::Number(42.0)]);
    }

    #[test]
    fn test_float() {
        assert_eq!(kinds("3.14"), vec![TokenKind::Number(3.14)]);
    }

    #[test]
    fn test_zero() {
        assert_eq!(kinds("0"), vec![TokenKind::Number(0.0)]);
    }

    #[test]
    fn test_float_leading_zero() {
        assert_eq!(kinds("0.5"), vec![TokenKind::Number(0.5)]);
    }

    #[test]
    fn test_multiple_numbers() {
        assert_eq!(
            kinds("1 2 3"),
            vec![
                TokenKind::Number(1.0),
                TokenKind::Number(2.0),
                TokenKind::Number(3.0),
            ]
        );
    }

    // ── Strings ─────────────────────────────

    #[test]
    fn test_simple_string() {
        assert_eq!(
            kinds(r#""hello""#),
            vec![TokenKind::StringLit("hello".into())]
        );
    }

    #[test]
    fn test_empty_string() {
        assert_eq!(
            kinds(r#""""#),
            vec![TokenKind::StringLit("".into())]
        );
    }

    #[test]
    fn test_string_with_spaces() {
        assert_eq!(
            kinds(r#""hello world""#),
            vec![TokenKind::StringLit("hello world".into())]
        );
    }

    #[test]
    fn test_string_escape_sequences() {
        assert_eq!(
            kinds(r#""line\none""#),
            vec![TokenKind::StringLit("line\none".into())]
        );
        assert_eq!(
            kinds(r#""tab\there""#),
            vec![TokenKind::StringLit("tab\there".into())]
        );
        assert_eq!(
            kinds(r#""back\\slash""#),
            vec![TokenKind::StringLit("back\\slash".into())]
        );
        assert_eq!(
            kinds(r#""say \"hi\"""#),
            vec![TokenKind::StringLit("say \"hi\"".into())]
        );
    }

    #[test]
    fn test_unterminated_string() {
        let mut lexer = Lexer::new(r#""hello"#);
        assert!(lexer.tokenize().is_err());
    }

    #[test]
    fn test_string_newline_error() {
        let mut lexer = Lexer::new("\"hello\nworld\"");
        assert!(lexer.tokenize().is_err());
    }

    // ── Keywords ────────────────────────────

    #[test]
    fn test_all_keywords() {
        let source = "var function end if then else elseif while do return and or not true false";
        assert_eq!(
            kinds(source),
            vec![
                TokenKind::Var,
                TokenKind::Function,
                TokenKind::End,
                TokenKind::If,
                TokenKind::Then,
                TokenKind::Else,
                TokenKind::ElseIf,
                TokenKind::While,
                TokenKind::Do,
                TokenKind::Return,
                TokenKind::And,
                TokenKind::Or,
                TokenKind::Not,
                TokenKind::True,
                TokenKind::False,
            ]
        );
    }

    // ── Identifiers ─────────────────────────

    #[test]
    fn test_simple_idents() {
        assert_eq!(
            kinds("speed score x y"),
            vec![
                TokenKind::Ident("speed".into()),
                TokenKind::Ident("score".into()),
                TokenKind::Ident("x".into()),
                TokenKind::Ident("y".into()),
            ]
        );
    }

    #[test]
    fn test_ident_with_underscore() {
        assert_eq!(
            kinds("key_down my_var _private"),
            vec![
                TokenKind::Ident("key_down".into()),
                TokenKind::Ident("my_var".into()),
                TokenKind::Ident("_private".into()),
            ]
        );
    }

    #[test]
    fn test_ident_with_numbers() {
        assert_eq!(
            kinds("x1 pos2d item42"),
            vec![
                TokenKind::Ident("x1".into()),
                TokenKind::Ident("pos2d".into()),
                TokenKind::Ident("item42".into()),
            ]
        );
    }

    #[test]
    fn test_keyword_prefix_is_ident() {
        // "variable" starts with "var" but is an identifier, not a keyword
        assert_eq!(
            kinds("variable functionality ending"),
            vec![
                TokenKind::Ident("variable".into()),
                TokenKind::Ident("functionality".into()),
                TokenKind::Ident("ending".into()),
            ]
        );
    }

    // ── Operators ───────────────────────────

    #[test]
    fn test_single_char_operators() {
        assert_eq!(
            kinds("+ - * / = < >"),
            vec![
                TokenKind::Plus,
                TokenKind::Minus,
                TokenKind::Star,
                TokenKind::Slash,
                TokenKind::Assign,
                TokenKind::Less,
                TokenKind::Greater,
            ]
        );
    }

    #[test]
    fn test_two_char_operators() {
        assert_eq!(
            kinds("== != <= >="),
            vec![
                TokenKind::EqualEqual,
                TokenKind::NotEqual,
                TokenKind::LessEqual,
                TokenKind::GreaterEqual,
            ]
        );
    }

    #[test]
    fn test_operators_no_spaces() {
        assert_eq!(
            kinds("a+b*c==d"),
            vec![
                TokenKind::Ident("a".into()),
                TokenKind::Plus,
                TokenKind::Ident("b".into()),
                TokenKind::Star,
                TokenKind::Ident("c".into()),
                TokenKind::EqualEqual,
                TokenKind::Ident("d".into()),
            ]
        );
    }

    #[test]
    fn test_assign_vs_equality() {
        assert_eq!(
            kinds("x = 5\ny == 3"),
            vec![
                TokenKind::Ident("x".into()),
                TokenKind::Assign,
                TokenKind::Number(5.0),
                TokenKind::Ident("y".into()),
                TokenKind::EqualEqual,
                TokenKind::Number(3.0),
            ]
        );
    }

    // ── Punctuation ─────────────────────────

    #[test]
    fn test_punctuation() {
        assert_eq!(
            kinds("( ) [ ] . ,"),
            vec![
                TokenKind::LParen,
                TokenKind::RParen,
                TokenKind::LBracket,
                TokenKind::RBracket,
                TokenKind::Dot,
                TokenKind::Comma,
            ]
        );
    }

    #[test]
    fn test_obj_field_access() {
        assert_eq!(
            kinds("obj[0].x"),
            vec![
                TokenKind::Ident("obj".into()),
                TokenKind::LBracket,
                TokenKind::Number(0.0),
                TokenKind::RBracket,
                TokenKind::Dot,
                TokenKind::Ident("x".into()),
            ]
        );
    }

    #[test]
    fn test_function_call() {
        assert_eq!(
            kinds(r#"key_down("right")"#),
            vec![
                TokenKind::Ident("key_down".into()),
                TokenKind::LParen,
                TokenKind::StringLit("right".into()),
                TokenKind::RParen,
            ]
        );
    }

    // ── Comments ────────────────────────────

    #[test]
    fn test_line_comment() {
        assert_eq!(
            kinds("42 -- this is a comment"),
            vec![TokenKind::Number(42.0)]
        );
    }

    #[test]
    fn test_comment_on_own_line() {
        assert_eq!(
            kinds("-- full line comment\n42"),
            vec![TokenKind::Number(42.0)]
        );
    }

    #[test]
    fn test_multiple_comments() {
        let source = r#"
-- setup
var speed = 200  -- pixels per second
-- end
"#;
        assert_eq!(
            kinds(source),
            vec![
                TokenKind::Var,
                TokenKind::Ident("speed".into()),
                TokenKind::Assign,
                TokenKind::Number(200.0),
            ]
        );
    }

    #[test]
    fn test_comment_only() {
        assert_eq!(kinds("-- nothing here"), vec![]);
    }

    #[test]
    fn test_minus_vs_comment() {
        // single minus is an operator, double minus is a comment
        assert_eq!(
            kinds("a - b -- subtract"),
            vec![
                TokenKind::Ident("a".into()),
                TokenKind::Minus,
                TokenKind::Ident("b".into()),
            ]
        );
    }

    // ── Newlines ────────────────────────────

    #[test]
    fn test_newlines_emitted() {
        assert_eq!(
            kinds_with_newlines("a\nb"),
            vec![
                TokenKind::Ident("a".into()),
                TokenKind::Newline,
                TokenKind::Ident("b".into()),
            ]
        );
    }

    #[test]
    fn test_multiple_newlines() {
        assert_eq!(
            kinds_with_newlines("a\n\n\nb"),
            vec![
                TokenKind::Ident("a".into()),
                TokenKind::Newline,
                TokenKind::Newline,
                TokenKind::Newline,
                TokenKind::Ident("b".into()),
            ]
        );
    }

    #[test]
    fn test_clean_strips_newlines() {
        let tokens = tokenize_clean("a\nb\nc").unwrap();
        let eof_count = tokens.iter().filter(|t| t.kind == TokenKind::Newline).count();
        assert_eq!(eof_count, 0);
    }

    // ── Spans ───────────────────────────────

    #[test]
    fn test_span_tracking() {
        let mut lexer = Lexer::new("var x = 10");
        let tokens = lexer.tokenize().unwrap();

        // "var" at line 1, col 1
        assert_eq!(tokens[0].span, Span::new(1, 1));
        // "x" at line 1, col 5
        assert_eq!(tokens[1].span, Span::new(1, 5));
        // "=" at line 1, col 7
        assert_eq!(tokens[2].span, Span::new(1, 7));
        // "10" at line 1, col 9
        assert_eq!(tokens[3].span, Span::new(1, 9));
    }

    #[test]
    fn test_span_multiline() {
        let source = "var x\nvar y";
        let mut lexer = Lexer::new(source);
        let tokens = lexer.tokenize().unwrap();

        // Line 1: var(1,1) x(1,5) \n
        assert_eq!(tokens[0].span, Span::new(1, 1)); // var
        assert_eq!(tokens[1].span, Span::new(1, 5)); // x

        // tokens[2] is Newline at end of line 1
        assert_eq!(tokens[2].kind, TokenKind::Newline);

        // Line 2: var(2,1) y(2,5)
        assert_eq!(tokens[3].span, Span::new(2, 1)); // var
        assert_eq!(tokens[4].span, Span::new(2, 5)); // y
    }

    // ── Error cases ─────────────────────────

    #[test]
    fn test_unexpected_character() {
        let mut lexer = Lexer::new("@");
        let err = lexer.tokenize().unwrap_err();
        assert!(err.message.contains("Unexpected character"));
    }

    #[test]
    fn test_invalid_escape() {
        let mut lexer = Lexer::new(r#""\q""#);
        let err = lexer.tokenize().unwrap_err();
        assert!(err.message.contains("Invalid escape"));
    }

    #[test]
    fn test_error_span() {
        let mut lexer = Lexer::new("  @");
        let err = lexer.tokenize().unwrap_err();
        assert_eq!(err.span, Span::new(1, 3));
    }

    #[test]
    fn test_error_display() {
        let err = LexError {
            message: "bad token".into(),
            span: Span::new(3, 7),
        };
        assert_eq!(format!("{}", err), "[line 3, col 7] Lex error: bad token");
    }

    // ── EOF ─────────────────────────────────

    #[test]
    fn test_eof() {
        let mut lexer = Lexer::new("");
        let tokens = lexer.tokenize().unwrap();
        assert_eq!(tokens.len(), 1);
        assert_eq!(tokens[0].kind, TokenKind::Eof);
    }

    #[test]
    fn test_eof_after_tokens() {
        let mut lexer = Lexer::new("42");
        let tokens = lexer.tokenize().unwrap();
        assert_eq!(tokens.last().unwrap().kind, TokenKind::Eof);
    }

    // ── Full program tokenization ───────────

    #[test]
    fn test_var_declaration() {
        assert_eq!(
            kinds("var speed = 200"),
            vec![
                TokenKind::Var,
                TokenKind::Ident("speed".into()),
                TokenKind::Assign,
                TokenKind::Number(200.0),
            ]
        );
    }

    #[test]
    fn test_function_declaration() {
        assert_eq!(
            kinds("function update(dt) end"),
            vec![
                TokenKind::Function,
                TokenKind::Ident("update".into()),
                TokenKind::LParen,
                TokenKind::Ident("dt".into()),
                TokenKind::RParen,
                TokenKind::End,
            ]
        );
    }

    #[test]
    fn test_if_statement() {
        let source = r#"if key_down("right") then
    obj[0].x = obj[0].x + speed * dt
end"#;

        assert_eq!(
            kinds(source),
            vec![
                TokenKind::If,
                TokenKind::Ident("key_down".into()),
                TokenKind::LParen,
                TokenKind::StringLit("right".into()),
                TokenKind::RParen,
                TokenKind::Then,
                TokenKind::Ident("obj".into()),
                TokenKind::LBracket,
                TokenKind::Number(0.0),
                TokenKind::RBracket,
                TokenKind::Dot,
                TokenKind::Ident("x".into()),
                TokenKind::Assign,
                TokenKind::Ident("obj".into()),
                TokenKind::LBracket,
                TokenKind::Number(0.0),
                TokenKind::RBracket,
                TokenKind::Dot,
                TokenKind::Ident("x".into()),
                TokenKind::Plus,
                TokenKind::Ident("speed".into()),
                TokenKind::Star,
                TokenKind::Ident("dt".into()),
                TokenKind::End,
            ]
        );
    }

    #[test]
    fn test_while_statement() {
        let source = "while score < 10 do\n    score = score + 1\nend";
        assert_eq!(
            kinds(source),
            vec![
                TokenKind::While,
                TokenKind::Ident("score".into()),
                TokenKind::Less,
                TokenKind::Number(10.0),
                TokenKind::Do,
                TokenKind::Ident("score".into()),
                TokenKind::Assign,
                TokenKind::Ident("score".into()),
                TokenKind::Plus,
                TokenKind::Number(1.0),
                TokenKind::End,
            ]
        );
    }

    #[test]
    fn test_full_program() {
        let source = r#"
-- Simple movement demo
var speed = 200

function update(dt)
    if key_down("right") then
        obj[0].x = obj[0].x + speed * dt
    end

    if key_down("left") then
        obj[0].x = obj[0].x - speed * dt
    end

    if key_down("down") then
        obj[0].y = obj[0].y + speed * dt
    end

    if key_down("up") then
        obj[0].y = obj[0].y - speed * dt
    end
end
"#;

        let tokens = tokenize_clean(source).unwrap();

        // Should not contain any newlines
        assert!(!tokens.iter().any(|t| t.kind == TokenKind::Newline));

        // Should end with Eof
        assert_eq!(tokens.last().unwrap().kind, TokenKind::Eof);

        // Count some expected tokens
        let keyword_count = tokens
            .iter()
            .filter(|t| matches!(t.kind, TokenKind::If | TokenKind::Then | TokenKind::End))
            .count();
        // 4 if + 4 then + 4 end (one per if) + 1 end (function) = 13
        assert_eq!(keyword_count, 13);

        let key_down_count = tokens
            .iter()
            .filter(|t| t.is_ident("key_down"))
            .count();
        assert_eq!(key_down_count, 4);
    }

    #[test]
    fn test_negative_number_expression() {
        // "-5" should lex as Minus + Number, NOT as Number(-5)
        // The parser handles unary minus
        assert_eq!(
            kinds("-5"),
            vec![TokenKind::Minus, TokenKind::Number(5.0)]
        );
    }

    #[test]
    fn test_elseif_chain() {
        let source = r#"if x > 0 then
    y = 1
elseif x < 0 then
    y = 2
else
    y = 0
end"#;

        assert_eq!(
            kinds(source),
            vec![
                TokenKind::If,
                TokenKind::Ident("x".into()),
                TokenKind::Greater,
                TokenKind::Number(0.0),
                TokenKind::Then,
                TokenKind::Ident("y".into()),
                TokenKind::Assign,
                TokenKind::Number(1.0),
                TokenKind::ElseIf,
                TokenKind::Ident("x".into()),
                TokenKind::Less,
                TokenKind::Number(0.0),
                TokenKind::Then,
                TokenKind::Ident("y".into()),
                TokenKind::Assign,
                TokenKind::Number(2.0),
                TokenKind::Else,
                TokenKind::Ident("y".into()),
                TokenKind::Assign,
                TokenKind::Number(0.0),
                TokenKind::End,
            ]
        );
    }

    #[test]
    fn test_boolean_expression() {
        assert_eq!(
            kinds("true and false or not true"),
            vec![
                TokenKind::True,
                TokenKind::And,
                TokenKind::False,
                TokenKind::Or,
                TokenKind::Not,
                TokenKind::True,
            ]
        );
    }

    #[test]
    fn test_dot_before_number() {
        // "obj[0].x" — the dot is a field access, not a decimal point
        // Make sure "." followed by non-digit letter works
        assert_eq!(
            kinds(".x"),
            vec![TokenKind::Dot, TokenKind::Ident("x".into())]
        );
    }

    #[test]
    fn test_number_then_dot_ident() {
        // "0.x" — tricky case. "0" is a number, "." is dot, "x" is ident
        // But "0.5" is a number 0.5
        // Our lexer: '0' starts a number, sees '.', peeks next char: 
        //   'x' is not a digit → stop number at '0', emit Dot separately
        assert_eq!(
            kinds("0.x"),
            vec![
                TokenKind::Number(0.0),
                TokenKind::Dot,
                TokenKind::Ident("x".into()),
            ]
        );
    }

    #[test]
    fn test_multiple_function_calls() {
        assert_eq!(
            kinds(r#"key_down("a"), key_down("b")"#),
            vec![
                TokenKind::Ident("key_down".into()),
                TokenKind::LParen,
                TokenKind::StringLit("a".into()),
                TokenKind::RParen,
                TokenKind::Comma,
                TokenKind::Ident("key_down".into()),
                TokenKind::LParen,
                TokenKind::StringLit("b".into()),
                TokenKind::RParen,
            ]
        );
    }
}