local json = require("src.thirdparty.dkjson")

local Codec = {}

Codec.MAGIC = "TMGR"   -- 4 bytes
Codec.VERSION = 1

Codec.SALT_LEN  = 16
Codec.NONCE_LEN = 16
Codec.TAG_LEN   = 32   -- HMAC-SHA256 output

-- pack format (fixed header):
-- [0:3]  MAGIC (4)
-- [4]    VERSION (1 byte)
-- [5:8]  iterations (uint32 big endian)
-- [9:24] salt (16)
-- [25:40] nonce (16)
-- [41:..] ciphertext (variable)
-- [end-31:end] tag (32)
local function u32be(n)
  local b1 = math.floor(n / 16777216) % 256
  local b2 = math.floor(n / 65536) % 256
  local b3 = math.floor(n / 256) % 256
  local b4 = n % 256
  return string.char(b1, b2, b3, b4)
end

local function fromU32be(s, i)
  local b1, b2, b3, b4 = s:byte(i, i+3)
  return ((b1*256 + b2)*256 + b3)*256 + b4
end

function Codec.encodeDataTableToJson(data)
  local txt = json.encode(data, { indent = false })
  assert(type(txt) == "string")
  return txt
end

function Codec.decodeJsonToDataTable(txt)
  local obj, pos, err = json.decode(txt, 1, nil)
  assert(obj, err)
  return obj
end

-- returns: rawBytes (not base64)
function Codec.packContainer(iterations, salt, nonce, ciphertext, tag)
  assert(#salt == Codec.SALT_LEN)
  assert(#nonce == Codec.NONCE_LEN)
  assert(#tag == Codec.TAG_LEN)
  return Codec.MAGIC
    .. string.char(Codec.VERSION)
    .. u32be(iterations)
    .. salt
    .. nonce
    .. ciphertext
    .. tag
end

-- returns: iterations, salt, nonce, ciphertext, tag
function Codec.unpackContainer(raw)
  assert(raw:sub(1,4) == Codec.MAGIC, "bad magic")
  local ver = raw:byte(5)
  assert(ver == Codec.VERSION, "bad version")

  local iterations = fromU32be(raw, 6)
  local saltStart = 10
  local saltEnd = saltStart + Codec.SALT_LEN - 1
  local nonceStart = saltEnd + 1
  local nonceEnd = nonceStart + Codec.NONCE_LEN - 1

  local salt = raw:sub(saltStart, saltEnd)
  local nonce = raw:sub(nonceStart, nonceEnd)

  local tag = raw:sub(-Codec.TAG_LEN)
  local ciphertext = raw:sub(nonceEnd + 1, #raw - Codec.TAG_LEN)

  return iterations, salt, nonce, ciphertext, tag
end

function Codec.toBase64(raw)
  return love.data.encode("string", "base64", raw)
end

function Codec.fromBase64(b64)
  return love.data.decode("string", "base64", b64)
end

return Codec