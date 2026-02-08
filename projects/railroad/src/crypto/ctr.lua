-- src/crypto/ctr.lua
-- AES-CTR mode (encrypt/decrypt are the same)
local AES = require("src.crypto.aes")
local bit = bit or require("bit")
local bxor = bit.bxor

local CTR = {}

local function xorBytes(a, b)
  local out = {}
  for i = 1, #a do
    out[i] = string.char(bxor(a:byte(i), b:byte(i)))
  end
  return table.concat(out)
end

local function incCounter(counter16)
  local c = { counter16:byte(1,16) }
  for i = 16, 1, -1 do
    c[i] = (c[i] + 1) % 256
    if c[i] ~= 0 then break end
  end
  return string.char(
    c[1],c[2],c[3],c[4],c[5],c[6],c[7],c[8],
    c[9],c[10],c[11],c[12],c[13],c[14],c[15],c[16]
  )
end

-- ctx = AES.new(key32)
-- nonce16 is the initial counter block (16 bytes)
function CTR.crypt(ctx, nonce16, input)
  assert(type(nonce16) == "string" and #nonce16 == 16, "nonce16 must be 16 bytes")
  assert(type(input) == "string", "input must be string")

  local out = {}
  local counter = nonce16
  local pos = 1

  while pos <= #input do
    local keystream = AES.encryptBlock(ctx, counter)
    local chunk = input:sub(pos, pos + 15)
    out[#out+1] = xorBytes(chunk, keystream:sub(1, #chunk))

    pos = pos + 16
    counter = incCounter(counter)
  end

  return table.concat(out)
end

return CTR