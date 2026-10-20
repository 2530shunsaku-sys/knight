#include <windows.h>
#include <iostream>
#include <cmath>
#include <vector>

#pragma comment(lib, "winmm.lib")

struct AnimInstance {
  HBITMAP hBmpCache;
  RECT targetRect;
  int animWidth;
  int animHeight;
  LARGE_INTEGER startTime;
};

std::vector<AnimInstance> g_animations;
LARGE_INTEGER g_frequency;

const float TIME_RED_LINE = 0.15f;
const float TIME_BLACK_LINE = 0.15f;
const float DELAY_BLACK = 0.05f;
const float DELAY_SPLIT = 0.15f;

const float FIVE_FRAMES_SEC = 5.0f / 60.0f;
const float INITIAL_STEP_X = 20.0f;

const float ANIM_SCALE_QUAD = 2500.0f;

HWINEVENTHOOK g_hook = NULL;
HWND hOverlayWnd = NULL;

HBITMAP CaptureScreenRect(HWND hwnd, RECT rect, int w, int h) {
  HDC hdcScreen = GetDC(NULL);
  HDC hdcMem = CreateCompatibleDC(hdcScreen);
  HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, w, h);
  SelectObject(hdcMem, hBmp);

  BOOL success = PrintWindow(hwnd, hdcMem, 4);
  if (!success) {
    BitBlt(hdcMem, 0, 0, w, h, hdcScreen, rect.left, rect.top, SRCCOPY);
  }

  DeleteDC(hdcMem);
  ReleaseDC(NULL, hdcScreen);
  return hBmp;
}

