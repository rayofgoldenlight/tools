// ir_capture.c
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <stdio.h>
#include <wchar.h>
#include <vfw.h>

#pragma comment(lib, "avifil32.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

#pragma pack(push, 1)
typedef struct _MY_AVIMAINHEADER {
    DWORD dwMicroSecPerFrame;
    DWORD dwMaxBytesPerSec;
    DWORD dwPaddingGranularity;
    DWORD dwFlags;
    DWORD dwTotalFrames;
    DWORD dwInitialFrames;
    DWORD dwStreams;
    DWORD dwSuggestedBufferSize;
    DWORD dwWidth;
    DWORD dwHeight;
    DWORD dwReserved[4];
} MY_AVIMAINHEADER;

typedef struct _MY_AVISTREAMHEADER {
    DWORD fccType;
    DWORD fccHandler;
    DWORD dwFlags;
    WORD  wPriority;
    WORD  wLanguage;
    DWORD dwInitialFrames;
    DWORD dwScale;
    DWORD dwRate;
    DWORD dwStart;
    DWORD dwLength;
    DWORD dwSuggestedBufferSize;
    DWORD dwQuality;
    DWORD dwSampleSize;
    RECT  rcFrame;
} MY_AVISTREAMHEADER;
#pragma pack(pop)

// Same sensor GUID used elsewhere
static const WCHAR SENSOR_GUID[] =
    L"{24E552D7-6523-47F7-A647-D3465BF1F5CA}";

// Build MF symbolic link from InstanceId
static void BuildSymbolicLink(const WCHAR *instanceId, WCHAR *out, size_t maxChars) {
    if (!instanceId || !out || maxChars < 16) {
        if (out && maxChars) out[0] = L'\0';
        return;
    }

    WCHAR tmp[1024];
    size_t idx = 0;

    for (; instanceId[idx] && idx < 1023; ++idx) {
        WCHAR c = instanceId[idx];
        if (c == L'\\') c = L'#';
        tmp[idx] = c;
    }
    tmp[idx] = L'\0';

    _snwprintf(out, maxChars, L"\\\\?\\%ls#%ls\\GLOBAL",
               tmp, SENSOR_GUID);
}

/* ----------------- GUI globals and WndProc ----------------- */

static volatile BOOL g_running = TRUE;

static LRESULT CALLBACK IrWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DESTROY:
        g_running = FALSE;
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}

#include "ir_capture.h"
#include <stdlib.h>
#include <vfw.h>
#pragma comment(lib, "avifil32.lib")

// AVI writer using AVIFile API

typedef struct Recorder_ {
    WCHAR      filename[260];
    UINT32     width;
    UINT32     height;
    UINT32     fps;
    unsigned long frameCount;
    int        active;

    PAVIFILE   pFile;
    PAVISTREAM pStream;

    BYTE      *frameBuf;      // bottom-up ARGB buffer for AVI
    LONG       bytesPerFrame;

    double     targetDurationSec;
} Recorder;

