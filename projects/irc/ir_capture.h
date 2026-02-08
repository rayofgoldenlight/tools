// ir_capture.h
#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// recordSeconds == 0 or outputFile == NULL -> no recording
int PreviewIrCamera(const WCHAR *instanceId,
                    double recordSeconds,
                    const WCHAR *outputFile);

#ifdef __cplusplus
}
#endif