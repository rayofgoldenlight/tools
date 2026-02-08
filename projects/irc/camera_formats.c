// camera_formats.c
//
// Exposes:
//   int PrintCameraFormatsForInstanceId(const WCHAR *instanceId);

#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <stdio.h>
#include <wchar.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

// KSCATEGORY_SENSOR_CAMERA
static const WCHAR SENSOR_GUID[] =
    L"{24E552D7-6523-47F7-A647-D3465BF1F5CA}";

static void PrintSubtype(const GUID guid) {
    if (IsEqualGUID(&guid, &MFVideoFormat_NV12))        printf("NV12");
    else if (IsEqualGUID(&guid, &MFVideoFormat_YUY2))  printf("YUY2");
    else if (IsEqualGUID(&guid, &MFVideoFormat_MJPG))  printf("MJPG");
    else if (IsEqualGUID(&guid, &MFVideoFormat_RGB32)) printf("RGB32");
    else if (memcmp(&guid, &MFVideoFormat_L8, sizeof(GUID)) == 0)  printf("L8 (IR)");
    else if (memcmp(&guid, &MFVideoFormat_L16, sizeof(GUID)) == 0) printf("L16 (IR)");
    else printf("{%08lX-...}", guid.Data1);
}

// Build full MF symbolic link:

static void BuildSymbolicLink(const WCHAR *instanceId, WCHAR *out, size_t maxChars) {
    if (!instanceId || !out || maxChars < 16) {
        if (out && maxChars) out[0] = L'\0';
        return;
    }

    WCHAR tmp[1024];
    size_t idx = 0;

    // Copy instanceId to tmp, replacing '\' with '#'
    for (; instanceId[idx] && idx < 1023; ++idx) {
        WCHAR c = instanceId[idx];
        if (c == L'\\') c = L'#';
        tmp[idx] = c;
    }
    tmp[idx] = L'\0';

    // Build full symbolic link
    _snwprintf(out, maxChars, L"\\\\?\\%ls#%ls\\GLOBAL",
               tmp, SENSOR_GUID);
}

int PrintCameraFormatsForInstanceId(const WCHAR *instanceId) {
    if (!instanceId || instanceId[0] == L'\0') {
        fprintf(stderr, "PrintCameraFormatsForInstanceId: empty InstanceId\n");
        return 1;
    }

    WCHAR symbolicLink[2048];
    BuildSymbolicLink(instanceId, symbolicLink, 2048);

    wprintf(L"\nConstructed MF symbolic link:\n  %ls\n\n", symbolicLink);

    HRESULT hr;
    int ret = 0;
    BOOL coInitialized = FALSE;
    BOOL mfStarted = FALSE;
    IMFAttributes *pAttr = NULL;
    IMFMediaSource *pSource = NULL;
    IMFPresentationDescriptor *pPD = NULL;
    IMFStreamDescriptor *pSD = NULL;
    IMFMediaTypeHandler *pHandler = NULL;

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        fprintf(stderr, "CoInitializeEx failed: 0x%08X\n", (unsigned)hr);
        ret = 2;
        goto done;
    }
    coInitialized = TRUE;

    hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    if (FAILED(hr)) {
        fprintf(stderr, "MFStartup failed: 0x%08X\n", (unsigned)hr);
        ret = 3;
        goto done;
    }
    mfStarted = TRUE;

    hr = MFCreateAttributes(&pAttr, 2);
    if (FAILED(hr)) { fprintf(stderr,"MFCreateAttributes failed\n"); ret = 4; goto done; }

    hr = pAttr->lpVtbl->SetGUID(
        pAttr,
        &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
    );
    if (FAILED(hr)) { fprintf(stderr,"SetGUID failed\n"); ret = 5; goto done; }

    hr = pAttr->lpVtbl->SetString(
        pAttr,
        &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
        symbolicLink
    );
    if (FAILED(hr)) {
        fprintf(stderr,"SetString(symbolicLink) failed: 0x%08X\n", (unsigned)hr);
        ret = 6;
        goto done;
    }

    // Open device
    hr = MFCreateDeviceSource(pAttr, &pSource);
    if (FAILED(hr)) {
        fprintf(stderr, "MFCreateDeviceSource failed: 0x%08X\n", (unsigned)hr);
        ret = 7;
        goto done;
    }

    // Read formats
    hr = pSource->lpVtbl->CreatePresentationDescriptor(pSource, &pPD);
    if (FAILED(hr)) { fprintf(stderr,"CreatePresentationDescriptor failed\n"); ret = 8; goto done; }

    BOOL selected = FALSE;
    hr = pPD->lpVtbl->GetStreamDescriptorByIndex(pPD, 0, &selected, &pSD);
    if (FAILED(hr)) { fprintf(stderr,"GetStreamDescriptorByIndex failed\n"); ret = 9; goto done; }

    hr = pSD->lpVtbl->GetMediaTypeHandler(pSD, &pHandler);
    if (FAILED(hr)) { fprintf(stderr,"GetMediaTypeHandler failed\n"); ret = 10; goto done; }

    DWORD count = 0;
    hr = pHandler->lpVtbl->GetMediaTypeCount(pHandler, &count);
    if (FAILED(hr)) { fprintf(stderr,"GetMediaTypeCount failed\n"); ret = 11; goto done; }

    printf("=== Formats for Camera ===\n");
    printf("InstanceId: ");
    wprintf(L"%ls\n", instanceId);
    printf("Symbolic link: ");
    wprintf(L"%ls\n", symbolicLink);
    printf("Found %lu formats:\n\n", (unsigned long)count);

    for (DWORD i = 0; i < count; i++) {
        IMFMediaType *pType = NULL;
        GUID subtype = {0};
        UINT64 frameSize = 0;
        UINT32 w = 0, h = 0;

        hr = pHandler->lpVtbl->GetMediaTypeByIndex(pHandler, i, &pType);
        if (FAILED(hr) || !pType) continue;

        pType->lpVtbl->GetGUID(pType, &MF_MT_SUBTYPE, &subtype);
        pType->lpVtbl->GetUINT64(pType, &MF_MT_FRAME_SIZE, &frameSize);

        w = (UINT32)(frameSize >> 32);
        h = (UINT32)(frameSize & 0xFFFFFFFF);

        printf("[%lu] ", (unsigned long)i);
        PrintSubtype(subtype);
        printf(" - %ux%u\n", w, h);

        pType->lpVtbl->Release(pType);
    }

done:
    if (pHandler)  pHandler->lpVtbl->Release(pHandler);
    if (pSD)       pSD->lpVtbl->Release(pSD);
    if (pPD)       pPD->lpVtbl->Release(pPD);
    if (pSource)   pSource->lpVtbl->Release(pSource);
    if (pAttr)     pAttr->lpVtbl->Release(pAttr);

    if (mfStarted) {
        MFShutdown();
    }
    if (coInitialized) {
        CoUninitialize();
    }

    return ret;
}