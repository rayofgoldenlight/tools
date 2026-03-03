#define _WIN32_WINNT 0x0600
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string.h>

//Compile this in MSYS2 MINGW64
//Do:
//gcc client_discover.c client_res.o -O2 -o LocalChat.exe -lws2_32 -lgdi32 -luser32 -ladvapi32 -mwindows

#pragma comment(lib, "ws2_32.lib")

#include <time.h>

#include <wincrypt.h>

#ifndef CALG_SHA_256
#define CALG_SHA_256 0x800c
#endif
#ifndef PROV_RSA_AES
#define PROV_RSA_AES 24
#endif

#define MAX_SERVERS 10

struct ServerInfo {
    char ip[INET_ADDRSTRLEN];
    int tcp_port;
    long long cur, soft, hard;
    int pin_required;
};

static struct ServerInfo servers[MAX_SERVERS];
static int server_count = 0;

HWND hChatBox, hInputBox, hSendBtn, hByteLabel;
HWND hUserList; 

// User mapping array
struct { int id; char name[32]; } online_users[100];
int online_user_count = 0;

int isDarkMode = 0; // Starts in Light mode

// Color references
COLORREF bg_color, text_color, chat_bg, accent_color;
HBRUSH hBgBrush, hChatBrush, hAccentBrush;

SOCKET global_tcp_sock; 
long long global_soft_cap = 0;

char initial_history[8192] = ""; 
char last_sent_message[2048] = "";

void AppendTextToChat(const char* text);
void ShowHelpDialog(HWND parent);
void SetTheme(HWND hwnd);

void compute_sha256_hex(const char* input, int len, char* output) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    BYTE hash[32];
    DWORD hashLen = 32;

    CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT);
    CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash);
    CryptHashData(hHash, (const BYTE*)input, len, 0);
    CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0);
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);

    for (int i = 0; i < 32; i++)
        sprintf(output + i * 2, "%02x", hash[i]);
    output[64] = '\0';
}


static int parse_beacon(const char *msg,
                        int *tcp_port,
                        long long *cur,
                        long long *soft,
                        long long *hard,
                        int *pin_required)
{
    // Expected: LOCALCHAT|port|cur|soft|hard|pin_flag
    char tag[32];
    int port = 0, pin = 0;
    long long c=0, s=0, h=0;

    if (sscanf(msg, "%31[^|]|%d|%lld|%lld|%lld|%d", tag, &port, &c, &s, &h, &pin) != 6)
        return 0;
    if (strcmp(tag, "LOCALCHAT") != 0)
        return 0;

    *tcp_port = port;
    *cur = c; *soft = s; *hard = h;
    *pin_required = pin;
    return 1;
}

HWND hServerList, hConnectBtn;
HWND hPortInput, hScanBtn;
int discovery_active = 1;
int discovery_port = 9001;
WNDPROC OldPortEditProc;

// Returns 1 if a new server was added, 0 if it already existed
static int add_or_update_server(const char* ip, int port, long long cur, long long soft, long long hard, int pin_required) {
    for (int i = 0; i < server_count; i++) {
        if (strcmp(servers[i].ip, ip) == 0 && servers[i].tcp_port == port) {
            servers[i].cur = cur; servers[i].soft = soft; servers[i].hard = hard;
            servers[i].pin_required = pin_required;
            return 0;
        }
    }
    if (server_count < MAX_SERVERS) {
        strcpy(servers[server_count].ip, ip);
        servers[server_count].tcp_port = port;
        servers[server_count].cur = cur;
        servers[server_count].soft = soft;
        servers[server_count].hard = hard;
        servers[server_count].pin_required = pin_required;
        server_count++;
        return 1;
    }
    return 0;
}

WNDPROC OldEditProc; // Stores the original text box behavior