void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
  if (idObject == OBJID_WINDOW && idChild == CHILDID_SELF && hwnd != NULL && hwnd != hOverlayWnd && hwnd != GetConsoleWindow()) {

    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);

    if (style & WS_CHILD) return;
    if (exStyle & WS_EX_TOOLWINDOW) return;
    if (!(style & WS_CAPTION) && !(exStyle & WS_EX_APPWINDOW)) return;

    wchar_t className[256];
    GetClassName(hwnd, className, 256);
    if (wcscmp(className, L"Progman") == 0 || wcscmp(className, L"WorkerW") == 0) return;

    if (event == EVENT_OBJECT_HIDE) {
      if (IsIconic(hwnd)) return;
    }

    RECT rect;
    if (GetWindowRect(hwnd, &rect)) {
      int w = rect.right - rect.left;
      int h = rect.bottom - rect.top;

      if (w > 150 && h > 150) {
        if (rect.right <= 0 || rect.bottom <= 0 ||
          rect.left >= GetSystemMetrics(SM_CXSCREEN) ||
          rect.top >= GetSystemMetrics(SM_CYSCREEN)) {
          return;
        }

        HBITMAP hBmp = CaptureScreenRect(hwnd, rect, w, h);
        if (!hBmp) return;

        AnimInstance anim;
        anim.hBmpCache = hBmp;
        anim.targetRect = rect;
        anim.animWidth = w;
        anim.animHeight = h;
        QueryPerformanceCounter(&anim.startTime);

        g_animations.push_back(anim);

        SetWindowPos(hOverlayWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(hOverlayWnd, NULL, FALSE);
        UpdateWindow(hOverlayWnd);
      }
    }
  }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    if (!g_animations.empty()) {
      int screenWidth = GetSystemMetrics(SM_CXSCREEN);
      int screenHeight = GetSystemMetrics(SM_CYSCREEN);

      HDC hdcBuffer = CreateCompatibleDC(hdc);
      HBITMAP hBmpBuffer = CreateCompatibleBitmap(hdc, screenWidth, screenHeight);
      HBITMAP hOldBufferBmp = (HBITMAP)SelectObject(hdcBuffer, hBmpBuffer);

      HBRUSH hBrush = CreateSolidBrush(RGB(255, 0, 255));
      RECT rcScreen = { 0, 0, screenWidth, screenHeight };
      FillRect(hdcBuffer, &rcScreen, hBrush);
      DeleteObject(hBrush);

      LARGE_INTEGER now;
      QueryPerformanceCounter(&now);

      for (auto it = g_animations.begin(); it != g_animations.end(); ) {
        float t = (float)(now.QuadPart - it->startTime.QuadPart) / g_frequency.QuadPart;

        float shiftX = 0.0f;
        if (t > DELAY_SPLIT) {
          float splitTime = t - DELAY_SPLIT;

          float easeFactor = splitTime * splitTime * splitTime * splitTime;
          float normalAnimX = easeFactor * ANIM_SCALE_QUAD;

          //修正5フレーム終了後（FIVE_FRAMES_SECより後）のブロックでも 
          // INITIAL_STEP_X (20px) を足し続けることで、元の位置に戻るバグを解消しました。
          shiftX = INITIAL_STEP_X + normalAnimX;
        }

        float pieceX_L = -shiftX;
        float pieceX_R = shiftX;

        if (it->targetRect.left + it->animWidth + (int)pieceX_L < 0 && it->targetRect.left + (int)pieceX_R > screenWidth) {
          DeleteObject(it->hBmpCache);
          it = g_animations.erase(it);
          continue;
        }

        HDC hdcSource = CreateCompatibleDC(hdc);
        HBITMAP hOldSourceBmp = (HBITMAP)SelectObject(hdcSource, it->hBmpCache);

        int topCut = (int)(it->animWidth * 0.4f);
        int bottomCut = (int)(it->animWidth * 0.6f);

        POINT dstLeft[4];
        dstLeft[0] = { it->targetRect.left + 0 + (int)pieceX_L,         it->targetRect.top };
        dstLeft[1] = { it->targetRect.left + topCut + (int)pieceX_L,    it->targetRect.top };
        dstLeft[2] = { it->targetRect.left + bottomCut + (int)pieceX_L, it->targetRect.top + it->animHeight };
        dstLeft[3] = { it->targetRect.left + 0 + (int)pieceX_L,         it->targetRect.top + it->animHeight };

        POINT dstRight[4];
        dstRight[0] = { it->targetRect.left + topCut + (int)pieceX_R,    it->targetRect.top };
        dstRight[1] = { it->targetRect.left + it->animWidth + (int)pieceX_R, it->targetRect.top };
        dstRight[2] = { it->targetRect.left + it->animWidth + (int)pieceX_R, it->targetRect.top + it->animHeight };
        dstRight[3] = { it->targetRect.left + bottomCut + (int)pieceX_R, it->targetRect.top + it->animHeight };

        HRGN hRgnLeft = CreatePolygonRgn(dstLeft, 4, WINDING);
        SelectClipRgn(hdcBuffer, hRgnLeft);
        BitBlt(hdcBuffer, it->targetRect.left + (int)pieceX_L, it->targetRect.top, it->animWidth, it->animHeight, hdcSource, 0, 0, SRCCOPY);
        SelectClipRgn(hdcBuffer, NULL);
        DeleteObject(hRgnLeft);

        HRGN hRgnRight = CreatePolygonRgn(dstRight, 4, WINDING);
        SelectClipRgn(hdcBuffer, hRgnRight);
        BitBlt(hdcBuffer, it->targetRect.left + (int)pieceX_R, it->targetRect.top, it->animWidth, it->animHeight, hdcSource, 0, 0, SRCCOPY);
        SelectClipRgn(hdcBuffer, NULL);
        DeleteObject(hRgnRight);

        float x1 = (float)(it->targetRect.left + topCut + (int)pieceX_L);
        float y1 = (float)(it->targetRect.top);
        float x2 = (float)(it->targetRect.left + bottomCut + (int)pieceX_L);
        float y2 = (float)(it->targetRect.top + it->animHeight);

        if (t >= 0.0f && t <= TIME_RED_LINE) {
          float progress = t / TIME_RED_LINE;
          int currentY = it->targetRect.top + (int)(it->animHeight * progress);

          float ratio = progress;
          int currentX = (int)(x1 + (x2 - x1) * ratio);

          HPEN hRedPen = CreatePen(PS_SOLID, 8, RGB(255, 0, 0));
          HPEN hOldPen = (HPEN)SelectObject(hdcBuffer, hRedPen);
          MoveToEx(hdcBuffer, (int)x1, (int)y1, NULL);
          LineTo(hdcBuffer, currentX, currentY);
          SelectObject(hdcBuffer, hOldPen);
          DeleteObject(hRedPen);
        }
        else if (t > TIME_RED_LINE && t <= DELAY_SPLIT) {
          HPEN hRedPen = CreatePen(PS_SOLID, 8, RGB(255, 0, 0));
          HPEN hOldPen = (HPEN)SelectObject(hdcBuffer, hRedPen);
          MoveToEx(hdcBuffer, (int)x1, (int)y1, NULL);
          LineTo(hdcBuffer, (int)x2, (int)y2);
          SelectObject(hdcBuffer, hOldPen);
          DeleteObject(hRedPen);
        }

        if (t >= DELAY_BLACK) {
          float blackT = t - DELAY_BLACK;
          if (blackT <= TIME_BLACK_LINE) {
            float progress = blackT / TIME_BLACK_LINE;
            int currentY = it->targetRect.top + (int)(it->animHeight * progress);

            float ratio = progress;
            int currentX = (int)(x1 + (x2 - x1) * ratio);

            HPEN hBlackPen = CreatePen(PS_SOLID, 10, RGB(0, 0, 0));
            HPEN hOldPen = (HPEN)SelectObject(hdcBuffer, hBlackPen);
            MoveToEx(hdcBuffer, (int)x1, (int)y1, NULL);
            LineTo(hdcBuffer, currentX, currentY);
            SelectObject(hdcBuffer, hOldPen);
            DeleteObject(hBlackPen);
          }
          else if (t <= DELAY_SPLIT) {
            HPEN hBlackPen = CreatePen(PS_SOLID, 10, RGB(0, 0, 0));
            HPEN hOldPen = (HPEN)SelectObject(hdcBuffer, hBlackPen);
            MoveToEx(hdcBuffer, (int)x1, (int)y1, NULL);
            LineTo(hdcBuffer, (int)x2, (int)y2);
            SelectObject(hdcBuffer, hOldPen);
            DeleteObject(hBlackPen);
          }
        }

        if (t > DELAY_SPLIT) {
          HPEN hRedPen = CreatePen(PS_SOLID, 4, RGB(255, 0, 0));
          HPEN hOldPen = (HPEN)SelectObject(hdcBuffer, hRedPen);

          MoveToEx(hdcBuffer, dstLeft[1].x, dstLeft[1].y, NULL);
          LineTo(hdcBuffer, dstLeft[2].x, dstLeft[2].y);

          MoveToEx(hdcBuffer, dstRight[0].x, dstRight[0].y, NULL);
          LineTo(hdcBuffer, dstRight[3].x, dstRight[3].y);

          SelectObject(hdcBuffer, hOldPen);
          DeleteObject(hRedPen);
        }

        SelectObject(hdcSource, hOldSourceBmp);
        DeleteDC(hdcSource);

        it++;
      }

      BitBlt(hdc, 0, 0, screenWidth, screenHeight, hdcBuffer, 0, 0, SRCCOPY);

      SelectObject(hdcBuffer, hOldBufferBmp);
      DeleteObject(hBmpBuffer);
      DeleteDC(hdcBuffer);

      if (g_animations.empty()) {
        ShowWindow(hwnd, SW_HIDE);
      }
    }
    EndPaint(hwnd, &ps);
    break;
  }
  case WM_ERASEBKGND:
    return 1;

  default:
    return DefWindowProc(hwnd, msg, wp, lp);
  }
  return 0;
}