static int Recorder_Begin(Recorder **ppRec,
                          const WCHAR *filename,
                          UINT32 width, UINT32 height,
                          UINT32 fps,
                          double targetDurationSec)
{
    if (!ppRec || !filename || !filename[0]) return 1;

    HRESULT hr;

    WCHAR finalName[260];
    wcsncpy(finalName, filename, 259);
    finalName[259] = L'\0';

    // If there is no '.' in the name, append ".avi"
    WCHAR *dot = wcsrchr(finalName, L'.');
    if (!dot) {
        size_t len = wcslen(finalName);
        if (len + 4 < 260) {   // room for ".avi" and null
            wcscat(finalName, L".avi");
        }
    }

    Recorder *r = (Recorder*)calloc(1, sizeof(Recorder));
    if (!r) return 1;

    wcsncpy(r->filename, finalName, 259);
    r->filename[259] = L'\0';
    r->width  = width;
    r->height = height;
    r->fps    = fps ? fps : 30;
    r->frameCount = 0;
    r->active = 0; // set true only if everything succeeds
    r->targetDurationSec = targetDurationSec;

    r->bytesPerFrame = (LONG)(width * height * 4);
    r->frameBuf = (BYTE*)malloc(r->bytesPerFrame);
    if (!r->frameBuf) {
        free(r);
        return 1;
    }

    // Init AVIFile library
    AVIFileInit();

    // Open AVI file for writing (Unicode version)
    hr = AVIFileOpenW(&r->pFile, finalName,
                      OF_WRITE | OF_CREATE,
                      NULL);
    if (hr != 0 || !r->pFile) {
        wprintf(L"[REC] AVIFileOpen failed (hr=0x%08X)\n", (unsigned)hr);
        free(r->frameBuf);
        free(r);
        AVIFileExit();
        return 1;
    }

    // Create video stream
    AVISTREAMINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.fccType = streamtypeVIDEO;
    si.fccHandler = 0; // uncompressed
    si.dwScale = 1;
    si.dwRate  = r->fps;  // frames per second
    si.dwSuggestedBufferSize = (DWORD)r->bytesPerFrame;
    SetRect(&si.rcFrame, 0, 0, (int)width, (int)height);

    hr = AVIFileCreateStreamW(r->pFile, &r->pStream, &si);

    if (hr != 0 || !r->pStream) {
        wprintf(L"[REC] AVIFileCreateStream failed (hr=0x%08X)\n", (unsigned)hr);
        AVIFileRelease(r->pFile);
        AVIFileExit();
        free(r->frameBuf);
        free(r);
        return 1;
    }

    // Set stream format: 32‑bit BI_RGB
    BITMAPINFOHEADER bih;
    ZeroMemory(&bih, sizeof(bih));
    bih.biSize        = sizeof(BITMAPINFOHEADER);
    bih.biWidth       = (LONG)width;
    bih.biHeight      = (LONG)height;  // bottom‑up
    bih.biPlanes      = 1;
    bih.biBitCount    = 32;
    bih.biCompression = BI_RGB;
    bih.biSizeImage   = (DWORD)r->bytesPerFrame;

    hr = AVIStreamSetFormat(r->pStream, 0, &bih, sizeof(bih));
    if (hr != 0) {
        wprintf(L"[REC] AVIStreamSetFormat failed (hr=0x%08X)\n", (unsigned)hr);
        AVIStreamRelease(r->pStream);
        AVIFileRelease(r->pFile);
        AVIFileExit();
        free(r->frameBuf);
        free(r);
        return 1;
    }

    r->active = 1;
    wprintf(L"[REC] AVI recording started: '%ls' (%ux%u @ %u fps)\n",
            r->filename, r->width, r->height, r->fps);

    *ppRec = r;
    return 0;
}

static int Recorder_WriteFrame(Recorder *r,
                               const BYTE *pARGB,
                               UINT32 width, UINT32 height)
{
    if (!r || !r->active || !pARGB) return 1;
    if (width != r->width || height != r->height) return 1;
    if (!r->pStream || !r->frameBuf) return 1;

    // ----- Convert top-down ARGB to bottom-up ARGB -----
    const DWORD *src = (const DWORD*)pARGB;      // your preview buffer
    DWORD *dst       = (DWORD*)r->frameBuf;      // AVI bottom-up

    for (UINT32 y = 0; y < height; ++y) {
        const DWORD *srcLine = src + y * width;
        DWORD *dstLine = dst + (height - 1 - y) * width;
        memcpy(dstLine, srcLine, width * 4);
    }

    // ----- Write frame to AVI -----
    LONG result = AVIStreamWrite(
        r->pStream,
        (LONG)r->frameCount,   // start frame
        1,                     // number of frames
        r->frameBuf,           // buffer
        r->bytesPerFrame,      // buffer size
        0,                     // flags
        NULL, NULL
    );

    if (result != 0) {
        wprintf(L"[REC] AVIStreamWrite failed at frame %lu (code %ld)\n",
                r->frameCount, result);
        return 1;
    }

    r->frameCount++;
    return 0;
}

