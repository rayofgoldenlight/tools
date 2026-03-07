pub mod token;
pub mod ast;
pub mod lexer;
pub mod parser;
pub mod codegen;

/// Maps a key name string to the browser keyCode value.
/// Returns None for unrecognized key names.
pub fn key_name_to_code(name: &str) -> Option<u16> {
    match name {
        // ── Arrows ──────────────────────
        "left"      => Some(37),
        "up"        => Some(38),
        "right"     => Some(39),
        "down"      => Some(40),

        // ── Common ──────────────────────
        "space"     => Some(32),
        "enter"     => Some(13),
        "escape"    => Some(27),
        "shift"     => Some(16),
        "ctrl"      => Some(17),
        "alt"       => Some(18),
        "tab"       => Some(9),
        "backspace" => Some(8),
        "delete"    => Some(46),

        // ── Named letters (for readability) ──
        "w" => Some(87), "a" => Some(65),
        "s" => Some(83), "d" => Some(68),
        "e" => Some(69), "f" => Some(70),
        "q" => Some(81), "r" => Some(82),

        // ── Punctuation ─────────────────
        "semicolon" => Some(186),
        "equals"    => Some(187),
        "comma"     => Some(188),
        "minus"     => Some(189),
        "period"    => Some(190),
        "slash"     => Some(191),
        "backtick"  => Some(192),

        _ => {
            // Single ASCII character fallback → its key code
            if name.len() == 1 {
                let ch = name.chars().next().unwrap();
                if ch.is_ascii_alphabetic() {
                    Some(ch.to_ascii_uppercase() as u16)
                } else if ch.is_ascii_digit() {
                    Some(ch as u16) // 0-9 keycodes match ASCII
                } else {
                    None
                }
            } else {
                None
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_key_names() {
        assert_eq!(key_name_to_code("left"), Some(37));
        assert_eq!(key_name_to_code("right"), Some(39));
        assert_eq!(key_name_to_code("up"), Some(38));
        assert_eq!(key_name_to_code("down"), Some(40));
        assert_eq!(key_name_to_code("space"), Some(32));
        assert_eq!(key_name_to_code("w"), Some(87));
        assert_eq!(key_name_to_code("a"), Some(65));
        assert_eq!(key_name_to_code("d"), Some(68));
        assert_eq!(key_name_to_code("s"), Some(83));
        assert_eq!(key_name_to_code("enter"), Some(13));
        assert_eq!(key_name_to_code("escape"), Some(27));
    }

    #[test]
    fn test_single_char_keys() {
        assert_eq!(key_name_to_code("z"), Some(90));
        assert_eq!(key_name_to_code("0"), Some(48));
        assert_eq!(key_name_to_code("9"), Some(57));
    }

    #[test]
    fn test_unknown_key() {
        assert_eq!(key_name_to_code("foobar"), None);
    }
}