-- src/crypto/sha256.lua
-- Pure Lua SHA-256 for LuaJIT/Love2D 11.5 (uses bit library)

local bit = bit or require("bit")
local band, bor, bxor, bnot = bit.band, bit.bor, bit.bxor, bit.bnot
local rshift, lshift, ror = bit.rshift, bit.lshift, bit.ror

local sha256 = {}

local function add32(a, b) return band(a + b, 0xffffffff) end
local function add32_4(a,b,c,d) return band(a + b + c + d, 0xffffffff) end
local function add32_5(a,b,c,d,e) return band(a + b + c + d + e, 0xffffffff) end

local function str2u32be(s, i)
  local b1, b2, b3, b4 = s:byte(i, i+3)
  return bor(lshift(b1,24), lshift(b2,16), lshift(b3,8), b4)
end

local function u32be2str(x)
  return string.char(
    band(rshift(x,24),0xff),
    band(rshift(x,16),0xff),
    band(rshift(x,8),0xff),
    band(x,0xff)
  )
end

local K = {
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
}

local function Ch(x,y,z)  return bxor(band(x,y), band(bnot(x),z)) end
local function Maj(x,y,z) return bxor(band(x,y), band(x,z), band(y,z)) end
local function Sig0(x) return bxor(ror(x,2), ror(x,13), ror(x,22)) end
local function Sig1(x) return bxor(ror(x,6), ror(x,11), ror(x,25)) end
local function sig0(x) return bxor(ror(x,7), ror(x,18), rshift(x,3)) end
local function sig1(x) return bxor(ror(x,17), ror(x,19), rshift(x,10)) end

local function pad(msg)
  local len = #msg
  local bitLenHi = 0
  local bitLenLo = len * 8

  -- append 0x80 then 0x00 until length ≡ 56 (mod 64)
  local padLen = 64 - ((len + 1 + 8) % 64)
  if padLen == 64 then padLen = 0 end

  msg = msg .. string.char(0x80) .. string.rep("\0", padLen)
  -- append 64-bit big endian length (we only handle messages < 2^32 bits here; fine for this app)
  msg = msg
    .. string.char(
      0,0,0,0, -- high 32 bits
      band(rshift(bitLenLo,24),0xff),
      band(rshift(bitLenLo,16),0xff),
      band(rshift(bitLenLo,8),0xff),
      band(bitLenLo,0xff)
    )
  return msg
end

function sha256.digestRaw(msg)
  msg = pad(msg)

  local H0 = 0x6a09e667
  local H1 = 0xbb67ae85
  local H2 = 0x3c6ef372
  local H3 = 0xa54ff53a
  local H4 = 0x510e527f
  local H5 = 0x9b05688c
  local H6 = 0x1f83d9ab
  local H7 = 0x5be0cd19

  local W = {}

  for chunkStart = 1, #msg, 64 do
    -- message schedule
    for i = 0, 15 do
      W[i] = str2u32be(msg, chunkStart + i*4)
    end
    for i = 16, 63 do
      W[i] = add32_4(sig1(W[i-2]), W[i-7], sig0(W[i-15]), W[i-16])
    end

    -- working vars
    local a,b,c,d,e,f,g,h = H0,H1,H2,H3,H4,H5,H6,H7

    for i = 0, 63 do
      local T1 = add32_5(h, Sig1(e), Ch(e,f,g), K[i+1], W[i])
      local T2 = add32(Sig0(a), Maj(a,b,c))

      h = g
      g = f
      f = e
      e = add32(d, T1)
      d = c
      c = b
      b = a
      a = add32(T1, T2)
    end

    H0 = add32(H0, a)
    H1 = add32(H1, b)
    H2 = add32(H2, c)
    H3 = add32(H3, d)
    H4 = add32(H4, e)
    H5 = add32(H5, f)
    H6 = add32(H6, g)
    H7 = add32(H7, h)
  end

  return u32be2str(H0)..u32be2str(H1)..u32be2str(H2)..u32be2str(H3)
      .. u32be2str(H4)..u32be2str(H5)..u32be2str(H6)..u32be2str(H7)
end

function sha256.toHex(raw)
  return (raw:gsub(".", function(c) return string.format("%02x", c:byte()) end))
end

function sha256.digestHex(msg)
  return sha256.toHex(sha256.digestRaw(msg))
end

return sha256