// Resizes the video after recording
static void Recorder_FixDurationHeader(const Recorder *r)
{
    if (!r || r->frameCount == 0 || r->targetDurationSec <= 0.0) {
        return;
    }

    // Compute desired microseconds per frame and FPS
    double microPerFrameD = (r->targetDurationSec * 1000000.0) /
                            (double)r->frameCount;
    DWORD microPerFrame = (DWORD)(microPerFrameD + 0.5);

    double desiredFps = (double)r->frameCount / r->targetDurationSec;

    FILE *fp = _wfopen(r->filename, L"rb+");
    if (!fp) {
        wprintf(L"[REC] Failed to reopen '%ls' to fix header.\n", r->filename);
        return;
    }

    long foundPos = -1;
    unsigned char tag[4];

    while (fread(tag, 1, 4, fp) == 4) {
        if (memcmp(tag, "avih", 4) == 0) {
            foundPos = ftell(fp);  // position just after "avih"
            break;
        }
        // Slide window by 1 byte
        if (fseek(fp, -3, SEEK_CUR) != 0) {
            break;
        }
    }

    if (foundPos < 0) {
        wprintf(L"[REC] 'avih' not found in '%ls'; cannot adjust main timing.\n",
                r->filename);
        fclose(fp);
        return;
    }

    // Read chunk size
    DWORD chunkSize = 0;
    if (fread(&chunkSize, sizeof(chunkSize), 1, fp) != 1) {
        fclose(fp);
        return;
    }

    // Read main AVI header from the chunk data
    MY_AVIMAINHEADER hdr;
    if (fread(&hdr, sizeof(hdr), 1, fp) != 1) {
        fclose(fp);
        return;
    }

    hdr.dwMicroSecPerFrame = microPerFrame;
    hdr.dwTotalFrames      = r->frameCount; // keep consistent

    // Seek back to start of header data and write patched header
    if (fseek(fp, -(LONG)sizeof(hdr), SEEK_CUR) != 0) {
        fclose(fp);
        return;
    }

    if (fwrite(&hdr, sizeof(hdr), 1, fp) != 1) {
        wprintf(L"[REC] Failed to write patched main header to '%ls'.\n",
                r->filename);
        fclose(fp);
        return;
    }

    // Rewind and search for 'strh'
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return;
    }

    long foundStrh = -1;
    while (fread(tag, 1, 4, fp) == 4) {
        if (memcmp(tag, "strh", 4) == 0) {
            foundStrh = ftell(fp);  // just after 'strh'
            break;
        }
        if (fseek(fp, -3, SEEK_CUR) != 0) {
            break;
        }
    }

    if (foundStrh >= 0) {
        DWORD strhChunkSize = 0;
        if (fread(&strhChunkSize, sizeof(strhChunkSize), 1, fp) == 1) {
            MY_AVISTREAMHEADER sh;
            if (fread(&sh, sizeof(sh), 1, fp) == 1) {
                // Only adjust if this is a video stream
                // 'vids' = 'sdiv' in little-endian; easier to compare as FOURCC
                if (sh.fccType == streamtypeVIDEO) {
                    const DWORD scale = 1000;  // ticks per second
                    DWORD rate = (DWORD)(desiredFps * scale + 0.5);

                    sh.dwScale  = scale;
                    sh.dwRate   = rate;
                    sh.dwLength = r->frameCount;

                    if (fseek(fp, -(LONG)sizeof(sh), SEEK_CUR) == 0) {
                        if (fwrite(&sh, sizeof(sh), 1, fp) != 1) {
                            wprintf(L"[REC] Failed to write patched stream header to '%ls'.\n",
                                    r->filename);
                            // fall through; main header already patched
                        }
                    }
                }
            }
        }
    } else {
        wprintf(L"[REC] 'strh' (stream header) not found in '%ls'; stream fps unchanged.\n",
                r->filename);
    }

    fclose(fp);

    wprintf(L"[REC] Retimed '%ls': %lu frames over %.2f s -> ~%.3f fps\n",
            r->filename,
            r->frameCount,
            r->targetDurationSec,
            desiredFps);
}

static void Recorder_End(Recorder *r)
{
    if (!r) return;

    wprintf(L"[REC] Finishing '%ls', %lu frames written.\n",
            r->filename, r->frameCount);

    if (r->pStream) {
        AVIStreamRelease(r->pStream);
        r->pStream = NULL;
    }
    if (r->pFile) {
        AVIFileRelease(r->pFile);
        r->pFile = NULL;
    }

    AVIFileExit();

    // Patch AVI main header to match target duration
    Recorder_FixDurationHeader(r);

    if (r->frameBuf) {
        free(r->frameBuf);
        r->frameBuf = NULL;
    }

    free(r);
}