void SendChatMessage() {
    char msg[2048];
    GetWindowText(hInputBox, msg, sizeof(msg));
    
    if (strlen(msg) == 0) return;

    // Save for the Up Arrow shortcut. 
    // Ignore setting commands as the "last message", EXCEPT if it's an edit.
    if (strncmp(msg, "/edit ", 6) == 0) {
        strncpy(last_sent_message, msg + 6, sizeof(last_sent_message) - 1);
    } else if (msg[0] != '/') {
        strncpy(last_sent_message, msg, sizeof(last_sent_message) - 1);
    }

    char single_line[2048];
    int j = 0;
    
    // Convert newlines to \x0B to keep the TCP packet as a single line
    for (int i = 0; msg[i] != '\0' && j < sizeof(single_line) - 2; i++) {
        if (msg[i] == '\r' && msg[i+1] == '\n') {
            single_line[j++] = '\x0B'; 
            i++; // skip the \n
        } else if (msg[i] == '\n') {
            single_line[j++] = '\x0B';
        } else {
            single_line[j++] = msg[i];
        }
    }
    single_line[j] = '\0';

    if (strlen(single_line) > 0) {
        strcat(single_line, "\n");
        send(global_tcp_sock, single_line, strlen(single_line), 0);
        SetWindowText(hInputBox, ""); // Instantly clear text box
    }
}

// Intercepts keys pressed inside the text box
LRESULT CALLBACK InputEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            if (GetKeyState(VK_SHIFT) & 0x8000) {
                return CallWindowProc(OldEditProc, hwnd, msg, wParam, lParam);
            } else {
                SendChatMessage();
                return 0; // Handled keydown
            }
        } else if (wParam == VK_UP) {
            // If the text box is empty, pull the last message to edit
            if (GetWindowTextLength(hwnd) == 0 && strlen(last_sent_message) > 0) {
                char edit_cmd[2048];
                snprintf(edit_cmd, sizeof(edit_cmd), "/edit %s", last_sent_message);
                SetWindowText(hwnd, edit_cmd);
                
                // Move cursor to the very end of the text
                int len = GetWindowTextLength(hwnd);
                SendMessage(hwnd, EM_SETSEL, (WPARAM)len, (LPARAM)len);
                return 0; // Prevent default UP arrow behavior
            }
        }
        // Notice we REMOVED the Ctrl+A check from here. 
        // We let the keydown pass through naturally so the control stays happy.
    } 
    else if (msg == WM_CHAR) {
        if (wParam == VK_RETURN) {
            // If Shift is NOT held, eat the WM_CHAR so it doesn't leave a residual newline
            if (!(GetKeyState(VK_SHIFT) & 0x8000)) {
                return 0; 
            }
        } else if (wParam == 1) {
            // wParam 1 is the ASCII control character for Ctrl+A.
            // Select all text here, then return 0 to eat the char and stop the beep!
            SendMessage(hwnd, EM_SETSEL, 0, -1);
            return 0;
        }
    }
    
    return CallWindowProc(OldEditProc, hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK PortEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        SendMessage(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(6, 0), 0);
        return 0;
    }
    return CallWindowProc(OldPortEditProc, hwnd, msg, wParam, lParam);
}

DWORD WINAPI discovery_thread(LPVOID arg) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    BOOL reuse = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(discovery_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(s, (struct sockaddr*)&addr, sizeof(addr));

    while (discovery_active) {
        fd_set readfds; FD_ZERO(&readfds); FD_SET(s, &readfds);
        struct timeval tv = {1, 0}; // 1 second timeout
        
        if (select(0, &readfds, NULL, NULL, &tv) > 0) {
            char buf[512]; struct sockaddr_in from; int fromlen = sizeof(from);
            int n = recvfrom(s, buf, sizeof(buf)-1, 0, (struct sockaddr*)&from, &fromlen);
            if (n > 0) {
                buf[n] = '\0';
                int port, pin_req; long long c, soft, h;
                if (parse_beacon(buf, &port, &c, &soft, &h, &pin_req)) {
                    char ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
                    
                    if (add_or_update_server(ip, port, c, soft, h, pin_req)) {
                        char display[128];
                        sprintf(display, "%s:%d (Soft Cap: %lld bytes)%s",
                            ip, port, soft, pin_req ? " [PIN]" : "");
                        SendMessage(hServerList, LB_ADDSTRING, 0, (LPARAM)display);
                    }
                }
            }
        }
    }
    closesocket(s);
    return 0;
}

const char* GetChatUserName(int id) { 
    for(int i = 0; i < online_user_count; i++){
        if(online_users[i].id == id) return online_users[i].name;
    }
    return "Unknown";
}

void ParseUsersStr(char* str) {
    online_user_count = 0;
    SendMessage(hUserList, LB_RESETCONTENT, 0, 0); // Clear side panel
    
    char* token = strtok(str, "|\r\n");
    while (token != NULL) {
        int id; char name[32];
        if (sscanf(token, "%d:%31s", &id, name) == 2) {
            online_users[online_user_count].id = id;
            strcpy(online_users[online_user_count].name, name);
            online_user_count++;
            
            char display[64];
            snprintf(display, sizeof(display), "%s (%d)", name, id);
            SendMessage(hUserList, LB_ADDSTRING, 0, (LPARAM)display);
        }
        token = strtok(NULL, "|\r\n");
    }
}

