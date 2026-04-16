{-# LANGUAGE ForeignFunctionInterface #-}
{-# LANGUAGE DeriveGeneric #-}
{-# LANGUAGE OverloadedStrings #-}

-- on Windows
-- to recompile this, use (in WSL, need to install ghc-wasm-meta from here: https://github.com/haskell-wasm/ghc-wasm-meta):
-- source ~/.ghc-wasm/env
-- wasm32-wasi-cabal build --allow-newer
-- cp $(find dist-newstyle -name "*.wasm" | grep -v "dependencies") ./analyzer.wasm
-- 
-- for optimization:
-- wasm-opt -O3 -o analyzer.wasm analyzer.wasm

module Main where

import GHC.Generics (Generic)
import Data.Aeson (ToJSON, encode)
import Data.Text (Text)
import qualified Data.Text as T
import qualified Data.ByteString.Lazy as BL
import qualified Data.ByteString.Lazy.Char8 as BL8
import Data.Word (Word8, Word16, Word32, Word64)
import Data.Char (isDigit)
import Control.Monad (replicateM)
import Data.Bits (testBit)
import qualified Data.ByteString.Char8 as BC
import Foreign.C.String (CString, newCString)
import Foreign.Ptr (Ptr, castPtr)
import qualified Data.ByteString as BS
import qualified Data.Map.Strict as M
import Numeric (showFFloat, showHex)
import qualified Data.ByteString.Char8 as BSC
import qualified Data.Text.Encoding as TE
import qualified Data.Text.Encoding.Error as TE
import Data.List (find, foldl')
import Data.Int (Int8, Int32)
import qualified Data.Set as Set

import Data.Binary.Get

-- | Behavior EXE is capable of + evidence.
data Capability = Capability
  { capName     :: Text
  , capSeverity :: Text   -- "High", "Medium", "Info"
  , capEvidence :: [Text] -- Strings or imports that triggered this
  } deriving (Show, Generic)

instance ToJSON Capability

data PEReport = PEReport
  { fileName          :: Text
  , fileSize          :: Int
  , architecture      :: Text
  , subsystem         :: Text
  , entryPoint        :: Word32
  , compileTime       :: Word32
  , overlaySize       :: Int
  , isSigned          :: Bool
  , signatureInfo     :: [Text] 
  , isDotNet          :: Bool
  , dotNetVersion     :: Text
  , hasTLS            :: Bool
  , pdbPath           :: Text
  , sectionNames      :: [Text]
  , riskScore         :: Int
  , verdict           :: Text
  , capabilities      :: [Capability]
  , exportedFunctions :: [Text]
  , suspiciousImports :: [Text]
  , suspiciousStrings :: [Text]
  , disassembly       :: [Text] 
  } deriving (Show, Generic)

instance ToJSON PEReport

data COFFHeader = COFFHeader
  { machine              :: Word16
  , numberOfSections     :: Word16
  , timeDateStamp        :: Word32
  , sizeOfOptionalHeader :: Word16
  } deriving (Show)

-- | Represents e.g. Export Table, Import Table, Resources
data DataDirectory = DataDirectory
  { dirVA   :: Word32 -- Virtual Address
  , dirSize :: Word32 -- Size
  } deriving (Show)

-- | Section Header with RVA routing data
data SectionHeader = SectionHeader
  { secName          :: BS.ByteString
  , virtualSize      :: Word32
  , virtualAddress   :: Word32
  , sizeOfRawData    :: Word32
  , pointerToRawData :: Word32
  , characteristics  :: Word32
  } deriving (Show)

-- | Parses single Section Header
parseSectionHeader :: Get SectionHeader
parseSectionHeader = do
    nameBytes <- getByteString 8
    vSize     <- getWord32le
    vAddr     <- getWord32le
    rawSize   <- getWord32le
    rawPtr    <- getWord32le
    skip 12                           -- Skip Relocations/LineNumbers
    chars     <- getWord32le
    return $ SectionHeader nameBytes vSize vAddr rawSize rawPtr chars

-- | ByteString ASCII string extractor.
extractAsciiFast :: Maybe Int -> BS.ByteString -> [Text]
extractAsciiFast cap strictBytes = 
    let -- ASCII printable range: 32-126 (" "-"~")
        isPrintable w = w >= 32 && w <= 126
        
        -- Split file at every non-printable byte
        chunks = BS.splitWith (not . isPrintable) strictBytes
        
        -- Filter chunks: length >= 5 and <= 256 (ignore big strings)
        -- Cap at 2000 strings total for default
        filtered = filter (\c -> BS.length c >= 5 && BS.length c <= 256) chunks

        validChunks = case cap of
            Just n  -> take n filtered
            Nothing -> filtered
        
    -- Decode to Text, ignoring malformed byte sequences
    in map (TE.decodeUtf8With TE.lenientDecode) validChunks
    
-- | Extracts Windows Wide Strings (UTF-16LE, printable ASCII subset).
-- Look for sequences of (Printable Byte, 0x00)
extractWideAsciiFast :: Maybe Int -> BS.ByteString -> [Text]
extractWideAsciiFast cap bs = takeCap (go 0)
  where
    takeCap xs = case cap of
        Just n  -> take n xs
        Nothing -> xs

    bsLen = BS.length bs
    isP w = w >= 32 && w <= 126
    
    go i
      | i + 9 >= bsLen = [] -- need at least 5 wide chars (10 bytes)
      | isP (BS.index bs i) && BS.index bs (i+1) == 0 =
          let end = wideEnd (i + 2)
              charCount = (end - i) `div` 2
          in if charCount >= 5
             then decodeSlice i end : go end
             else go (i + 1)
      | otherwise = go (i + 1)

    wideEnd j
      | j + 1 < bsLen && isP (BS.index bs j) && BS.index bs (j+1) == 0 = wideEnd (j + 2)
      | otherwise = j
      
    -- pack ASCII half of the wide characters
    decodeSlice start end = 
        TE.decodeUtf8With TE.lenientDecode $ 
        BS.pack [BS.index bs k | k <- [start, start+2 .. end - 2]]

-- | parse the Optional Header bytes to extract Data Directories
parseDataDirectories :: BL.ByteString -> [DataDirectory]
parseDataDirectories bytes = 
    case runGetOrFail getDirs bytes of
        Right (_, _, dirs) -> dirs
        Left _             -> []
  where
    getDirs = do
        magic <- getWord16le
        let is64 = magic == 0x020B -- 0x010B 32-bit, 0x020B 64-bit
        let offsetToRva = if is64 then 108 else 92
        
        -- Ensure header is large enough
        if BL.length bytes < fromIntegral (offsetToRva + 4)
            then return []
            else do
                skip (offsetToRva - 2) -- already read 2-byte magic
                numRva <- getWord32le
                
                -- Cap at 16 directories
                let safeNum = min 16 (fromIntegral numRva)
                replicateM safeNum $ do
                    va <- getWord32le
                    sz <- getWord32le
                    return $ DataDirectory va sz


-- | Parser for DOS Header, PE Signature, and COFF Header
parsePEHeader :: Get (COFFHeader, [DataDirectory], [SectionHeader], BL.ByteString)
parsePEHeader = do
    -- DOS Header & PE offset
    magic <- getWord16le 
    if magic /= 0x5A4D 
        then fail "Not a valid MZ DOS executable. Missing 'MZ' signature."
        else do
            skip 58
            peOffset <- getWord32le
            let bytesToSkip = fromIntegral peOffset - 64
            if bytesToSkip < 0 
                then fail "Corrupted PE: Invalid e_lfanew pointer."
                else skip bytesToSkip
                
            -- PE Signature
            peSig <- getWord32le
            if peSig /= 0x00004550 
                then fail "Invalid PE signature. Missing 'PE\\0\\0'."
                else do
                    -- NOW AT COFF HEADER (20 Bytes Total)
                    m      <- getWord16le
                    numSec <- getWord16le
                    tds    <- getWord32le
                    
                    skip 8 -- Skip PointerToSymbolTable (4) & NumberOfSymbols (4)
                    
                    optHdrSize <- getWord16le
                    skip 2 -- Skip Characteristics (2)
                    
                    let coff = COFFHeader m numSec tds optHdrSize
                    
                    -- Skip Optional Header to reach Section Headers
                    optHdrBytes <- getLazyByteString (fromIntegral optHdrSize)
                    let dataDirs = parseDataDirectories optHdrBytes
                    
                    -- parse all Section Headers
                    sections <- replicateM (fromIntegral numSec) parseSectionHeader
                    
                    return (coff, dataDirs, sections, optHdrBytes)

-- | Convert rva to physical file offset
rvaToOffset :: Word32 -> [SectionHeader] -> Maybe Word32
rvaToOffset rva sections =
    let isValid sec = rva >= virtualAddress sec && rva < (virtualAddress sec + max (virtualSize sec) (sizeOfRawData sec))
    in case filter isValid sections of
        (s:_) -> Just $ rva - virtualAddress s + pointerToRawData s
        []    -> Nothing

-- | Extract null-terminated ASCII string from a specific file offset
readStringAtOffset :: BL.ByteString -> Word32 -> Text
readStringAtOffset fullBytes offset =
    let strBytes = BL.takeWhile (/= 0) $ BL.drop (fromIntegral offset) fullBytes
    in T.pack (BC.unpack (BL.toStrict strBytes))

-- | run Get parser on slice of bytes
runGetSafe :: Get a -> BL.ByteString -> Maybe a
runGetSafe p b = case runGetOrFail p b of
    Right (_, _, val) -> Just val
    Left _            -> Nothing

-- | Parse Import Table (extracts "DLL -> Function" strings)
parseImports :: BL.ByteString -> [SectionHeader] -> Bool -> Word64 -> DataDirectory -> ([Text], M.Map Word64 Text)
parseImports fullBytes sections is64 imageBase importDir =
    if dirVA importDir == 0 then ([], M.empty) else
    case rvaToOffset (dirVA importDir) sections of
        Nothing     -> ([], M.empty)
        Just offset -> 
            let (strsRev, mapPairs) = extractDescriptors offset (0 :: Int) [] []
            in (reverse strsRev, M.fromList mapPairs)
  where
    extractDescriptors off count strAcc mapAcc =
        if count > 100 || off + 20 > fromIntegral (BL.length fullBytes) then (strAcc, mapAcc) else
        let chunk = BL.drop (fromIntegral off) fullBytes
            nameRVA  = runGetSafe (skip 12 >> getWord32le) chunk
            -- FirstThunk (IAT) contains memory addresses called by the code
            thunkRVA = runGetSafe (skip 16 >> getWord32le) chunk 
        in case (nameRVA, thunkRVA) of
            (Just nRVA, Just tRVA) | nRVA /= 0 -> 
                let dllName = case rvaToOffset nRVA sections of
                                Just noff -> readStringAtOffset fullBytes noff
                                Nothing   -> "Unknown.dll"
                    (funcsRev, newMapPairs) = extractThunks tRVA (0 :: Int) [] []
                    funcs = reverse funcsRev
                    formatted = map (\f -> dllName <> " -> " <> f) funcs
                in extractDescriptors (off + 20) (count + 1) (reverse formatted ++ strAcc) (newMapPairs ++ mapAcc)
            _ -> (strAcc, mapAcc)

    extractThunks rva count funcAcc mapAcc =
        if count > 500 || rva == 0 then (funcAcc, mapAcc) else
        case rvaToOffset rva sections of
            Nothing -> (funcAcc, mapAcc)
            Just off -> 
                let chunk = BL.drop (fromIntegral off) fullBytes
                    (thunkSize, val, isOrdinal) = if is64 
                        then case runGetSafe getWord64le chunk of
                                Just v  -> (8, fromIntegral (v `mod` 0xFFFFFFFF), testBit v 63)
                                Nothing -> (8, 0, False)
                        else case runGetSafe getWord32le chunk of
                                Just v  -> (4, v, testBit v 31)
                                Nothing -> (4, 0, False)
                    -- Calculate absolute memory address of this specific thunk
                    absAddr = imageBase + fromIntegral rva
                in if val == 0 then (funcAcc, mapAcc) else
                   if isOrdinal then extractThunks (rva + thunkSize) (count + 1) ("<Ordinal>" : funcAcc) mapAcc else
                   case rvaToOffset val sections of
                       Just nameOff -> 
                           let funcName = readStringAtOffset fullBytes (nameOff + 2)
                           in extractThunks (rva + thunkSize) (count + 1) (funcName : funcAcc) ((absAddr, funcName) : mapAcc)
                       Nothing -> extractThunks (rva + thunkSize) (count + 1) funcAcc mapAcc

-- | Parse Delay Import Table (Extracts "lazy" "DLL -> Function" strings)
parseDelayImports :: BL.ByteString -> [SectionHeader] -> Bool -> DataDirectory -> [Text]
parseDelayImports fullBytes sections is64 delayDir =
    if dirVA delayDir == 0 then [] else
    case rvaToOffset (dirVA delayDir) sections of
        Nothing     -> []
        Just offset -> reverse $ extractDescriptors offset (0 :: Int) []
  where
    extractDescriptors off count acc =
        -- Delay descriptors are 32 bytes
        if count > 100 || off + 32 > fromIntegral (BL.length fullBytes) then acc else
        let chunk = BL.drop (fromIntegral off) fullBytes
            nameRVA = runGetSafe (skip 4 >> getWord32le) chunk   -- DllNameRVA is at offset 4
            intRVA  = runGetSafe (skip 16 >> getWord32le) chunk  -- DelayINT (Import Name Table) is at offset 16
        in case (nameRVA, intRVA) of
            (Just nRVA, Just iRVA) | nRVA /= 0 && iRVA /= 0 -> 
                let dllName = case rvaToOffset nRVA sections of
                                Just noff -> readStringAtOffset fullBytes noff
                                Nothing   -> "Unknown.dll"
                    funcsRev = extractThunks iRVA (0 :: Int) []
                    funcs = reverse funcsRev
                    formatted = map (\f -> "[DELAY] " <> dllName <> " -> " <> f) funcs
                in extractDescriptors (off + 32) (count + 1) (reverse formatted ++ acc)
            _ -> acc

    -- reusing Thunk logic as normal imports
    extractThunks rva count acc =
        if count > 500 || rva == 0 then acc else
        case rvaToOffset rva sections of
            Nothing -> acc
            Just off -> 
                let chunk = BL.drop (fromIntegral off) fullBytes
                    (thunkSize, val, isOrdinal) = if is64 
                        then case runGetSafe getWord64le chunk of
                                Just v  -> (8, fromIntegral (v `mod` 0xFFFFFFFF), testBit v 63)
                                Nothing -> (8, 0, False)
                        else case runGetSafe getWord32le chunk of
                                Just v  -> (4, v, testBit v 31)
                                Nothing -> (4, 0, False)
                in if val == 0 then acc else
                   if isOrdinal then extractThunks (rva + thunkSize) (count + 1) ("<Ordinal>" : acc) else
                   case rvaToOffset val sections of
                       Just nameOff -> 
                           let funcName = readStringAtOffset fullBytes (nameOff + 2)
                           in extractThunks (rva + thunkSize) (count + 1) (funcName : acc)
                       Nothing -> extractThunks (rva + thunkSize) (count + 1) acc

-- | calculate Shannon Entropy of ByteString (0.0 to 8.0)
calculateEntropy :: BL.ByteString -> Double
calculateEntropy bs
    | len == 0  = 0.0
    | otherwise = negate $ sum [ p * logBase 2 p | p <- probs ]
  where
    len = fromIntegral (BL.length bs) :: Double
    -- Count frequency per byte
    freqs = BL.foldl' (\acc w -> M.insertWith (+) w 1 acc) M.empty bs
    -- Calculate probability per byte
    probs = map (\count -> count / len) (M.elems freqs)

-- | anomalies in PE sections and Entry Point
detectAnomalies :: Word32 -> [SectionHeader] -> [Text]
detectAnomalies epRva sections =
    let 
        -- Find Entry Point section
        epSec = find (\s -> epRva >= virtualAddress s && epRva < virtualAddress s + max (virtualSize s) (sizeOfRawData s)) sections
        
        epAnomalies = case epSec of
            Nothing -> if epRva == 0 then [] else ["Entry Point (0x" <> T.pack (showHex epRva "") <> ") does not map to any known section."]
            Just s  -> 
                let isWritable = testBit (characteristics s) 31
                    isExec     = testBit (characteristics s) 29
                    name       = T.pack . BSC.unpack . BS.takeWhile (/= 0) $ secName s
                in (if isWritable then ["Entry Point is inside a Writable section (" <> name <> "). Highly indicative of packing/injection."] else []) ++
                   (if not isExec then ["Entry Point is inside a Non-Executable section (" <> name <> ")."] else [])

        -- Check sections for size/permission anomalies
        secAnomalies = concatMap checkSec sections
        checkSec s =
            let name = T.pack . BSC.unpack . BS.takeWhile (/= 0) $ secName s
                vSize = virtualSize s
                rSize = sizeOfRawData s
                chars = characteristics s
                isExec = testBit chars 29
                isWritable = testBit chars 31
                isZeroRaw = rSize == 0 && vSize > 0
                isHugeVirtual = vSize > 0 && rSize > 0 && (vSize > rSize * 5) -- Virtual size is 5x physical
            in (if isExec && isZeroRaw then ["Executable section with 0 physical bytes (" <> name <> "). Memory is allocated dynamically."] else []) ++
               (if isExec && isWritable then ["RWX (Read/Write/Execute) section found (" <> name <> "). Often used for shellcode."] else []) ++
               (if isHugeVirtual then ["Highly compressed or uninitialized section (" <> name <> "). Virtual size vastly exceeds raw size."] else [])

    in epAnomalies ++ secAnomalies
    
-- | threat categories, severity, and API substrings that trigger them
behaviorRules :: [(Text, Text, [Text])]
behaviorRules = 
    [ ("Process Injection & Memory Manipulation", "High", 
        ["virtualalloc", "writeprocessmemory", "createremotethread", "ntunmapviewofsection", "queueuserapc", "setthreadcontext", "ntcreatethreadex", "mapviewoffile"])
    , ("Network Communication & C2", "Medium", 
        ["winhttpopen", "internetopen", "urldownloadtofile", "wsastartup", "gethostbyname", "httpsendrequest", "internetconnect"])
    , ("Command & Process Execution", "High", 
        ["createprocess", "shellexecute", "winexec", "system", "loadlibrary", "getprocaddress"])
    , ("Persistence & Configuration", "Medium", 
        ["regsetvalueex", "createservice", "startservice", "schtasks", "copyfile", "movefile"])
    , ("Anti-Analysis & Evasion", "High", 
        ["isdebuggerpresent", "checkremotedebuggerpresent", "outputdebugstring", "sleep", "gettickcount", "queryperformancecounter", "virtualprotect", "flusinstructioncache"])
    , ("Cryptography & Ransomware", "High", 
        ["cryptencrypt", "cryptdecrypt", "bcryptencrypt", "bcryptdecrypt", "cryptacquirecontext"])
    ]

-- | Scanner that categorizes imported functions into Behavior Chains
classifyImports :: [Text] -> [(Text, Text, [Text])]
classifyImports imports = 
    let lowerImports = map (\i -> (i, T.toLower i)) imports
        
        -- Check if a specific import matches any of the rule substrings
        matchesRule ruleStrings lowerImp = any (`T.isInfixOf` lowerImp) ruleStrings
        
        -- Filter the full import list against a specific category's rules
        getMatches ruleStrings = 
            let matched = filter (\(_, lowerImp) -> matchesRule ruleStrings lowerImp) lowerImports
                matches = map fst matched
                matchCount = length matches
                cap = if matchCount > 200 then 10 else 25 -- Cap at 10/25 matches per category
            in take cap matches
            
        -- reverse accumulation
        results = foldl' (\acc (catName, severity, ruleStrings) -> 
            let matches = getMatches ruleStrings
            in if null matches then acc else (catName, severity, matches) : acc
           ) [] behaviorRules
    in reverse results

-- | Defines combinations of APIs that prove behavior
behaviorChains :: [(Text, Text, [Text])]
behaviorChains =
    [ ("Process Injection (Classic)", "High", ["virtualalloc", "writeprocessmemory", "createremotethread"])
    , ("Process Injection (Reflective DLL)", "High", ["virtualalloc", "virtualprotect", "loadlibrary", "getprocaddress"])
    , ("File Dropper", "High", ["createfile", "writefile", "createprocess"])
    , ("Credential Theft (DPAPI)", "High", ["cryptunprotectdata"])
    , ("Keylogger", "High", ["setwindowshookex", "getasynckeystate", "getkeystate"])
    , ("Screen Capture", "Medium", ["bitblt", "getdc", "createcompatiblebitmap"])
    , ("Download & Execute", "High", ["urldownloadtofile", "shellexecute"])
    , ("Service Persistence", "High", ["openscmanager", "createservice", "startservice"])
    ]

-- | Scans imports for behavior chains
classifyChains :: [Text] -> [(Text, Text, [Text])]
classifyChains imports = 
    let lowerImports = map T.toLower imports
        checkChain (name, sev, required) =
            let matched = filter (\r -> any (r `T.isInfixOf`) lowerImports) required
            in if length matched == length required
               then Just (name, sev, matched)
               else Nothing
    in foldl' (\acc chain -> case checkChain chain of
                                Just res -> res : acc
                                Nothing  -> acc) [] behaviorChains

-- | Check to see if a string looks like an IPv4 address
isIPv4 :: Text -> Bool
isIPv4 t = 
    let parts = T.splitOn "." t
    in length parts == 4 && all (\p -> T.length p > 0 && T.length p <= 3 && T.all isDigit p) parts

-- | Scan strings and puts into categorized Indicators of Compromise (IOCs)
-- Returns: (Network IOCs, Registry IOCs, Script/Command IOCs, Suspicious Paths)
extractIOCs :: Int -> [Text] -> ([Text], [Text], [Text], [Text])
extractIOCs iocCap strings = foldl' categorize ([], [], [], []) strings
  where
    categorize (net, reg, cmd, path) s =
        let ls = T.toLower s
            
            -- Network: URLs and IPs
            isNet = "http://" `T.isInfixOf` ls || "https://" `T.isInfixOf` ls || isIPv4 s
            
            -- Registry: Autoruns and persistence keys
            isReg = "hkey_" `T.isInfixOf` ls || "hkcu\\" `T.isInfixOf` ls || "hklm\\" `T.isInfixOf` ls || "software\\microsoft\\windows\\currentversion" `T.isInfixOf` ls
            
            -- Scripts/Commands: PowerShell, WScript, CMD
            isCmd = "powershell" `T.isInfixOf` ls || "cmd.exe" `T.isInfixOf` ls || "wscript" `T.isInfixOf` ls || "-enc " `T.isInfixOf` ls || "/c " `T.isInfixOf` ls
            
            -- File Paths: Temp, AppData, System32 drops
            isPath = "appdata" `T.isInfixOf` ls || "temp\\" `T.isInfixOf` ls || "%temp%" `T.isInfixOf` ls || "system32\\" `T.isInfixOf` ls || ".vbs" `T.isSuffixOf` ls
            
            -- Cap categories at 15/30
            addIf cond cat = if cond && not (s `elem` cat) && length cat < iocCap then s:cat else cat
            
        in ( addIf isNet net
           , addIf isReg reg
           , addIf isCmd cmd
           , addIf isPath path
           )
           
-- | Defines byte pattern to scan
data OpcodePattern = OpcodePattern
    { patName    :: Text
    , patBytes   :: BS.ByteString
    , patMeaning :: Text
    } deriving (Show)

suspiciousPatterns :: [OpcodePattern]
suspiciousPatterns =
    [ OpcodePattern "PEB Access (x86)" 
        (BS.pack [0x64, 0xA1, 0x30, 0x00, 0x00, 0x00])
        "Manual API resolution via Process Environment Block (FS:0x30)"
    , OpcodePattern "PEB Access (x64)" 
        (BS.pack [0x65, 0x48, 0x8B, 0x04, 0x25, 0x60, 0x00, 0x00, 0x00])
        "Manual 64-bit API resolution via PEB (GS:0x60)"
    , OpcodePattern "NOP Sled (8+)" 
        (BS.pack [0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90])
        "NOP sled often preceding shellcode"
    , OpcodePattern "Int 2D (Anti-Debug)" 
        (BS.pack [0xCD, 0x2D])
        "Interrupt 0x2D triggers debugger breakpoint exception"
    , OpcodePattern "SYSCALL" 
        (BS.pack [0x0F, 0x05])
        "Direct system call bypassing Win32 API layer"
    ]

-- | Sweeps executable sections for raw opcode patterns
scanForPatterns :: BS.ByteString -> [SectionHeader] -> [Text]
scanForPatterns strictBytes sections =
    let execSections = filter (\s -> testBit (characteristics s) 29) sections
        scanSec sec = 
            let secBytes = BS.take (fromIntegral $ sizeOfRawData sec) 
                         $ BS.drop (fromIntegral $ pointerToRawData sec) strictBytes
                name = T.pack . BSC.unpack . BS.takeWhile (/= 0) $ secName sec
            in foldl' (\acc pat -> 
                if patBytes pat `BS.isInfixOf` secBytes
                then (patName pat <> " in " <> name <> ": " <> patMeaning pat) : acc
                else acc
               ) [] suspiciousPatterns
    in concatMap scanSec execSections

-- | Evaluates data using fold
evaluateRisk :: [(Text, Double, Word32)] -> ([Text], [Text], [Text], [Text]) -> [Text] -> Int -> Bool -> Text -> Bool -> [Word32] -> [Text] -> [Text] -> [Text] -> (Int, Text, [Capability], [Text])
evaluateRisk sections (netIOCs, regIOCs, cmdIOCs, pathIOCs) imports overlay isSigned pdbPath isDotNet tlsCallbacks anomalies embeddedPayloads opcodes =
    let
        -- STRINGS PRE-PROCESSED
        suspiciousStrs = take 50 (netIOCs ++ regIOCs ++ cmdIOCs ++ pathIOCs)

        -- BEHAVIOR CLASSIFICATION
        behaviors = classifyImports imports
        chains = classifyChains imports
        allBehaviors = behaviors ++ chains
        
        sevScore "High" = 40
        sevScore "Medium" = 20
        sevScore _ = 10
        
        behaviorScore = sum [sevScore sev | (_, sev, _) <- allBehaviors]
        behaviorCaps = [Capability name sev ev | (name, sev, ev) <- allBehaviors]

        -- SECTION METADATA
        knownPackers = ["upx", "aspack", "mpress", "nsp", "themida", "vmp"]
        packerEv  = [ s | (s, _, _) <- sections, any (\p -> p `T.isInfixOf` T.toLower s) knownPackers ]
        entropyEv = [ s <> " (" <> T.pack (showFFloat (Just 2) e "") <> ")" | (s, e, _) <- sections, e >= 7.2 ]
        rwxEv     = [ s | (s, _, chars) <- sections, testBit chars 29 && testBit chars 31 ]
        
        totalImports = length imports
        lowerImports = map T.toLower imports
        hasDynResolver = any (\i -> "getprocaddress" `T.isInfixOf` i || "loadlibrary" `T.isInfixOf` i) lowerImports
        hasMemLoader = any (\i -> "virtualalloc" `T.isInfixOf` i || "virtualprotect" `T.isInfixOf` i) lowerImports
        
        isHeavilyPacked = not isDotNet && (not (null entropyEv) || not (null packerEv)) && totalImports > 0 && totalImports < 15 && hasDynResolver
        
        unpackEv = (if isHeavilyPacked then ["Tiny Import Table (" <> T.pack (show totalImports) <> " imports) + High Entropy. Binary is reconstructing its payload in memory dynamically."] else [])
                ++ (if isHeavilyPacked && hasMemLoader then ["Code uses VirtualAlloc/VirtualProtect to map the hidden payload into executable memory."] else [])

        -- SCORING
        baseScore = sum [ if not (null netIOCs)  then 15 else 0
                        , if not (null cmdIOCs)  then 20 else 0
                        , if not (null regIOCs)  then 15 else 0
                        , if not (null pathIOCs) then 10 else 0
                        , if not (null packerEv) then 40 else 0
                        , if not (null entropyEv) then 20 else 0
                        , if overlay > 1024      then 15 else 0
                        , length anomalies * 15
                        , if not (null embeddedPayloads) then 30 else 0
                        , if not (null unpackEv) then 35 else 0
                        , if not (null opcodes) then 30 else 0
                        , behaviorScore
                        ]
                        
        score = max 0 (if isSigned then baseScore - 15 else baseScore + 10)
        
        verdict | score >= 80 = "Critical Risk"
                | score >= 60 = "High Risk"
                | score >= 30 = "Suspicious"
                | otherwise   = "Likely Safe"
                
        -- CAPABILITIES
        mkCap name sev evList = if null evList then [] else [Capability name sev evList]
        
        staticCaps = concat
            [ mkCap "Network/C2 IOCs" "High" netIOCs
            , mkCap "Suspicious Memory (RWX)" "High" rwxEv
            , mkCap "Command/Scripting IOCs" "High" cmdIOCs
            , mkCap "Persistence/Registry IOCs" "High" regIOCs
            , mkCap "Suspicious File Paths" "Medium" pathIOCs
            , mkCap "Packed / Obfuscated" "High" packerEv
            , mkCap "High Entropy (Encrypted)" "Medium" entropyEv
            , mkCap "Dynamic Unpacking Heuristics" "High" unpackEv
            , mkCap "Structural Anomalies" "High" anomalies
            , mkCap "Embedded Payloads" "High" embeddedPayloads
            , mkCap "Suspicious Code Patterns" "High" opcodes
            , if overlay > 1024 then [Capability "Hidden Overlay" "Medium" [T.pack $ show overlay ++ " bytes"]] else []
            , if not isSigned then [Capability "Unsigned Binary" "Info" ["Missing Signature"]] else []
            , if pdbPath /= "" then [Capability "Leaked Build Path" "Info" [pdbPath]] else []
            , if isDotNet then [Capability ".NET Managed Assembly" "Info" ["File is a C#/.NET executable, not native machine code."]] else []
            , if not (null tlsCallbacks) 
                then [Capability "TLS Callbacks Executed" "High" 
                     ("Code executes before Entry Point at:" : map (\rva -> "0x" <> T.pack (showHex rva "")) tlsCallbacks)] 
                else []
            ]

    in (score, verdict, staticCaps ++ behaviorCaps, suspiciousStrs)

-- | Extract PDB/Debug path if present
extractPdbPath :: BL.ByteString -> [SectionHeader] -> DataDirectory -> Text
extractPdbPath fullBytes sections debugDir =
    if dirVA debugDir == 0 || dirSize debugDir < 28 then "" else
    case rvaToOffset (dirVA debugDir) sections of
        Nothing     -> ""
        Just offset -> 
            let chunk = BL.drop (fromIntegral offset) fullBytes
                parseDebug = do
                    skip 12                  -- Characteristics, TimeDateStamp, Major/MinorVer
                    dbgType <- getWord32le   -- Type
                    skip 8                   -- SizeOfData, AddressOfRawData
                    rawPtr <- getWord32le    -- PointerToRawData
                    return (dbgType, rawPtr)
            in case runGetSafe parseDebug chunk of
                Just (2, rawPtr) | rawPtr > 0 ->  -- Type 2 - CodeView
                    let cvChunk = BL.drop (fromIntegral rawPtr) fullBytes
                    in case runGetSafe getWord32le cvChunk of
                        Just 0x53445352 -> readStringAtOffset fullBytes (rawPtr + 24) -- "RSDS" signature is 0x53445352. String is 24 bytes after.
                        _               -> ""
                _ -> ""

-- | Helper to pad hex with a leading zero
toHex :: Word8 -> String
toHex w = let h = showHex w "" in if length h == 1 then "0" ++ h else h

-- | x86/x64 Pattern Matching Disassembler
-- decodes subset of relevant opcodes (Prologues, Jumps, Calls, NOPs).
disassembleBytes :: M.Map Word64 Text -> BS.ByteString -> Word64 -> Int -> Int -> [Text]
disassembleBytes importMap bytes baseAddress offset maxInstr
    | BS.null bytes || maxInstr <= 0 = []
    | otherwise = 
        let b1 = BS.index bytes 0
            currentAddr = baseAddress + fromIntegral offset
            addrPrefix = T.pack (showHex currentAddr "") <> " | "
            
            -- Helper for hex
            hexB b = T.pack (toHex b)
            
            -- Standard x86/x64 opcodes
        in case b1 of
            0x55 -> (addrPrefix <> "55          | push ebp") : next 1
            0x90 -> (addrPrefix <> "90          | nop") : next 1
            0xCC -> (addrPrefix <> "CC          | int 3") : next 1
            0xC3 -> (addrPrefix <> "C3          | ret") : next 1
            
            -- CALL rel32 (E8 xx xx xx xx)
            0xE8 -> if BS.length bytes >= 5 then
                        let rel32 = runGetSafe getWord32le (BL.fromStrict $ BS.take 4 $ BS.drop 1 bytes)
                        in case rel32 of
                            Just rel -> 
                                -- Calculate target: Current Addr + 5 (instruction size) + rel32 offset
                                -- x86, rel32 is a signed two's complement offset.
                                let target = currentAddr + 5 + fromIntegral (fromIntegral rel :: Int32)
                                    hexStr = hexB b1 <> " " <> hexB (BS.index bytes 1) <> " " <> hexB (BS.index bytes 2) <> " " <> hexB (BS.index bytes 3) <> " " <> hexB (BS.index bytes 4)
                                in (addrPrefix <> hexStr <> " | call 0x" <> T.pack (showHex target "")) : next 5
                            Nothing -> fallback b1
                    else fallback b1
            
            -- JMP rel32 (E9 xx xx xx xx)
            0xE9 -> if BS.length bytes >= 5 then
                        let rel32 = runGetSafe getWord32le (BL.fromStrict $ BS.take 4 $ BS.drop 1 bytes)
                        in case rel32 of
                            Just rel -> 
                                let target = currentAddr + 5 + fromIntegral (fromIntegral rel :: Int32)
                                in (addrPrefix <> "E9 ...      | jmp 0x" <> T.pack (showHex target "")) : next 5
                            Nothing -> fallback b1
                    else fallback b1
                    
            -- JMP rel8 (EB xx)
            0xEB -> if BS.length bytes >= 2 then
                        let rel8 = fromIntegral (BS.index bytes 1) :: Int8
                            target = currentAddr + 2 + fromIntegral rel8
                        in (addrPrefix <> "EB " <> hexB (BS.index bytes 1) <> "       | jmp short 0x" <> T.pack (showHex target "")) : next 2
                    else fallback b1

            -- XOR EAX, EAX (33 C0)
            0x33 -> if BS.length bytes >= 2 && BS.index bytes 1 == 0xC0
                    then (addrPrefix <> "33 C0       | xor eax, eax") : next 2
                    else fallback b1
                    
            -- PUSH imm32 (68 xx xx xx xx)
            0x68 -> if BS.length bytes >= 5 then
                        (addrPrefix <> "68 ...      | push imm32") : next 5
                    else fallback b1
                    
            -- Indirect CALL (FF 15 xx xx xx xx) - Often for IAT calls
            0xFF -> if BS.length bytes >= 6 && BS.index bytes 1 == 0x15 then
                        let ptr32 = runGetSafe getWord32le (BL.fromStrict $ BS.take 4 $ BS.drop 2 bytes)
                        in case ptr32 of
                            Just ptr -> 
                                let ptr64 = fromIntegral ptr
                                    -- LOOKUP THE API NAME IN HASHMAP
                                    targetStr = case M.lookup ptr64 importMap of
                                                    Just apiName -> apiName
                                                    Nothing      -> "0x" <> T.pack (showHex ptr "")
                                in (addrPrefix <> "FF 15 ...   | call dword ptr [" <> targetStr <> "]") : next 6
                            Nothing -> fallback b1
                    else fallback b1

            _ -> fallback b1
            
  where
    next step = disassembleBytes importMap (BS.drop step bytes) baseAddress (offset + step) (maxInstr - 1)
    
    fallback b = 
        let currentAddr = baseAddress + fromIntegral offset
            addrPrefix = T.pack (showHex currentAddr "") <> " | "
        in (addrPrefix <> T.pack (toHex b) <> "          | db 0x" <> T.pack (toHex b)) : next 1

-- | Parse TLS Dir to extract pre-execution Callback RVAs
parseTLSCallbacks :: BL.ByteString -> [SectionHeader] -> Bool -> Word64 -> DataDirectory -> [Word32]
parseTLSCallbacks fullBytes sections is64 imageBase tlsDir =
    if dirVA tlsDir == 0 || imageBase == 0 then [] else
    case rvaToOffset (dirVA tlsDir) sections of
        Nothing -> []
        Just offset ->
            let chunk = BL.drop (fromIntegral offset) fullBytes
                -- AddressOfCallBacks is at offset 12 (32-bit) / 24 (64-bit)
                cbVaOffset = if is64 then 24 else 12
                cbVA = if is64 
                       then runGetSafe (skip cbVaOffset >> getWord64le) chunk
                       else fromIntegral <$> runGetSafe (skip cbVaOffset >> getWord32le) chunk
            in case cbVA of
                Just va | va >= imageBase -> 
                    let arrayRVA = fromIntegral (va - imageBase)
                    in case rvaToOffset arrayRVA sections of
                        Just arrOff -> reverse $ readCallbackArray arrOff (0 :: Int) []
                        Nothing     -> []
                _ -> []
  where
    -- Read null-terminated array of callback pointers
    readCallbackArray off count acc =
        if count > 10 || off + 8 > fromIntegral (BL.length fullBytes) then acc else
        let chunk = BL.drop (fromIntegral off) fullBytes
            val = if is64 
                  then runGetSafe getWord64le chunk
                  else fromIntegral <$> runGetSafe getWord32le chunk
        in case val of
            Just v | v == 0 -> acc -- Null terminator
            Just v | v >= imageBase -> 
                let cbRVA = fromIntegral (v - imageBase)
                    step = if is64 then 8 else 4
                in readCallbackArray (off + step) (count + 1) (cbRVA : acc)
            _ -> acc

-- | Parse Export Dir to extract exported function names
parseExports :: BL.ByteString -> [SectionHeader] -> DataDirectory -> [Text]
parseExports fullBytes sections exportDir =
    if dirVA exportDir == 0 then [] else
    case rvaToOffset (dirVA exportDir) sections of
        Nothing -> []
        Just offset ->
            let chunk = BL.drop (fromIntegral offset) fullBytes
            in case runGetSafe parseExportHeader chunk of
                Just (numNames, namesRVA) | numNames > 0 ->
                    case rvaToOffset namesRVA sections of
                        Just namesOff -> reverse $ extractExportNames namesOff 0 (min numNames 500) [] -- cap at 500
                        Nothing -> []
                _ -> []
  where
    parseExportHeader = do
        skip 24              -- Skip to NumberOfNames
        numNames <- getWord32le
        skip 4               -- Skip AddressOfFunctions
        namesRVA <- getWord32le
        return (numNames, namesRVA)

    extractExportNames off count maxCount acc =
        if count >= maxCount then acc else
        let chunk = BL.drop (fromIntegral off) fullBytes
        in case runGetSafe getWord32le chunk of
            Just nameRVA | nameRVA /= 0 ->
                case rvaToOffset nameRVA sections of
                    Just strOff -> 
                        let funcName = readStringAtOffset fullBytes strOff
                        in extractExportNames (off + 4) (count + 1) maxCount (funcName : acc)
                    Nothing -> extractExportNames (off + 4) (count + 1) maxCount acc
            _ -> acc

-- | Parse Security Dir (Certs)
-- Data Dir 4's VirtualAddress is physical, not rva
parseSignatureDetails :: BL.ByteString -> DataDirectory -> [Text]
parseSignatureDetails fullBytes secDir =
    if dirVA secDir == 0 || dirSize secDir == 0 then [] else
    let offset = fromIntegral (dirVA secDir)
    in if offset + 8 > fromIntegral (BL.length fullBytes) then [] else
       let chunk = BL.drop offset fullBytes
           parseCert = do
               dwLength <- getWord32le
               wRevision <- getWord16le
               wCertType <- getWord16le
               return (dwLength, wRevision, wCertType)
       in case runGetSafe parseCert chunk of
           Just (len, rev, ctype) ->
               let typeName = case ctype of
                               0x0001 -> "X.509"
                               0x0002 -> "PKCS#7 Authenticode"
                               0x0003 -> "PKCS#10"
                               0x0004 -> "PKCS#7 SignedData"
                               _      -> "Unknown (" <> T.pack (show ctype) <> ")"
                   revName = case rev of
                               0x0100 -> "v1"
                               0x0200 -> "v2"
                               _      -> "Unknown (" <> T.pack (show rev) <> ")"
               in [ "Type: " <> typeName
                  , "Revision: " <> revName
                  , "Size: " <> T.pack (show len) <> " bytes"
                  , "Raw Offset: 0x" <> T.pack (showHex offset "") ]
           Nothing -> ["Invalid Certificate Header"]

-- | Scans raw byte chunk for known file signatures (Magic Bytes)
detectEmbeddedPayloads :: BS.ByteString -> [Text]
detectEmbeddedPayloads bytes =
    let hasMZ  = "MZ\x90\x00" `BS.isInfixOf` bytes
        hasZIP = "PK\x03\x04" `BS.isInfixOf` bytes
        hasCAB = "MSCF" `BS.isInfixOf` bytes
        has7z  = "7z\xBC\xAF\x27\x1C" `BS.isInfixOf` bytes
        hasRar = "Rar!\x1A\x07" `BS.isInfixOf` bytes
    in (if hasMZ then ["Embedded PE Executable (MZ)"] else []) ++
       (if hasZIP then ["Embedded ZIP Archive"] else []) ++
       (if hasCAB then ["Embedded CAB Archive"] else []) ++
       (if has7z then ["Embedded 7z Archive"] else []) ++
       (if hasRar then ["Embedded RAR Archive"] else [])
       
-- | Scan Resource Dir for privilege escalation manifests and hidden payloads
parseResources :: BL.ByteString -> [SectionHeader] -> DataDirectory -> ([Text], [Text])
parseResources fullBytes sections rsrcDir =
    if dirVA rsrcDir == 0 || dirSize rsrcDir == 0 then ([], []) else
    case rvaToOffset (dirVA rsrcDir) sections of
        Nothing -> ([], [])
        Just offset ->
            let rsrcBytesStrict = BL.toStrict $ BL.take (fromIntegral $ dirSize rsrcDir) $ BL.drop (fromIntegral offset) fullBytes
                
                hasAdmin   = BSC.isInfixOf "level=\"requireAdministrator\"" rsrcBytesStrict
                hasHighest = BSC.isInfixOf "level=\"highestAvailable\"" rsrcBytesStrict
                
                manifestEv = (if hasAdmin then ["Manifest requests Administrator privileges"] else []) ++
                             (if hasHighest then ["Manifest requests highest available privileges"] else [])
                             
                fileSize = fromIntegral (BL.length fullBytes) :: Word32
                sizeEv = if dirSize rsrcDir > 3 * 1024 * 1024 && dirSize rsrcDir > (fileSize `div` 3)
                         then ["Resource directory is unusually large (" <> T.pack (show (dirSize rsrcDir `div` 1024)) <> " KB)"]
                         else []
                         
                -- Scan for payloads and prepend location
                payloads = map (\p -> "Resource Directory: " <> p) (detectEmbeddedPayloads rsrcBytesStrict)

            in (manifestEv ++ sizeEv, payloads)

-- | Parse .NET CLI Header to extract version
parseDotNetVersion :: BL.ByteString -> [SectionHeader] -> DataDirectory -> Text
parseDotNetVersion fullBytes sections clrDir =
    if dirVA clrDir == 0 then "" else
    case rvaToOffset (dirVA clrDir) sections of
        Nothing -> ""
        Just clrOffset ->
            -- metadata RVA is 8 bytes into the IMAGE_COR20_HEADER
            let clrChunk = BL.drop (fromIntegral clrOffset + 8) fullBytes
            in case runGetSafe getWord32le clrChunk of
                Just metaRVA | metaRVA /= 0 ->
                    case rvaToOffset metaRVA sections of
                        Just metaOff -> 
                            let metaChunk = BL.drop (fromIntegral metaOff) fullBytes
                                parseMeta = do
                                    sig <- getWord32le
                                    if sig /= 0x424A5342 then fail "Not BSJB" else do -- "BSJB"
                                        skip 8 -- MajorVer, MinorVer, Reserved
                                        len <- getWord32le
                                        verBytes <- getByteString (fromIntegral len)
                                        return $ T.pack (BSC.unpack (BS.takeWhile (/= 0) verBytes))
                            in case runGetSafe parseMeta metaChunk of
                                Just verString -> verString
                                Nothing -> ""
                        Nothing -> ""
                _ -> ""

-- | Analyze first instruction at Entry Point for cross-section JMPs
analyzeEntryPointCFG :: Word32 -> [SectionHeader] -> BS.ByteString -> Bool -> [Text]
analyzeEntryPointCFG epRva sections bytes isDotNet =
    if epRva == 0 || isDotNet then [] else
    case rvaToOffset epRva sections of
        Nothing -> []
        Just epOff ->
            let epBytes = BS.take 6 $ BS.drop (fromIntegral epOff) bytes
                epSec = find (\s -> epRva >= virtualAddress s && epRva < virtualAddress s + max (virtualSize s) (sizeOfRawData s)) sections
            in if BS.length epBytes < 2 then [] else
               let b1 = BS.index epBytes 0
                   targetRva = case b1 of
                       -- E9: JMP rel32
                       0xE9 -> if BS.length epBytes >= 5 then
                                   case runGetSafe getWord32le (BL.fromStrict $ BS.take 4 $ BS.drop 1 epBytes) of
                                        Just rel -> Just $ fromIntegral ((fromIntegral epRva :: Int32) + 5 + (fromIntegral rel :: Int32))
                                        Nothing -> Nothing
                               else Nothing
                       -- EB: JMP rel8
                       0xEB -> let rel8 = fromIntegral (BS.index epBytes 1) :: Int8
                               in Just $ fromIntegral ((fromIntegral epRva :: Int32) + 2 + fromIntegral rel8)
                       _ -> Nothing
               in case targetRva of
                   Nothing -> []
                   Just trva -> 
                       let targetSec = find (\s -> trva >= virtualAddress s && trva < virtualAddress s + max (virtualSize s) (sizeOfRawData s)) sections
                       in case (epSec, targetSec) of
                           (Just s1, Just s2) -> 
                               if secName s1 /= secName s2 
                               then let n1 = T.pack . BSC.unpack . BS.takeWhile (/= 0) $ secName s1
                                        n2 = T.pack . BSC.unpack . BS.takeWhile (/= 0) $ secName s2
                                    in ["Entry Point immediately jumps from " <> n1 <> " into " <> n2 <> ". High probability of a packer stub."]
                               else []
                           _ -> []

-- | Analyze ByteString
analyzePE :: Text -> BS.ByteString -> BL.ByteString -> Bool -> Int -> PEReport
analyzePE name strictBytes lazyBytes doDisasm maxInstr = 
    case runGetOrFail parsePEHeader lazyBytes of
        Left (_, _, errMsg) -> 
            let errCap = Capability "Parser Error" "High" [T.pack errMsg]
            in PEReport name (fromIntegral $ BL.length lazyBytes) "Unknown" "Unknown" 0 0 0 False [] False "" False "" [] 0 "Invalid File" [errCap] [] [] [] []
        Right (_, _, (coff, dirs, sections, optHdrBytes)) ->
            let archText = case machine coff of
                            0x014C -> "x86 (32-bit)"
                            0x8664 -> "x64 (64-bit)"
                            _      -> "Unknown Architecture"

                epRVA = case runGetSafe (skip 16 >> getWord32le) optHdrBytes of
                          Just ep -> ep
                          Nothing -> 0
                          
                subsVal = case runGetSafe (skip 68 >> getWord16le) optHdrBytes of
                          Just s -> s
                          Nothing -> 0
                          
                subsystemText = case subsVal of
                                  1 -> "Native (Driver)"
                                  2 -> "Windows GUI"
                                  3 -> "Windows CUI (Console)"
                                  _ -> "Unknown (" <> T.pack (show subsVal) <> ")"

                -- Overlay calculation & scanning
                fileSizeTotal = fromIntegral (BL.length lazyBytes) :: Word32

                stringCap = if doDisasm then Nothing else Just 2000

                lastSectionEnd = if null sections then 0 else maximum (map (\s -> pointerToRawData s + sizeOfRawData s) sections)
                
                calcOverlay = if fileSizeTotal > lastSectionEnd && lastSectionEnd > 0 then fromIntegral (fileSizeTotal - lastSectionEnd) else 0
                
                -- Scan up to 5/10MB of the overlay
                overlayPayloads = 
                    if calcOverlay > 0
                    then
                        let capBytes = 
                                if fileSizeTotal > 20 * 1024 * 1024 
                                then 5 * 1024 * 1024
                                else 10 * 1024 * 1024
                            ovBytes = BL.toStrict $ BL.take capBytes $ BL.drop (fromIntegral lastSectionEnd) lazyBytes
                        in map (\p -> "Overlay: " <> p) (detectEmbeddedPayloads ovBytes)
                    else []
                            
                -- PROCESS SECTIONS + ENTROPY
                processSection sec = 
                        let name = T.pack . BSC.unpack . BS.takeWhile (/= 0) $ secName sec
                            -- Extract specific bytes for this section from file
                            secBytes = BL.take (fromIntegral $ sizeOfRawData sec) $ BL.drop (fromIntegral $ pointerToRawData sec) lazyBytes
                            entropy = calculateEntropy secBytes
                        in (name, entropy, characteristics sec)
                        
                sectionData = map processSection sections
                
                -- Format section names for UI (e.g., ".text (Entropy: 6.45)")
                formatChars c =
                    let r = if testBit c 30 then "R" else ""
                        w = if testBit c 31 then "W" else ""
                        x = if testBit c 29 then "X" else ""
                    in r ++ w ++ x

                secNamesDisplay =
                    map (\(n, e, c) ->
                        n <> " (Entropy: "
                        <> T.pack (showFFloat (Just 2) e "")
                        <> ", "
                        <> T.pack (formatChars c)
                        <> ")"
                    ) sectionData
                
                allStringsRaw = extractAsciiFast stringCap strictBytes ++ extractWideAsciiFast stringCap strictBytes
                allStrings = Set.toList $ Set.fromList allStringsRaw

                iocCap = if doDisasm then 30 else 15
                extractedIOCs = extractIOCs iocCap allStrings

                imageBaseOffset = if is64Bit then 24 else 28
                imageBase = case runGetSafe (skip imageBaseOffset >> if is64Bit then getWord64le else fromIntegral <$> getWord32le) optHdrBytes of
                                  Just ib -> ib
                                  Nothing -> 0
                
                -- EXTRACT IMPORTS (Data Directory Index 1)
                is64Bit = machine coff == 0x8664
                importDir = if length dirs > 1 then dirs !! 1 else DataDirectory 0 0
                (exeImports, importMap) = parseImports lazyBytes sections is64Bit imageBase importDir

                disasmLimit = if maxInstr <= 0 then 5000 else maxInstr

                disasmOutput = if doDisasm && epRVA /= 0
                               then case find (\s -> epRVA >= virtualAddress s && epRVA < virtualAddress s + max (virtualSize s) (sizeOfRawData s)) sections of
                                    Just epSec -> 
                                        let rawOff = pointerToRawData epSec
                                            vAddr  = virtualAddress epSec
                                            -- Extract up to 200KB of exe section
                                            codeBytes = BS.take (200 * 1024) $ BS.drop (fromIntegral rawOff) strictBytes
                                            -- Calculate abs mem address of this section
                                            secBaseAddr = imageBase + fromIntegral vAddr
                                            
                                        -- Disassemble cap up to 5000 instructions
                                        in disassembleBytes importMap codeBytes secBaseAddr 0 disasmLimit
                                    Nothing -> ["Error: Entry Point RVA not found in any section."]
                               else []

                delayDir = if length dirs > 13 then dirs !! 13 else DataDirectory 0 0
                delayImports = parseDelayImports lazyBytes sections is64Bit delayDir

                allImports = exeImports ++ delayImports

                -- EXTRACT SECURITY DIR (Index 4)
                secDir = if length dirs > 4 then dirs !! 4 else DataDirectory 0 0
                sigDetails = parseSignatureDetails lazyBytes secDir
                hasSignature = not (null sigDetails)

                debugDir = if length dirs > 6 then dirs !! 6 else DataDirectory 0 0
                pdb = extractPdbPath lazyBytes sections debugDir

                -- EXTRACT CLR/.NET DIR (Index 14)
                clrDir = if length dirs > 14 then dirs !! 14 else DataDirectory 0 0
                hasCLR = dirVA clrDir /= 0
                dnVersion = parseDotNetVersion lazyBytes sections clrDir

                -- EXTRACT RESOURCE DIR (Index 2)
                rsrcDir = if length dirs > 2 then dirs !! 2 else DataDirectory 0 0
                (rsrcAnomalies, rsrcPayloads) = parseResources lazyBytes sections rsrcDir

                -- Combine EP anomalies with Resource anomalies
                baseAnomalies = detectAnomalies epRVA sections
                cfgAnomalies = analyzeEntryPointCFG epRVA sections strictBytes hasCLR
                allAnomalies = baseAnomalies ++ rsrcAnomalies ++ cfgAnomalies

                allPayloads = rsrcPayloads ++ overlayPayloads

                tlsDir = if length dirs > 9 then dirs !! 9 else DataDirectory 0 0
                extractedTLSCallbacks = parseTLSCallbacks lazyBytes sections is64Bit imageBase tlsDir

                exportDir = case dirs of (d:_) -> d; [] -> DataDirectory 0 0
                exeExports = parseExports lazyBytes sections exportDir

                opcodeFindings = scanForPatterns strictBytes sections

                -- RUN RULE ENGINE
                (calcScore, calcVerdict, calcCaps, suspStrs) = evaluateRisk sectionData extractedIOCs allImports calcOverlay hasSignature pdb hasCLR extractedTLSCallbacks allAnomalies allPayloads opcodeFindings
            in PEReport
              { fileName          = name
              , fileSize          = fromIntegral (BL.length lazyBytes)
              , architecture      = archText
              , subsystem         = subsystemText
              , entryPoint        = epRVA
              , compileTime       = timeDateStamp coff
              , overlaySize       = calcOverlay
              , isSigned          = hasSignature
              , signatureInfo     = sigDetails
              , isDotNet          = hasCLR
              , dotNetVersion     = dnVersion
              , hasTLS            = not (null extractedTLSCallbacks)
              , pdbPath           = pdb
              , sectionNames      = secNamesDisplay
              , riskScore         = calcScore
              , verdict           = calcVerdict
              , capabilities      = calcCaps
              , exportedFunctions = exeExports
              , suspiciousImports = allImports
              , suspiciousStrings = suspStrs
              , disassembly = disasmOutput
              }
              
-- | Expose function to C/WASM
foreign export ccall "analyze_pe_wasm"
  analyzePeWasm :: Ptr Word8 -> Int -> Int -> Int -> IO CString

-- | function JS will call
analyzePeWasm :: Ptr Word8 -> Int -> Int -> Int -> IO CString
analyzePeWasm ptr len doDisasmInt maxInstr = do
    strictBytes <- BS.packCStringLen (castPtr ptr, len)
    let lazyBytes = BL.fromStrict strictBytes
    
    let doDisasm = doDisasmInt == 1 -- Convert C Int to Haskell Bool
    let disasmLimit = if maxInstr <= 0 then 5000 else maxInstr
    
    -- Pass flag into analyzePE
    let report = analyzePE "browser_upload.exe" strictBytes lazyBytes doDisasm disasmLimit
    
    let jsonString = BL8.unpack (encode report)
    newCString jsonString

-- | Main entry point for CLI testing
main :: IO ()
main = putStrLn "Haskell WASM module initialized."