-- src/crypto/pbkdf2.lua
-- PBKDF2-HMAC-SHA256, returns raw bytes

local hmac = require("src.crypto.hmac")
local bit = bit or require("bit")
local bxor = bit.bxor

local pbkdf2 = {}

local function u32be(n)
  return string.char(
    bit.band(bit.rshift(n, 24), 0xff),
    bit.band(bit.rshift(n, 16), 0xff),
    bit.band(bit.rshift(n,  8), 0xff),
    bit.band(n, 0xff)
  )
end

local function xorStrings(a, b)
  local out = {}
  for i = 1, #a do
    out[i] = string.char(bxor(a:byte(i), b:byte(i)))
  end
  return table.concat(out)
end

--- password: string
--- salt: string (raw bytes)
--- iterations: number (>= 1)
--- dkLen: number of bytes to derive
function pbkdf2.hmac_sha256(password, salt, iterations, dkLen)
  assert(type(password) == "string", "password must be string")
  assert(type(salt) == "string", "salt must be string")
  assert(type(iterations) == "number" and iterations >= 1, "iterations must be >= 1")
  assert(type(dkLen) == "number" and dkLen > 0, "dkLen must be > 0")

  local hLen = 32 -- SHA-256 output bytes
  local l = math.ceil(dkLen / hLen) -- number of blocks
  local r = dkLen - (l - 1) * hLen

  local blocks = {}

  for i = 1, l do
    local U = hmac.sha256_raw(password, salt .. u32be(i))
    local T = U
    for _ = 2, iterations do
      U = hmac.sha256_raw(password, U)
      T = xorStrings(T, U)
    end
    blocks[i] = T
  end

  local dk = table.concat(blocks)
  if #dk > dkLen then
    dk = dk:sub(1, dkLen)
  end
  return dk
end

return pbkdf2