void FormatAndAppendChat(const char* raw_line) {
    // Decode \x0B back to standard newlines for rendering
    char decoded[4096];
    int j = 0;
    for (int i = 0; raw_line[i] != '\0' && j < sizeof(decoded) - 1; i++) {
        if (raw_line[i] == '\x0B') {
            decoded[j++] = '\n';
        } else {
            decoded[j++] = raw_line[i];
        }
    }
    decoded[j] = '\0';

    int id;
    int offset = 0;
    
    if (sscanf(decoded, "%d: %n", &id, &offset) == 1 && offset > 0) {
        char out[4096];
        snprintf(out, sizeof(out), "%s (%d): %s\n", GetChatUserName(id), id, decoded + offset);
        AppendTextToChat(out);
    } else {
        char out[4096];
        snprintf(out, sizeof(out), "%s\n", decoded);
        AppendTextToChat(out);
    }
}

void ProcessPacket(char* line) {
    if (strncmp(line, "USERS|", 6) == 0) {
        ParseUsersStr(line + 6);
    } 
    else if (strncmp(line, "MSG|", 4) == 0) {
        long long cur_bytes;
        int offset = 0;
        if (sscanf(line, "MSG|%lld|%n", &cur_bytes, &offset) == 1) {
            char label_buf[128];
            snprintf(label_buf, sizeof(label_buf), "Server Storage: %lld / %lld bytes used", cur_bytes, global_soft_cap);
            SetWindowText(hByteLabel, label_buf);
            FormatAndAppendChat(line + offset);
        }
    }
    else if (strncmp(line, "RELOAD|", 7) == 0) {
        long long cur_bytes;
        if (sscanf(line, "RELOAD|%lld", &cur_bytes) == 1) {
            char label_buf[128];
            snprintf(label_buf, sizeof(label_buf), "Server Storage: %lld / %lld bytes used", cur_bytes, global_soft_cap);
            SetWindowText(hByteLabel, label_buf);
            
            // Erase the chat window completely to prepare for the incoming full history
            SetWindowText(hChatBox, "--- CHAT HISTORY ---\r\n");
        }
    }
    else {
        FormatAndAppendChat(line); // History lines
    }
}

