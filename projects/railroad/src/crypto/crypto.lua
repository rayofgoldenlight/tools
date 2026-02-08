-- src/crypto/crypto.lua
local Codec = require("src.crypto.codec")
local KDF   = require("src.crypto.kdf")
local AES   = require("src.crypto.aes")
local CTR   = require("src.crypto.ctr")
local hmac  = require("src.crypto.hmac")
local bit = bit or require("bit")

local Crypto = {}

local DEFAULT_ITERS = 200000

-- ----- constant-time compare -----
local function ctEquals(a, b)
  if type(a) ~= "string" or type(b) ~= "string" then return false end
  if #a ~= #b then return false end
  local diff = 0
  for i = 1, #a do
    diff = bit.bor(diff, bit.bxor(a:byte(i), b:byte(i)))
  end
  return diff == 0
end

-- ----- OS randomness (preferred) -----
local function randomBytes_os(n)
  local ok, ffi = pcall(require, "ffi")
  if not ok then return nil end

  if ffi.os == "Windows" then
    -- Try bcrypt.dll first
    local okLoad, bcrypt = pcall(ffi.load, "bcrypt")
    if okLoad and bcrypt then
      ffi.cdef[[
        typedef unsigned char UCHAR;
        typedef unsigned long ULONG;
        typedef long NTSTATUS;
        NTSTATUS BCryptGenRandom(void* hAlgorithm, UCHAR* pbBuffer, ULONG cbBuffer, ULONG dwFlags);
      ]]
      local buf = ffi.new("UCHAR[?]", n)
      local BCRYPT_USE_SYSTEM_PREFERRED_RNG = 0x00000002
      local okCall, status = pcall(function()
        return bcrypt.BCryptGenRandom(nil, buf, n, BCRYPT_USE_SYSTEM_PREFERRED_RNG)
      end)
      if okCall and status == 0 then
        return ffi.string(buf, n)
      end
    end

    -- Fallback: advapi32 SystemFunction036 (RtlGenRandom)
    local okAdv, advapi = pcall(ffi.load, "advapi32")
    if okAdv and advapi then
      ffi.cdef[[
        typedef int BOOL;
        BOOL SystemFunction036(void* RandomBuffer, unsigned long RandomBufferLength);
      ]]
      local buf = ffi.new("unsigned char[?]", n)
      local okCall, res = pcall(function()
        return advapi.SystemFunction036(buf, n)
      end)
      if okCall and res ~= 0 then
        return ffi.string(buf, n)
      end
    end

    return nil
  else
    -- Linux/macOS: read /dev/urandom
    ffi.cdef[[
      int open(const char *pathname, int flags, ...);
      long read(int fd, void *buf, unsigned long count);
      int close(int fd);
    ]]
    local O_RDONLY = 0
    local fd = ffi.C.open("/dev/urandom", O_RDONLY)
    if fd < 0 then return nil end
    local buf = ffi.new("unsigned char[?]", n)
    local got = ffi.C.read(fd, buf, n)
    ffi.C.close(fd)
    if got ~= n then return nil end
    return ffi.string(buf, n)
  end
end

local function randomBytes_fallback(n)
  -- Not cryptographically strong. Only used if OS RNG fails.
  -- Still OK for development, should keep OS RNG working for "real crypto".
  if love and love.math and love.timer then
    local seed = os.time() + math.floor(love.timer.getTime() * 1000000)
    love.math.setRandomSeed(seed)
  end
  local t = {}
  for i = 1, n do
    t[i] = string.char(love.math.random(0, 255))
  end
  return table.concat(t)
end

local function randomBytes(n)
  local s = randomBytes_os(n)
  if s then return s end
  print("[WARN] OS CSPRNG unavailable; falling back to love.math.random (NOT ideal for real crypto).")
  return randomBytes_fallback(n)
end

-- Compute tag over: MAGIC|VER|ITERS|SALT|NONCE|CIPHERTEXT (i.e., whole container minus tag)
local function computeTag(macKey, iterations, salt, nonce, ciphertext)
  local withoutTag = Codec.packContainer(iterations, salt, nonce, ciphertext, string.rep("\0", Codec.TAG_LEN))
  withoutTag = withoutTag:sub(1, #withoutTag - Codec.TAG_LEN)
  return hmac.sha256_raw(macKey, withoutTag)
end

function Crypto.exportString(data, password, iterations)
  assert(type(password) == "string" and password ~= "", "password required")
  iterations = iterations or DEFAULT_ITERS

  local salt  = randomBytes(Codec.SALT_LEN)
  local nonce = randomBytes(Codec.NONCE_LEN)

  local plaintext = Codec.encodeDataTableToJson(data)

  local encKey, macKey = KDF.deriveKeys(password, salt, iterations)
  local ctx = AES.new(encKey)

  local ciphertext = CTR.crypt(ctx, nonce, plaintext)
  local tag = computeTag(macKey, iterations, salt, nonce, ciphertext)

  local raw = Codec.packContainer(iterations, salt, nonce, ciphertext, tag)
  return Codec.toBase64(raw)
end

function Crypto.importString(b64, password)
  assert(type(password) == "string" and password ~= "", "password required")
  if type(b64) ~= "string" or b64 == "" then
    return nil, "empty_string"
  end

  local ok, rawOrErr = pcall(Codec.fromBase64, b64)
  if not ok or type(rawOrErr) ~= "string" then
    return nil, "bad_base64"
  end
  local raw = rawOrErr

  local ok2, iterations, salt, nonce, ciphertext, tag = pcall(Codec.unpackContainer, raw)
  if not ok2 then
    return nil, "bad_container"
  end

  local encKey, macKey = KDF.deriveKeys(password, salt, iterations)
  local expectedTag = computeTag(macKey, iterations, salt, nonce, ciphertext)

  if not ctEquals(tag, expectedTag) then
    return nil, "bad_password_or_tampered"
  end

  local ctx = AES.new(encKey)
  local plaintext = CTR.crypt(ctx, nonce, ciphertext)

  local ok3, dataOrErr = pcall(Codec.decodeJsonToDataTable, plaintext)
  if not ok3 then
    return nil, "bad_json"
  end

  return dataOrErr
end

return Crypto