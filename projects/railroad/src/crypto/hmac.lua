-- src/crypto/hmac.lua
local sha256 = require("src.crypto.sha256")
local bit = bit or require("bit")
local bxor = bit.bxor

local hmac = {}

local function xorWithByte(str, byte)
  local out = {}
  for i = 1, #str do
    out[i] = string.char(bxor(str:byte(i), byte))
  end
  return table.concat(out)
end

local function xorStrings(a, b)
  local out = {}
  for i = 1, #a do
    out[i] = string.char(bxor(a:byte(i), b:byte(i)))
  end
  return table.concat(out)
end

-- Returns raw 32-byte HMAC-SHA256
function hmac.sha256_raw(key, msg)
  assert(type(key) == "string" and type(msg) == "string")

  local blockSize = 64
  if #key > blockSize then
    key = sha256.digestRaw(key)
  end
  if #key < blockSize then
    key = key .. string.rep("\0", blockSize - #key)
  end

  local ipad = string.rep(string.char(0x36), blockSize)
  local opad = string.rep(string.char(0x5c), blockSize)

  local k_ipad = xorStrings(key, ipad)
  local k_opad = xorStrings(key, opad)

  local inner = sha256.digestRaw(k_ipad .. msg)
  return sha256.digestRaw(k_opad .. inner)
end

function hmac.sha256_hex(key, msg)
  return sha256.toHex(hmac.sha256_raw(key, msg))
end

return hmac