DWORD WINAPI receive_thread(LPVOID arg) {
    SOCKET sock = (SOCKET)(uintptr_t)arg;
    char buf[8192];
    int n;
    
    char line_buf[8192];
    int line_len = 0;

    while ((n = recv(sock, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[n] = '\0';
        for (int i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                line_buf[line_len] = '\0';
                ProcessPacket(line_buf); // Pass line to our new handler
                line_len = 0;
            } else if (buf[i] != '\r') {
                if (line_len < sizeof(line_buf) - 1) {
                    line_buf[line_len++] = buf[i];
                }
            }
        }
    }
    printf("\n[Server closed connection or byte cap reached]\n");
    exit(0); 
    return 0;
}

// --- PIN Dialog ---
static char g_pin_result[8] = "";
static int g_pin_ok = 0;
static HWND hPinWnd = NULL;
static HWND hPinEdit = NULL;
static WNDPROC OldPinEditProc;

LRESULT CALLBACK PinEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        SendMessage(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(1, 0), 0);
        return 0;
    }
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
        SendMessage(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(2, 0), 0);
        return 0;
    }
    return CallWindowProc(OldPinEditProc, hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK PinWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, text_color);
            SetBkColor(hdc, bg_color);
            return (LRESULT)hBgBrush;
        }
        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, text_color);
            SetBkColor(hdc, chat_bg);
            return (LRESULT)hChatBrush;
        }
        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT pdis = (LPDRAWITEMSTRUCT)lParam;
            FillRect(pdis->hDC, &pdis->rcItem, hAccentBrush);
            SetBkMode(pdis->hDC, TRANSPARENT);
            SetTextColor(pdis->hDC, RGB(255, 255, 255));
            char btnText[32];
            GetWindowText(pdis->hwndItem, btnText, 32);
            DrawText(pdis->hDC, btnText, -1, &pdis->rcItem,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == 1) { // OK
                GetWindowText(hPinEdit, g_pin_result, sizeof(g_pin_result));
                if (strlen(g_pin_result) == 4) {
                    g_pin_ok = 1;
                    DestroyWindow(hwnd);
                }
            } else if (LOWORD(wParam) == 2) { // Cancel
                g_pin_ok = 0;
                DestroyWindow(hwnd);
            }
            break;
        case WM_CLOSE:
            g_pin_ok = 0;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            hPinWnd = NULL;
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int PromptForPIN() {
    HINSTANCE hInst = GetModuleHandle(NULL);
    HWND parent = GetParent(hServerList);

    WNDCLASS wc = {0};
    wc.lpfnWndProc = PinWndProc;
    wc.hInstance = hInst;
    wc.hbrBackground = hBgBrush;
    wc.hIcon = LoadIcon(hInst, "IDI_ICON1");
    wc.lpszClassName = "PinDlgClass";
    RegisterClass(&wc);

    g_pin_ok = 0;
    g_pin_result[0] = '\0';

    // Center the dialog on the parent window
    RECT pr;
    GetWindowRect(parent, &pr);
    int px = pr.left + ((pr.right - pr.left) - 300) / 2;
    int py = pr.top + ((pr.bottom - pr.top) - 170) / 2;

    hPinWnd = CreateWindow("PinDlgClass", "Server PIN Required",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        px, py, 300, 170,
        parent, NULL, hInst, NULL);

    HFONT hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");

    HFONT hPinFont = CreateFont(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_MODERN, "Consolas");

    HWND hLabel = CreateWindow("STATIC", "Enter 4-digit PIN to connect:",
        WS_CHILD | WS_VISIBLE,
        20, 15, 260, 20, hPinWnd, NULL, hInst, NULL);
    SendMessage(hLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

    hPinEdit = CreateWindow("EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_PASSWORD | ES_NUMBER | ES_CENTER,
        60, 42, 170, 32, hPinWnd, NULL, hInst, NULL);
    SendMessage(hPinEdit, EM_SETLIMITTEXT, 4, 0);
    SendMessage(hPinEdit, WM_SETFONT, (WPARAM)hPinFont, TRUE);
    OldPinEditProc = (WNDPROC)SetWindowLongPtr(hPinEdit, GWLP_WNDPROC, (LONG_PTR)PinEditProc);
    SetFocus(hPinEdit);

    CreateWindow("BUTTON", "Connect",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
        30, 90, 110, 32, hPinWnd, (HMENU)1, hInst, NULL);

    CreateWindow("BUTTON", "Cancel",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
        160, 90, 110, 32, hPinWnd, (HMENU)2, hInst, NULL);

    // Modal: disable parent
    EnableWindow(parent, FALSE);

    MSG msg;
    while (hPinWnd && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    DeleteObject(hFont);
    DeleteObject(hPinFont);

    return g_pin_ok;
}

// --- Help Dialog ---
static HWND hHelpWnd = NULL;

LRESULT CALLBACK HelpWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT: {
            // Color the text box with our theme colors!
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, text_color);
            SetBkColor(hdc, chat_bg);
            return (LRESULT)hChatBrush;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            hHelpWnd = NULL;
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void ShowHelpDialog(HWND parent) {
    if (hHelpWnd) return; // Prevent opening multiple windows

    HINSTANCE hInst = GetModuleHandle(NULL);
    WNDCLASS wc = {0};
    wc.lpfnWndProc = HelpWndProc;
    wc.hInstance = hInst;
    wc.hbrBackground = hBgBrush;
    wc.hIcon = LoadIcon(hInst, "IDI_ICON1");
    wc.lpszClassName = "HelpDlgClass";
    RegisterClass(&wc);

    // Center the Help dialog on the main window
    RECT pr;
    GetWindowRect(parent, &pr);
    int width = 500;
    int height = 380;
    int px = pr.left + ((pr.right - pr.left) - width) / 2;
    int py = pr.top + ((pr.bottom - pr.top) - height) / 2;

    hHelpWnd = CreateWindow("HelpDlgClass", "Help & Commands",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        px, py, width, height, parent, NULL, hInst, NULL);

    HFONT hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");

    const char* helpText =
        "[ COMMANDS ]\r\n"
        "/name [new_name]\r\n"
        "Changes your display name in the chat.\r\n\r\n"
        "/edit [corrected_message]\r\n"
        "Edits your last sent message. It will instantly update for everyone.\r\n"
        "Shortcut: Press the UP ARROW in an empty chat box to auto-fill your last message for editing!\r\n\r\n"
        "[ HOTKEYS & SHORTCUTS ]\r\n"
        "Shift + Enter  : Type a multi-line message.\r\n"
        "Ctrl + A       : Select all text in the chat box.\r\n"
        "F2             : Toggle Light / Dark mode.\r\n"
        "F3             : Open or close this help menu.\r\n\r\n"
        "[ NETWORKING & HOSTING NOTE ]\r\n"
        "When connecting to a server, ensure you use the server's UDP Port for discovery, NOT the TCP Port.\r\n"
        "The Host UI will display the correct UDP port (which is usually the TCP Port + 1).";

    // Create a Multi-line, Read-Only, vertically scrollable text box
    HWND hHelpEdit = CreateWindow("EDIT", helpText,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER | ES_MULTILINE | ES_READONLY,
        15, 15, width - 45, height - 70, hHelpWnd, NULL, hInst, NULL);
    
    SendMessage(hHelpEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Make it modal (disables clicks on the main window while open)
    EnableWindow(parent, FALSE);

    MSG msg;
    // Run a local message loop for the modal
    while (hHelpWnd && GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_HOTKEY) {
            if (msg.wParam == 2) { 
                // If F3 is pressed again, close the dialog!
                DestroyWindow(hHelpWnd);
                continue;
            } else if (msg.wParam == 1) {
                // If F2 is pressed, we can toggle the theme even while Help is open!
                isDarkMode = !isDarkMode;
                SetTheme(parent);
                SetClassLongPtr(hHelpWnd, GCLP_HBRBACKGROUND, (LONG_PTR)hBgBrush);
                InvalidateRect(hHelpWnd, NULL, TRUE);
                EnumChildWindows(hHelpWnd, (WNDENUMPROC)InvalidateRect, TRUE);
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Restore the main window when closed
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    DeleteObject(hFont);
}

// Helper: read one line from socket
static int recv_line(SOCKET s, char* buf, int maxlen) {
    int idx = 0;
    while (idx < maxlen - 1) {
        char c;
        if (recv(s, &c, 1, 0) <= 0) break;
        buf[idx++] = c;
        if (c == '\n') break;
    }
    buf[idx] = '\0';
    // Trim trailing \r\n
    while (idx > 0 && (buf[idx-1] == '\n' || buf[idx-1] == '\r'))
        buf[--idx] = '\0';
    return idx;
}

void RestartDiscovery() {
    // Signal old thread to stop
    discovery_active = 0;

    // Clear server list
    server_count = 0;
    SendMessage(hServerList, LB_RESETCONTENT, 0, 0);

    // Read new port from input
    char port_text[16];
    GetWindowText(hPortInput, port_text, sizeof(port_text));
    int new_port = atoi(port_text);
    if (new_port < 1 || new_port > 65535) {
        MessageBox(NULL, "Port must be between 1 and 65535", "Invalid Port", MB_ICONERROR);
        discovery_active = 1;
        CreateThread(NULL, 0, discovery_thread, NULL, 0, NULL);
        return;
    }
    discovery_port = new_port;

    // Small delay to let old thread exit its select() timeout
    Sleep(150);

    // Start new discovery thread
    discovery_active = 1;
    CreateThread(NULL, 0, discovery_thread, NULL, 0, NULL);
}

void ConnectToSelectedServer(int index) {
    if (index < 0 || index >= server_count) return;
    struct ServerInfo *selected = &servers[index];

    SOCKET tcp_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(selected->tcp_port);
    inet_pton(AF_INET, selected->ip, &server_addr.sin_addr);

    if (connect(tcp_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        MessageBox(NULL, "Connection failed! Ensure the server is running.", "Error", MB_ICONERROR);
        closesocket(tcp_sock);
        return;
    }

    // Step 1: Send handshake
    const char *handshake = "HELLO_LOCALCHAT_V1\n";
    send(tcp_sock, handshake, strlen(handshake), 0);

    // Step 2: Read auth response
    char auth_line[256];
    recv_line(tcp_sock, auth_line, sizeof(auth_line));

    if (strncmp(auth_line, "AUTH_REQUIRED|", 14) == 0) {
        char* nonce = auth_line + 14;

        // Prompt user for PIN
        if (!PromptForPIN()) {
            closesocket(tcp_sock);
            return;
        }

        // Compute SHA256(nonce + pin)
        char to_hash[512];
        snprintf(to_hash, sizeof(to_hash), "%s%s", nonce, g_pin_result);

        char hash_hex[65];
        compute_sha256_hex(to_hash, strlen(to_hash), hash_hex);

        // Send AUTH|<hash>
        char auth_resp[128];
        snprintf(auth_resp, sizeof(auth_resp), "AUTH|%s\n", hash_hex);
        send(tcp_sock, auth_resp, strlen(auth_resp), 0);

        // Read result
        char result[64];
        recv_line(tcp_sock, result, sizeof(result));

        if (strncmp(result, "AUTH_OK", 7) != 0) {
            MessageBox(NULL, "Incorrect PIN!", "Authentication Failed", MB_ICONERROR);
            closesocket(tcp_sock);
            return;
        }
    } else if (strncmp(auth_line, "AUTH_NONE", 9) != 0) {
        MessageBox(NULL, "Unexpected server response.", "Error", MB_ICONERROR);
        closesocket(tcp_sock);
        return;
    }

    // Step 3: Receive SYNC header
    char header[1024];
    int h_idx = 0;
    while (h_idx < sizeof(header) - 1) {
        char c;
        if (recv(tcp_sock, &c, 1, 0) <= 0) break;
        header[h_idx++] = c;
        if (c == '\n') break;
    }
    header[h_idx] = '\0';

    long long cur, soft, hard, hist_len;
    int offset = 0;
    if (sscanf(header, "SYNC|%lld|%lld|%lld|%lld|%n", &cur, &soft, &hard, &hist_len, &offset) == 4) {
        global_soft_cap = soft;

        if (offset > 0) {
            ParseUsersStr(header + offset);
        }

        long long bytes_read = 0;
        while (bytes_read < hist_len) {
            char chunk[512];
            int to_read = (hist_len - bytes_read > sizeof(chunk) - 1) ? sizeof(chunk) - 1 : (hist_len - bytes_read);
            int r = recv(tcp_sock, chunk, to_read, 0);
            if (r <= 0) break;
            chunk[r] = '\0';
            strncat(initial_history, chunk, sizeof(initial_history) - strlen(initial_history) - 1);
            bytes_read += r;
        }

        char label_buf[128];
        snprintf(label_buf, sizeof(label_buf), "Server Storage: %lld / %lld bytes used", cur, soft);
        SetWindowText(hByteLabel, label_buf);
    }

    global_tcp_sock = tcp_sock;
    discovery_active = 0;

    ShowWindow(hServerList, SW_HIDE);
    ShowWindow(hConnectBtn, SW_HIDE);
    ShowWindow(hPortInput, SW_HIDE);
    ShowWindow(hScanBtn, SW_HIDE);

    ShowWindow(hChatBox, SW_SHOW);
    ShowWindow(hUserList, SW_SHOW);
    ShowWindow(hByteLabel, SW_SHOW);
    ShowWindow(hInputBox, SW_SHOW);
    ShowWindow(hSendBtn, SW_SHOW);

    AppendTextToChat("--- CHAT HISTORY ---\n");
    char* hist_line = strtok(initial_history, "\n");
    while (hist_line != NULL) {
        FormatAndAppendChat(hist_line);
        hist_line = strtok(NULL, "\n");
    }

    InvalidateRect(GetParent(hChatBox), NULL, TRUE);

    CreateThread(NULL, 0, receive_thread, (LPVOID)(uintptr_t)tcp_sock, 0, NULL);
}

void SetTheme(HWND hwnd) {
    // Delete old brushes to prevent memory leaks
    if (hBgBrush) DeleteObject(hBgBrush);
    if (hChatBrush) DeleteObject(hChatBrush);
    if (hAccentBrush) DeleteObject(hAccentBrush);

    if (isDarkMode) {
        // DARK MODE: Black, Red Accent
        bg_color = RGB(18, 18, 18);       // Black background
        chat_bg = RGB(28, 28, 28);        // Slightly lighter black for text boxes
        text_color = RGB(240, 240, 240);  // Soft white text
        accent_color = RGB(220, 53, 69);  // Crimson Red
    } else {
        // LIGHT MODE: White, Blue Accent
        bg_color = RGB(240, 244, 248);    // Soft Gray background
        chat_bg = RGB(255, 255, 255);     // Pure white for text boxes
        text_color = RGB(20, 20, 20);     // Near-black text
        accent_color = RGB(13, 110, 253); // Tech Blue
    }

    hBgBrush = CreateSolidBrush(bg_color);
    hChatBrush = CreateSolidBrush(chat_bg);
    hAccentBrush = CreateSolidBrush(accent_color);

    // If window exists, force everything to repaint instantly!
    if (hwnd) {
        SetClassLongPtr(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)hBgBrush);
        InvalidateRect(hwnd, NULL, TRUE); 
        UpdateWindow(hwnd);
        // Force child elements to repaint
        EnumChildWindows(hwnd, (WNDENUMPROC)InvalidateRect, TRUE);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
        case WM_CREATE:
            // Register Theme (F2) and Help (F3) hotkeys
            RegisterHotKey(hwnd, 1, 0, VK_F2);
            RegisterHotKey(hwnd, 2, 0, VK_F3);
            break;

        case WM_HOTKEY:
            if (wParam == 1) { // F2 pressed
                isDarkMode = !isDarkMode;
                SetTheme(hwnd);
            } else if (wParam == 2) { // F3 pressed
                ShowHelpDialog(hwnd);
            }
            break;

        case WM_COMMAND:
            if (LOWORD(wParam) == 4) {
                SendChatMessage(); 
            } 
            else if (LOWORD(wParam) == 5) { 
                int sel = SendMessage(hServerList, LB_GETCURSEL, 0, 0);
                if (sel != LB_ERR) ConnectToSelectedServer(sel);
                else MessageBox(hwnd, "Please select a server from the list first.", "Notice", MB_OK);
            }
            else if (LOWORD(wParam) == 6) { // Scan button
                RestartDiscovery();
            }
            break;

        // --- Custom Draw for Flat Modern Buttons ---
        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT pdis = (LPDRAWITEMSTRUCT)lParam;
            if (pdis->CtlID == 4 || pdis->CtlID == 5 || pdis->CtlID == 6) { 
                // Draw flat accent background
                FillRect(pdis->hDC, &pdis->rcItem, hAccentBrush);
                
                // Draw text in pure white so it pops on both Blue and Red
                SetBkMode(pdis->hDC, TRANSPARENT);
                SetTextColor(pdis->hDC, RGB(255, 255, 255)); 
                
                char btnText[64];
                GetWindowText(pdis->hwndItem, btnText, 64);
                DrawText(pdis->hDC, btnText, -1, &pdis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                return TRUE;
            }
            break;
        }
            
        // Text Boxes & List Boxes
        case WM_CTLCOLOREDIT: 
        case WM_CTLCOLORLISTBOX: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, text_color);
            SetBkColor(hdc, chat_bg);
            return (LRESULT)hChatBrush;
        }

        // Labels / Window Background
        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, text_color);
            SetBkColor(hdc, bg_color);
            return (LRESULT)hBgBrush;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            // Create 2-pixel wide pen using our current accent color (Blue/Red)
            HPEN hAccentPen = CreatePen(PS_SOLID, 2, accent_color);
            HPEN hOldPen = (HPEN)SelectObject(hdc, hAccentPen);

            if (discovery_active) {
                // UI: Server Browser Mode
                // Top separator under port controls
                MoveToEx(hdc, 10, 40, NULL);
                LineTo(hdc, 720, 40);
                // Bottom separator above connect button
                MoveToEx(hdc, 10, 355, NULL);
                LineTo(hdc, 720, 355);
            } else {
                // UI: Active Chat Mode
                // Horizontal line separating the chat from the input zone
                MoveToEx(hdc, 10, 355, NULL);
                LineTo(hdc, 720, 355);

                // Vertical line separating the chat history from the user list
                MoveToEx(hdc, 565, 10, NULL);
                LineTo(hdc, 565, 350);
            }

            // Cleanup memory
            SelectObject(hdc, hOldPen);
            DeleteObject(hAccentPen);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            UnregisterHotKey(hwnd, 1);
            UnregisterHotKey(hwnd, 2);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void AppendTextToChat(const char* text) {
    if (!hChatBox) return;
    
    // Windows EDIT controls need \r\n for line breaks
    char crlf_buf[2048];
    int j = 0;
    for (int i = 0; text[i] != '\0' && j < sizeof(crlf_buf) - 2; i++) {
        if (text[i] == '\n') crlf_buf[j++] = '\r';
        crlf_buf[j++] = text[i];
    }
    crlf_buf[j] = '\0';

    // Move cursor to the end and append the text
    int len = GetWindowTextLength(hChatBox);
    SendMessage(hChatBox, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessage(hChatBox, EM_REPLACESEL, 0, (LPARAM)crlf_buf);
}

void LaunchChatGUI(HINSTANCE hInstance) {
    // 1. Initialize Theme Engine globally
    SetTheme(NULL); 

    // 2. Register Window Class
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = hBgBrush;
    wc.hIcon = LoadIcon(hInstance, "IDI_ICON1");
    wc.lpszClassName = "LocalChatClass";
    RegisterClass(&wc);

    // 3. Create Main Window
    HWND hwnd = CreateWindow("LocalChatClass", "LocalChat (F3: Help)",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 750, 500,
        NULL, NULL, hInstance, NULL);

    HFONT hFont = CreateFont(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    HFONT hChatFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_MODERN, "Consolas");

    // --- Discovery Port Controls ---
    HWND hPortLabel = CreateWindow("STATIC", "UDP Port:",
        WS_CHILD | WS_VISIBLE,
        10, 14, 75, 20, hwnd, NULL, hInstance, NULL);
    SendMessage(hPortLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

    hPortInput = CreateWindow("EDIT", "9001",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_CENTER,
        90, 10, 70, 26, hwnd, NULL, hInstance, NULL);
    SendMessage(hPortInput, EM_SETLIMITTEXT, 5, 0);
    SendMessage(hPortInput, WM_SETFONT, (WPARAM)hFont, TRUE);
    OldPortEditProc = (WNDPROC)SetWindowLongPtr(hPortInput, GWLP_WNDPROC, (LONG_PTR)PortEditProc);

    hScanBtn = CreateWindow("BUTTON", "Scan",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
        168, 10, 70, 26, hwnd, (HMENU)6, hInstance, NULL);
    SendMessage(hScanBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

    // --- Server List ---
    hServerList = CreateWindow("LISTBOX", NULL,
        WS_CHILD | WS_VISIBLE | LBS_NOTIFY,
        10, 45, 710, 305, hwnd, (HMENU)10, hInstance, NULL);
    SendMessage(hServerList, WM_SETFONT, (WPARAM)hFont, TRUE);

    hConnectBtn = CreateWindow("BUTTON", "Connect to Selected Server",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
        10, 360, 710, 60, hwnd, (HMENU)5, hInstance, NULL);
    SendMessage(hConnectBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    hChatBox = CreateWindow("EDIT", "",
        WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        10, 10, 550, 340, hwnd, (HMENU)1, hInstance, NULL);
    SendMessage(hChatBox, WM_SETFONT, (WPARAM)hChatFont, TRUE);

    hUserList = CreateWindow("LISTBOX", NULL,
        WS_CHILD | LBS_NOTIFY,
        570, 10, 150, 340, hwnd, (HMENU)11, hInstance, NULL);
    SendMessage(hUserList, WM_SETFONT, (WPARAM)hFont, TRUE);

    hByteLabel = CreateWindow("STATIC", "Bytes: Connecting to server...",
        WS_CHILD, 10, 360, 710, 20, hwnd, (HMENU)2, hInstance, NULL);
    SendMessage(hByteLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

    hInputBox = CreateWindow("EDIT", "",
        WS_CHILD | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
        10, 390, 610, 60, hwnd, (HMENU)3, hInstance, NULL);
    SendMessage(hInputBox, WM_SETFONT, (WPARAM)hFont, TRUE);
    OldEditProc = (WNDPROC)SetWindowLongPtr(hInputBox, GWLP_WNDPROC, (LONG_PTR)InputEditProc);

    hSendBtn = CreateWindow("BUTTON", "Send",
        WS_CHILD | BS_PUSHBUTTON | BS_OWNERDRAW,
        630, 390, 90, 60, hwnd, (HMENU)4, hInstance, NULL);
    SendMessage(hSendBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Start listening for servers in the background
    CreateThread(NULL, 0, discovery_thread, NULL, 0, NULL);

    // 4. Run the Windows Message Loop
    MSG win_msg;
    while (GetMessage(&win_msg, NULL, 0, 0)) {
        TranslateMessage(&win_msg);
        DispatchMessage(&win_msg);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Start Windows Networking
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) return 1;

    // Launch our glorious GUI
    LaunchChatGUI(hInstance);

    // Clean up when the window is closed
    WSACleanup();
    return 0;
}