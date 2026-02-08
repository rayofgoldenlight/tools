// main.c
// Orchestrator:
//   1) Runs camera selection (SetupAPI) to get an InstanceId
//   2) Uses that InstanceId with Media Foundation to print supported formats
//   3) Uses the format to then allow the user to create a recording/preview

#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <wchar.h>
#include <stdio.h>

int SelectCameraInstanceId(WCHAR *outInstanceId, size_t maxChars);
int PrintCameraFormatsForInstanceId(const WCHAR *instanceId);
int PreviewIrCamera(const WCHAR *instanceId,
                    double recordSeconds,
                    const WCHAR *outputFile);

int main(void) {
    WCHAR instanceId[1024];

    // Part 1: let user choose a camera, get its InstanceId
    int ret = SelectCameraInstanceId(instanceId,
                                     sizeof(instanceId) / sizeof(WCHAR));
    if (ret != 0) {
        fwprintf(stderr,
                 L"\nCamera selection failed (code %d). Aborting.\n",
                 ret);
        return ret;
    }

    wprintf(L"\nSelected camera InstanceId:\n  %ls\n", instanceId);
    wprintf(L"\nInspecting supported formats via Media Foundation...\n\n");

    // Part 2: use that InstanceId to query MF formats
    ret = PrintCameraFormatsForInstanceId(instanceId);
    if (ret != 0) {
        fwprintf(stderr,
                 L"\nFormat inspection failed (code %d).\n",
                 ret);
    }

    // Part 3: live IR preview + optional recording
    wprintf(L"\nDo you want to preview the IR camera live? (y/n): ");
    WCHAR answer = 0;
    if (wscanf(L" %c", &answer) == 1 &&
        (answer == L'y' || answer == L'Y')) {

        double recordSeconds = 0.0;
        WCHAR outFile[260] = L"";

        wprintf(L"\nDo you want to record to AVI while previewing? (y/n): ");
        WCHAR recAns = 0;
        if (wscanf(L" %c", &recAns) == 1 &&
            (recAns == L'y' || recAns == L'Y')) {

            wprintf(L"Enter duration in seconds: ");
            if (wscanf(L"%lf", &recordSeconds) != 1 || recordSeconds <= 0.0) {
                wprintf(L"Invalid duration; recording disabled.\n");
                recordSeconds = 0.0;
            } else {
                wprintf(L"Enter output AVI filename (e.g. ir_test.avi): ");
                if (wscanf(L" %259ls", outFile) != 1) {
                    wprintf(L"Invalid filename; recording disabled.\n");
                    recordSeconds = 0.0;
                    outFile[0] = L'\0';
                }
            }
        }

        int irRet = PreviewIrCamera(instanceId,
                                    recordSeconds,
                                    outFile[0] ? outFile : NULL);
        if (irRet != 0) {
            fwprintf(stderr,
                     L"\nIR preview/recording failed (code %d).\n",
                     irRet);
            return irRet;
        }
    }

    return ret;
}