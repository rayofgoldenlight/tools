// camera_select.c
// Build & link (MinGW example):
//   gcc camera_select.c -o camera_select -lsetupapi -lcfgmgr32
//
// This program:
//  - Enumerates "Camera" class devices
//  - Prints them with indices (1, 2, 3, ...)
//  - Lets the user choose one
//  - Prints the chosen device's InstanceId

#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <stdio.h>
#include <wchar.h>
#include <winreg.h>
#include <string.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")

#define MY_MAX_DEVICE_ID_LEN 1024
#define MAX_FRIENDLYNAME_LEN 512
#define MAX_CLASSNAME_LEN    64
#define MAX_DEVICES          32

typedef struct {
    WCHAR name[MAX_FRIENDLYNAME_LEN];
    WCHAR status[32];
    WCHAR instanceId[MY_MAX_DEVICE_ID_LEN];
} DeviceInfo;

static int mymax(int a, int b) {
    return (a > b) ? a : b;
}

static void print_repeated_wide(WCHAR ch, int count) {
    for (int i = 0; i < count; ++i) {
        wprintf(L"%c", ch);
    }
}

int SelectCameraInstanceId(WCHAR *outInstanceId, size_t maxChars) {
    if (outInstanceId && maxChars > 0) {
        outInstanceId[0] = L'\0';
    }

    HDEVINFO hDevInfo = SetupDiGetClassDevsW(
        NULL, NULL, NULL,
        DIGCF_PRESENT | DIGCF_ALLCLASSES
    );
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"SetupDiGetClassDevsW failed\n");
        return 1;
    }

    DeviceInfo devices[MAX_DEVICES];
    int numDevices = 0;
    DWORD index = 0;

    while (numDevices < MAX_DEVICES) {
        SP_DEVINFO_DATA devInfoData;
        memset(&devInfoData, 0, sizeof(devInfoData));
        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

        if (!SetupDiEnumDeviceInfo(hDevInfo, index++, &devInfoData)) {
            break; // no more devices
        }

        // Get Class name
        WCHAR className[MAX_CLASSNAME_LEN];
        DWORD dataType = 0;
        BOOL isCamera = FALSE;
        if (SetupDiGetDeviceRegistryPropertyW(
                hDevInfo, &devInfoData,
                SPDRP_CLASS,
                &dataType,
                (PBYTE)className,
                sizeof(className),
                NULL)) {
            className[MAX_CLASSNAME_LEN - 1] = L'\0'; // safety
            if (_wcsicmp(className, L"Camera") == 0) {
                isCamera = TRUE;
            }
        }

        if (!isCamera) {
            continue;
        }

        // Get InstanceId
        WCHAR instanceId[MY_MAX_DEVICE_ID_LEN];
        if (!SetupDiGetDeviceInstanceIdW(
                hDevInfo, &devInfoData,
                instanceId,
                MY_MAX_DEVICE_ID_LEN,
                NULL)) {
            continue;
        }

        // Get Name (FriendlyName or Description)
        WCHAR fname[MAX_FRIENDLYNAME_LEN];
        DWORD reqSize = 0;
        BOOL gotName = FALSE;

        // Try FriendlyName first
        SetupDiGetDeviceRegistryPropertyW(
            hDevInfo, &devInfoData,
            SPDRP_FRIENDLYNAME,
            NULL, NULL, 0, &reqSize
        );
        if (reqSize > 0 && reqSize <= sizeof(fname)) {
            if (SetupDiGetDeviceRegistryPropertyW(
                    hDevInfo, &devInfoData,
                    SPDRP_FRIENDLYNAME,
                    NULL, (PBYTE)fname, reqSize, NULL)) {
                gotName = TRUE;
            }
        }

        // Fallback to Description
        if (!gotName) {
            reqSize = 0;
            SetupDiGetDeviceRegistryPropertyW(
                hDevInfo, &devInfoData,
                SPDRP_DEVICEDESC,
                NULL, NULL, 0, &reqSize
            );
            if (reqSize > 0 && reqSize <= sizeof(fname)) {
                if (SetupDiGetDeviceRegistryPropertyW(
                        hDevInfo, &devInfoData,
                        SPDRP_DEVICEDESC,
                        NULL, (PBYTE)fname, reqSize, NULL)) {
                    gotName = TRUE;
                }
            }
        }

        if (!gotName) {
            fname[0] = L'\0';
        }

        // Get Status
        ULONG status = 0, problemNumber = 0;
        CONFIGRET cr = CM_Get_DevNode_Status(
            &status, &problemNumber,
            devInfoData.DevInst, 0
        );
        WCHAR statusStr[32];
        if (cr == CR_SUCCESS) {
            if (problemNumber == 0) {
                wcscpy(statusStr, L"OK");
            } else {
                swprintf(statusStr, 32, L"Error %lu", problemNumber);
            }
        } else {
            wcscpy(statusStr, L"Unknown");
        }

        // Store device
        wcsncpy(devices[numDevices].name, fname, MAX_FRIENDLYNAME_LEN - 1);
        devices[numDevices].name[MAX_FRIENDLYNAME_LEN - 1] = L'\0';

        wcsncpy(devices[numDevices].status, statusStr, 31);
        devices[numDevices].status[31] = L'\0';

        wcsncpy(devices[numDevices].instanceId, instanceId, MY_MAX_DEVICE_ID_LEN - 1);
        devices[numDevices].instanceId[MY_MAX_DEVICE_ID_LEN - 1] = L'\0';

        numDevices++;
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);

    if (numDevices == 0) {
        wprintf(L"No Camera class devices found.\n");
        if (outInstanceId && maxChars > 0) {
            outInstanceId[0] = L'\0';
        }
        return 1;
    }

    // Compute column widths (including index column)
    int indexWidth  = 5;   // "#"
    int nameWidth   = 4;   // "Name"
    int statusWidth = 6;   // "Status"
    int instWidth   = 10;  // "InstanceId"

    for (int j = 0; j < numDevices; ++j) {
        nameWidth   = mymax(nameWidth,  (int)wcslen(devices[j].name));
        statusWidth = mymax(statusWidth, (int)wcslen(devices[j].status));
        instWidth   = mymax(instWidth,  (int)wcslen(devices[j].instanceId));
    }

    // Header
    wprintf(L"%-*ls %-*ls %-*ls %-*ls\n",
            indexWidth,  L"#",
            nameWidth,   L"Name",
            statusWidth, L"Status",
            instWidth,   L"InstanceId");

    // Dashes
    print_repeated_wide(L'-', indexWidth);
    wprintf(L" ");
    print_repeated_wide(L'-', nameWidth);
    wprintf(L" ");
    print_repeated_wide(L'-', statusWidth);
    wprintf(L" ");
    print_repeated_wide(L'-', instWidth);
    wprintf(L"\n");

    // Rows (1-based index for user)
    for (int j = 0; j < numDevices; ++j) {
        wprintf(L"%-*d %-*ls %-*ls %-*ls\n",
                indexWidth,  j + 1,
                nameWidth,   devices[j].name,
                statusWidth, devices[j].status,
                instWidth,   devices[j].instanceId);
    }

    // Prompt user to choose a device
    int choice = 0;
    wprintf(L"\nSelect a camera by number (1-%d): ", numDevices);
    if (wscanf(L"%d", &choice) != 1) {
        fwprintf(stderr, L"\nInvalid input.\n");
        return 1;
    }

    if (choice < 1 || choice > numDevices) {
        fwprintf(stderr, L"\nChoice out of range.\n");
        return 1;
    }

    DeviceInfo *sel = &devices[choice - 1];

    wprintf(L"\nYou selected:\n");
    wprintf(L"  Name       : %ls\n", sel->name);
    wprintf(L"  Status     : %ls\n", sel->status);
    wprintf(L"  InstanceId : %ls\n", sel->instanceId);

    wcsncpy(outInstanceId, sel->instanceId, maxChars - 1);
    outInstanceId[maxChars - 1] = L'\0';
    return 0;
}