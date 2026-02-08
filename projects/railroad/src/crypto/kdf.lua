-- src/crypto/kdf.lua
local pbkdf2 = require("src.crypto.pbkdf2")

local KDF = {}

-- Derive 64 bytes: first 32 = encKey, next 32 = macKey
function KDF.deriveKeys(password, salt, iterations)
  local dk = pbkdf2.hmac_sha256(password, salt, iterations, 64)
  local encKey = dk:sub(1, 32)
  local macKey = dk:sub(33, 64)
  return encKey, macKey
end

return KDF