#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#define NOMINMAX
#include <Windows.h>
#include <wingdi.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "api.h"
#include "httplib.h"
#include "json.hpp"

using json = nlohmann::json;

LRESULT CALLBACK winProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
std::string fetch();
std::wstring tttt;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int cmdShow) {
  constexpr wchar_t CLASS_NAME[] = L"mainWindow";
  WNDCLASSW wc = {};
  wc.lpfnWndProc = winProc;
  wc.hInstance = hInstance;
  wc.lpszClassName = CLASS_NAME;
  RegisterClassW(&wc);
  auto hwnd = CreateWindowExW(0, CLASS_NAME, L"compact-weather",
                              WS_POPUPWINDOW | WS_VISIBLE, CW_USEDEFAULT,
                              CW_USEDEFAULT, 800, 36, 0, 0, hInstance, 0);
  ShowWindow(hwnd, cmdShow);
  std::thread fetchInterval([hwnd]() {
    while (true) {
      auto text = fetch();
      tttt.assign(text.begin(), text.end());
      InvalidateRect(hwnd, NULL, FALSE);
      UpdateWindow(hwnd);

      // 2 hours
      Sleep(1000 * 60 * 60 * 2);
    }
  });

  MSG msg = {};
  while (GetMessage(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  return 0;
}

LRESULT CALLBACK winProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_CLOSE:
      exit(0);
    case WM_NCHITTEST: {
      LRESULT hit = DefWindowProcW(hwnd, msg, wParam, lParam);
      if (hit == HTCLIENT) hit = HTCAPTION;
      return hit;
    }
    case WM_PAINT: {
      RECT rect;
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      GetClientRect(hwnd, &rect);
      SetTextColor(hdc, RGB(0, 0, 0));
      SetBkMode(hdc, TRANSPARENT);
      FillRect(hdc, &rect, (HBRUSH)(COLOR_WINDOW + 1));
      rect.left = 2;
      rect.top = 0;
      HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
      LOGFONTW logFont;
      GetObjectW(hFont, sizeof(LOGFONTW), &logFont);
      HFONT hNewFont =
          CreateFontW(30, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE,
                      ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                      DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Arial");
      HFONT hOldFont = (HFONT)SelectObject(hdc, hNewFont);

      DrawTextW(hdc, tttt.c_str(), -1, &rect, DT_SINGLELINE | DT_NOCLIP);

      SelectObject(hdc, hOldFont);
      DeleteObject(hNewFont);
      EndPaint(hwnd, &ps);
    }
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int ftoc(int f) { return (f - 32) * 5 / 9; }

std::string formatMinMax(int min, int max) {
  std::string r = {};
  r.append(std::to_string(min));
  r.append(",");
  r.append(std::to_string(max));
  return r;
}

std::string formatP(int rain, int ice) {
  if (rain == -1 && ice == -1) {
    return "?";
  }
  int i;
  if (rain != -1) {
    i = rain;
  } else {
    i = ice;
  }
  return std::to_string(i);
}

std::string fetch() {
#ifdef API_MOCKING
  json data = json::parse(std::ifstream("mock.json"));
#else
  constexpr char url[] = "http://dataservice.accuweather.com";
  constexpr char path[] =
      "/forecasts/v1/daily/5day/" API_LOCATION_KEY "?apikey=" API_KEY;
  httplib::Client client(url);
  auto res = client.Get(path);
  if (res->status != 200) {
    return "status " + std::to_string(res->status);
  }

  json data = json::parse(res->body);
#endif

  std::string result = {};
  auto forecasts = data["DailyForecasts"];
  for (int i = 0; i < 3; i++) {
    auto forecast = forecasts[i];
    std::string date = forecast["Date"];
    std::string simpleDate = date.substr(5, 5);
    int minF = forecast["Temperature"]["Minimum"]["Value"];
    int maxF = forecast["Temperature"]["Maximum"]["Value"];
    int rainP = -1;
    int iceP = -1;
    if (i != 0) {
      result.append("      ");
    }
    result.append("<");
    result.append(simpleDate);
    result.append("> [");
    result.append(formatMinMax(ftoc(minF), ftoc(maxF)));
    result.append("] (");
    result.append(formatP(rainP, iceP));
    result.append(")");
  }

  return result;
}