// Helper to read a UINT64 "ratio" attribute (hi 32 bits = num, lo 32 bits = den)
static HRESULT GetAttributeRatio(IMFAttributes *pAttr,
                                 const GUID *key,
                                 UINT32 *pNum,
                                 UINT32 *pDen)
{
    if (!pAttr || !key || !pNum || !pDen) return E_POINTER;

    UINT64 packed = 0;
    HRESULT hr = pAttr->lpVtbl->GetUINT64(pAttr, key, &packed);
    if (FAILED(hr)) return hr;

    *pNum = (UINT32)(packed >> 32);
    *pDen = (UINT32)(packed & 0xFFFFFFFF);

    return (*pDen == 0) ? E_FAIL : S_OK;
}

/* ----------------- Main preview function ----------------- */

int PreviewIrCamera(const WCHAR *instanceId,
                    double recordSeconds,
                    const WCHAR *outputFile) {
    if (!instanceId || instanceId[0] == L'\0') {
        fprintf(stderr, "PreviewIrCamera: empty InstanceId\n");
        return 1;
    }

    HRESULT hr = S_OK;

    IMFAttributes      *pAttributes    = NULL;
    IMFMediaSource     *pSource        = NULL;
    IMFSourceReader    *pReader        = NULL;
    IMFMediaType       *pCurrentType   = NULL;
    IMFSample          *pSample        = NULL;
    IMFMediaBuffer     *pBuffer        = NULL;

    UINT32 width = 0, height = 0;

    WCHAR symbolicLink[2048];
    BuildSymbolicLink(instanceId, symbolicLink, 2048);

    wprintf(L"\n=== IR Live Preview ===\n");
    wprintf(L"[IR] Using symbolic link:\n  %ls\n", symbolicLink);

    // Initialize COM + MF
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        fprintf(stderr, "CoInitializeEx failed: 0x%08X\n", (unsigned)hr);
        goto done;
    }

    hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    if (FAILED(hr)) {
        fprintf(stderr, "MFStartup failed: 0x%08X\n", (unsigned)hr);
        goto done;
    }

    // Open device
    hr = MFCreateAttributes(&pAttributes, 2);
    if (FAILED(hr)) { fprintf(stderr, "MFCreateAttributes failed\n"); goto done; }

    hr = pAttributes->lpVtbl->SetGUID(
        pAttributes,
        &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
    );
    if (FAILED(hr)) { fprintf(stderr, "SetGUID failed\n"); goto done; }

    hr = pAttributes->lpVtbl->SetString(
        pAttributes,
        &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
        symbolicLink
    );
    if (FAILED(hr)) {
        fprintf(stderr, "SetString(symbolicLink) failed: 0x%08X\n", (unsigned)hr);
        goto done;
    }

    hr = MFCreateDeviceSource(pAttributes, &pSource);
    if (FAILED(hr)) {
        fprintf(stderr, "MFCreateDeviceSource failed: 0x%08X\n", (unsigned)hr);
        goto done;
    }

    // Source Reader
    hr = MFCreateSourceReaderFromMediaSource(pSource, NULL, &pReader);
    if (FAILED(hr)) {
        fprintf(stderr, "MFCreateSourceReaderFromMediaSource failed: 0x%08X\n", (unsigned)hr);
        goto done;
    }

    {
        IMFMediaType *pType = NULL;
        hr = MFCreateMediaType(&pType);
        if (SUCCEEDED(hr)) {
            hr = pType->lpVtbl->SetGUID(pType, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
        }
        if (SUCCEEDED(hr)) {
            hr = pType->lpVtbl->SetGUID(pType, &MF_MT_SUBTYPE, &MFVideoFormat_L8);
        }
        if (SUCCEEDED(hr)) {
            hr = pReader->lpVtbl->SetCurrentMediaType(
                pReader,
                MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                NULL,
                pType
            );
        }
        if (pType) pType->lpVtbl->Release(pType);
        if (FAILED(hr)) {
            fprintf(stderr, "Failed to set L8 media type (0x%08X). Continuing with default.\n", (unsigned)hr);
        }
    }

    // Get width/height
    hr = pReader->lpVtbl->GetCurrentMediaType(
        pReader,
        MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        &pCurrentType
    );
    if (FAILED(hr)) {
        fprintf(stderr, "GetCurrentMediaType failed: 0x%08X\n", (unsigned)hr);
        goto done;
    }

    UINT64 frameSize = 0;
    hr = pCurrentType->lpVtbl->GetUINT64(
        pCurrentType,
        &MF_MT_FRAME_SIZE,
        &frameSize
    );
    if (FAILED(hr)) {
        fprintf(stderr, "GetUINT64(MF_MT_FRAME_SIZE) failed: 0x%08X\n", (unsigned)hr);
        goto done;
    }

    width  = (UINT32)(frameSize >> 32);
    height = (UINT32)(frameSize & 0xFFFFFFFF);

    UINT32 fpsInt = 30;
    UINT32 frNum = 0, frDen = 0;
    hr = GetAttributeRatio((IMFAttributes*)pCurrentType,
                        &MF_MT_FRAME_RATE, &frNum, &frDen);
    if (SUCCEEDED(hr)) {
        double fps = (double)frNum / (double)frDen;
        fpsInt = (UINT32)(fps + 0.5);
    }
    printf("[IR] Resolution: %ux%u, approx %u fps\n", width, height, fpsInt);

    BOOL       recordingEnabled = (recordSeconds > 0.0 && outputFile && outputFile[0]);
    BOOL       recordingActive  = FALSE;
    ULONGLONG  recordStartMs    = 0;
    Recorder  *pRec             = NULL;

    if (recordingEnabled) {
        if (Recorder_Begin(&pRec, outputFile, width, height, fpsInt, recordSeconds) == 0) {
            recordingActive  = TRUE;
            recordStartMs    = GetTickCount64();
        } else {
            wprintf(L"[REC] failed to start recorder; recording disabled.\n");
            recordingEnabled = FALSE;
        }
    }

    /* ---------- Create a simple window for display ---------- */

    HINSTANCE hInst = GetModuleHandle(NULL);
    const WCHAR *CLASS_NAME = L"IRPreviewWindow";

    WNDCLASSW wc = {0};
    wc.lpfnWndProc   = IrWndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);

    ATOM atom = RegisterClassW(&wc);
    if (!atom) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            fprintf(stderr, "RegisterClassW failed, error %lu\n", err);
            goto done;
        }
    }

    int winW = (int)width;
    int winH = (int)height;

    HWND hWnd = CreateWindowW(
        CLASS_NAME, L"IR Preview",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        winW + 16, winH + 39,  // rough frame size compensation
        NULL, NULL, hInst, NULL
    );
    
    if (!hWnd) {
        fprintf(stderr, "CreateWindowW failed\n");
        goto done;
    }

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    // Create DIB section for 32bpp RGB display
    HDC hWndDC = GetDC(hWnd);
    HDC hMemDC = CreateCompatibleDC(hWndDC);
    void *pBitmapData = NULL;

    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = (LONG)width;
    bmi.bmiHeader.biHeight      = -(LONG)height;  // top‑down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HBITMAP hBitmap = CreateDIBSection(
        hWndDC, &bmi, DIB_RGB_COLORS, &pBitmapData, NULL, 0
    );
    if (!hBitmap || !pBitmapData) {
        fprintf(stderr, "CreateDIBSection failed\n");
        ReleaseDC(hWnd, hWndDC);
        goto done;
    }

    HGDIOBJ oldBmp = SelectObject(hMemDC, hBitmap);
    ReleaseDC(hWnd, hWndDC);

    printf("[IR] Showing live preview. Close window to stop.\n");

    g_running = TRUE;

    /* ---------- Main loop: messages + frame capture ---------- */
    while (g_running) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_running = FALSE;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!g_running) break;

        // Read next frame
        DWORD   streamIndex = 0;
        DWORD   flags       = 0;
        LONGLONG llTimeStamp = 0;

        if (pSample) { pSample->lpVtbl->Release(pSample); pSample = NULL; }
        if (pBuffer) { pBuffer->lpVtbl->Release(pBuffer); pBuffer = NULL; }

        hr = pReader->lpVtbl->ReadSample(
            pReader,
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,
            &streamIndex,
            &flags,
            &llTimeStamp,
            &pSample
        );
        if (FAILED(hr)) {
            fprintf(stderr, "ReadSample failed: 0x%08X\n", (unsigned)hr);
            break;
        }

        if (!pSample) {
            // no frame; small sleep to avoid busy spin
            Sleep(5);
            continue;
        }

        hr = pSample->lpVtbl->ConvertToContiguousBuffer(pSample, &pBuffer);
        if (FAILED(hr)) {
            fprintf(stderr, "ConvertToContiguousBuffer failed: 0x%08X\n", (unsigned)hr);
            break;
        }

        BYTE *pData = NULL;
        DWORD cbData = 0;
        hr = pBuffer->lpVtbl->Lock(pBuffer, &pData, NULL, &cbData);
        if (FAILED(hr)) {
            fprintf(stderr, "Lock failed: 0x%08X\n", (unsigned)hr);
            break;
        }

        BYTE  *src  = pData;                 // raw L8 from camera
        DWORD *dest = (DWORD*)pBitmapData;   // ARGB for preview/record

        UINT32 numPixels = width * height;
        UINT32 sampleCount = (cbData < numPixels) ? cbData : numPixels;

        // ---- Detect "almost black" frame ----
        // Threshold: pixel > 10 is considered "non‑black".
        // If fewer than 1% of pixels are non‑black, we treat the frame as black.
        const BYTE  intensityThreshold = 10;
        UINT32      nonBlackPixels     = 0;

        for (UINT32 i = 0; i < sampleCount; ++i) {
            if (src[i] > intensityThreshold) {
                ++nonBlackPixels;
            }
            BYTE v = src[i]; // Convert L8 -> 32‑bit ARGB in pBitmapData (for preview/record)
            dest[i] = 0xFF000000 | (v << 16) | (v << 8) | v; // ARGB
        }

        BOOL isBlackFrame = (nonBlackPixels < sampleCount / 100); // <1% bright pixels

        pBuffer->lpVtbl->Unlock(pBuffer);

        // --- Recording timing and frame dispatch (wall‑clock based) ---
        if (recordingActive) {
            ULONGLONG nowMs = GetTickCount64();
            double elapsed = (double)(nowMs - recordStartMs) / 1000.0;

            if (elapsed <= recordSeconds) {
                // Only write non‑black frames
                if (!isBlackFrame) {
                    Recorder_WriteFrame(pRec,
                                        (const BYTE*)pBitmapData,
                                        width, height);
                }
            } else {
                recordingActive = FALSE;
                ULONGLONG postStartMs = GetTickCount64();
                Recorder_End(pRec);
                ULONGLONG postEndMs = GetTickCount64();
                double postSeconds = (double)(postEndMs - postStartMs) / 1000.0;
                pRec = NULL;
                wprintf(L"[REC] reached %.2f seconds; recording stopped.\n",
                        recordSeconds);
                wprintf(L"[REC] Post-record processing (file close + resize) took %.3f seconds.\n",
                        postSeconds);
            }
        }

        // Blit to window
        HDC hDC = GetDC(hWnd);
        BitBlt(hDC, 0, 0, winW, winH, hMemDC, 0, 0, SRCCOPY);
        ReleaseDC(hWnd, hDC);
    }

    // Cleanup GDI
    if (hMemDC) {
        SelectObject(hMemDC, oldBmp);
        DeleteDC(hMemDC);
    }
    if (hBitmap) DeleteObject(hBitmap);

done:
    if (pBuffer)      pBuffer->lpVtbl->Release(pBuffer);
    if (pSample)      pSample->lpVtbl->Release(pSample);
    if (pCurrentType) pCurrentType->lpVtbl->Release(pCurrentType);
    if (pReader)      pReader->lpVtbl->Release(pReader);
    if (pSource)      pSource->lpVtbl->Release(pSource);
    if (pAttributes)  pAttributes->lpVtbl->Release(pAttributes);
    if (recordingActive && pRec) {
        Recorder_End(pRec);
        pRec = NULL;    
    }

    MFShutdown();
    CoUninitialize();

    printf("[IR] Preview finished.\n");
    return FAILED(hr) ? 1 : 0;
}