int main() {
  std::cout << "=========================================" << std::endl;
  std::cout << "  超低遅延・高平滑マルチダブルバッファ    " << std::endl;
  std::cout << "=========================================" << std::endl;

  QueryPerformanceFrequency(&g_frequency);
  timeBeginPeriod(1);

  HINSTANCE hInst = GetModuleHandle(NULL);
  WNDCLASSEX wc = { 0 };
  wc.cbSize = sizeof(WNDCLASSEX);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInst;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.lpszClassName = L"OverlayDoubleBufferedWindow";
  RegisterClassEx(&wc);
  hOverlayWnd = CreateWindowEx(
    WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
    L"OverlayDoubleBufferedWindow", L"", WS_POPUP,
    0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
    NULL, NULL, hInst, NULL
  );
  SetLayeredWindowAttributes(hOverlayWnd, RGB(255, 0, 255), 0, LWA_COLORKEY);
  g_hook = SetWinEventHook(
    EVENT_OBJECT_HIDE, EVENT_OBJECT_HIDE,
    NULL, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT
  );
  MSG msg = { 0 };
  while (msg.message != WM_QUIT) {
    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    else {
      if (!g_animations.empty()) {
        SetWindowPos(hOverlayWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        InvalidateRect(hOverlayWnd, NULL, FALSE);
        UpdateWindow(hOverlayWnd);
        Sleep(0);
      }
      else {
        Sleep(1);
      }
    }
  }
  for (auto& anim : g_animations) {
    if (anim.hBmpCache) DeleteObject(anim.hBmpCache);
  }
  UnhookWinEvent(g_hook);
  timeEndPeriod(1);
  return (int)msg.wParam;
}