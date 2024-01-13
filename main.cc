#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#define NOMINMAX
#include <Windows.h>
#include <wingdi.h>

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "api.h"
#include "httplib.h"
#include "json.hpp"

struct Text {
  int size;
  std::string value;
  DWORD color;
  bool bold;
};

LRESULT CALLBACK winProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

std::vector<Text> getTexts();
std::vector<Text> texts;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int cmdShow) {
  constexpr wchar_t CLASS_NAME[] = L"mainWindow";
  WNDCLASSW wc = {};
  wc.lpfnWndProc = winProc;
  wc.hInstance = hInstance;
  wc.lpszClassName = CLASS_NAME;
  RegisterClassW(&wc);
  auto hwnd = CreateWindowExW(0, CLASS_NAME, L"compact-weather", WS_POPUPWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 1300,
                              40, 0, 0, hInstance, 0);
  ShowWindow(hwnd, cmdShow);
  std::thread fetchInterval([hwnd]() {
    int64_t lastFetchedAt = 0;
    while (true) {
      auto epoch = std::time(nullptr);
      if (epoch - lastFetchedAt > 60 * 60) {
        lastFetchedAt = epoch;
        texts = getTexts();
        InvalidateRect(hwnd, NULL, FALSE);
        UpdateWindow(hwnd);
      }
      Sleep(1000 * 60 * 60 * 4);
    }
  });

  MSG msg = {};
  while (GetMessage(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  return 0;
}

void render(HWND hwnd) {
  constexpr int WIDTH = 250;
  RECT rect;
  PAINTSTRUCT ps;
  HDC hdc = BeginPaint(hwnd, &ps);
  GetClientRect(hwnd, &rect);
  SetBkMode(hdc, TRANSPARENT);
  SetTextAlign(hdc, TA_LEFT | TA_TOP);
  FillRect(hdc, &rect, (HBRUSH)(COLOR_WINDOW + 1));
  rect.left = 2;
  for (auto it = texts.begin(); it != texts.end(); ++it) {
    HFONT hNewFont =
        CreateFontW(it->size, 0, 0, 0, it->bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"monospace");
    HFONT hOldFont = (HFONT)SelectObject(hdc, hNewFont);

    std::wstring wStr;
    wStr.assign(it->value.begin(), it->value.end());
    SIZE size;
    GetTextExtentPointW(hdc, wStr.c_str(), wStr.size(), &size);
    SetTextColor(hdc, it->color);
    DrawTextW(hdc, wStr.c_str(), -1, &rect, DT_SINGLELINE | DT_NOCLIP);
    rect.left += size.cx + 1;

    SelectObject(hdc, hOldFont);
    DeleteObject(hNewFont);
  }
  EndPaint(hwnd, &ps);
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
      render(hwnd);
      break;
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

std::string zeroFill(int i) {
  if (i < 10) {
    return "0" + std::to_string(i);
  }
  return std::to_string(i);
}

std::string getBaseDate(tm *now) {
  return std::to_string(1900 + now->tm_year) + zeroFill(now->tm_mon + 1) + zeroFill(now->tm_mday);
}

std::string getBaseTime(tm *now) {
  int hour = now->tm_hour + 1;
  int min = now->tm_min;
  if (hour < 2 || (hour == 2 && min < 12)) {
    throw std::exception("not yet");
  }

  return "0200";
}

struct Forecast {
  struct Today {
    int month;
    int day;
    int minTemp;
    int maxTemp;
    int maxPop;
  };
  struct Hour {
    int hour;
    int temp;
    int pop;
  };
  Today today;
  std::vector<Hour> hours;
};

Forecast fetch() {
  constexpr char API_URL[] = "http://apis.data.go.kr";
  constexpr char API_PATH[] = "/1360000/VilageFcstInfoService_2.0/getVilageFcst";
  auto epoch = std::time(0);
  auto now = std::localtime(&epoch);

  std::string baseDate = getBaseDate(now);
  std::string baseTime = getBaseTime(now);
  std::string path =
      std::string(API_PATH) +
      "?pageNo=1&dataType=JSON&serviceKey=" API_SERVICE_KEY "&numOfRows=300&nx=" API_NX "&ny=" API_NY "&base_date=" + baseDate +
      "&base_time=" + baseTime;
  httplib::Client client(API_URL);
  auto res = client.Get(path);
  if (res->status != 200) {
    throw std::runtime_error("status: " + std::to_string(res->status));
  }

  using json = nlohmann::json;
  json data = json::parse(res->body);
  std::string responseCode = data["response"]["header"]["resultCode"];
  if (responseCode != "00") {
    throw std::runtime_error("result:" + responseCode + ": " + (std::string)data["response"]["header"]["resultMsg"]);
  }

  json items = data["response"]["body"]["items"]["item"];
  Forecast forecast = {};
  forecast.today.month = now->tm_mon + 1;
  forecast.today.day = now->tm_mday;
  forecast.today.minTemp = -100;
  forecast.today.maxTemp = 100;
  forecast.today.maxPop = -1;
  for (auto it = items.begin(); it != items.end(); ++it) {
    std::string fcstDate = (*it)["fcstDate"];
    if (fcstDate != baseDate) {
      continue;
    }
    std::string category = (*it)["category"];
    std::string value = (*it)["fcstValue"];
    if (category == "TMN") {
      forecast.today.minTemp = std::stoi(value);
      continue;
    }
    if (category == "TMX") {
      forecast.today.maxTemp = std::stoi(value);
      continue;
    }

    std::string fcstTime = (*it)["fcstTime"];
    int fcstHour = std::stoi(fcstTime.substr(0, 2));
    Forecast::Hour *hour = nullptr;
    auto hourIt = std::find_if(forecast.hours.begin(), forecast.hours.end(),
                               [fcstHour](Forecast::Hour hour) -> bool { return hour.hour == fcstHour; });
    if (hourIt == forecast.hours.end()) {
      forecast.hours.push_back(Forecast::Hour{fcstHour, -100, -1});
      hour = &forecast.hours.back();
    } else {
      hour = &(*hourIt);
    }

    if (category == "POP") {
      int pop = std::stoi(value);
      hour->pop = pop;
      if (forecast.today.maxPop < pop) {
        forecast.today.maxPop = pop;
      }
    }
    if (category == "TMP") {
      hour->temp = std::stoi(value);
    }
  }
  std::vector<Forecast::Hour> resultHours;
  std::copy_if(forecast.hours.begin(), forecast.hours.end(), std::back_inserter(resultHours),
               [](Forecast::Hour hour) { return hour.hour >= 8 && hour.hour <= 24 && hour.hour % 2 == 0; });
  forecast.hours = resultHours;
  return forecast;
}

std::vector<Text> getTexts() {
  const Text delimiterS = {20, "|", RGB(180, 180, 180)};
  const Text delimiter = {35, " | ", RGB(150, 150, 150)};

  std::vector<Text> result = {};
  try {
    auto forecast = fetch();
    result.push_back(Text{35, std::to_string(forecast.today.month) + "/" + std::to_string(forecast.today.day), 0, true});
    result.push_back(delimiter);
    result.push_back(Text{31, std::to_string(forecast.today.minTemp), RGB(155, 80, 0)});
    result.push_back(Text{18, "c ", RGB(80, 80, 80)});
    result.push_back(Text{31, std::to_string(forecast.today.maxTemp), RGB(155, 0, 80)});
    result.push_back(Text{18, "c ", RGB(80, 80, 80)});
    result.push_back(Text{31, std::to_string(forecast.today.maxPop), RGB(0, 0, 155)});
    result.push_back(Text{18, "%  ", RGB(80, 80, 80)});
    for (auto it = forecast.hours.begin(); it != forecast.hours.end(); ++it) {
      result.push_back(delimiter);
      result.push_back(Text{24, std::to_string(it->hour), 0, true});
      result.push_back(delimiterS);
      result.push_back(Text{24, std::to_string(it->temp), RGB(130, 0, 0)});
      result.push_back(Text{13, "c ", RGB(80, 80, 80)});
      result.push_back(Text{24, std::to_string(it->pop), RGB(0, 0, 155)});
      result.push_back(Text{13, "%   ", RGB(80, 80, 80)});
    }
  } catch (std::exception e) {
    result.push_back(Text{30, "error", RGB(99, 0, 0)});
    result.push_back(Text{12, e.what()});
  }

  return result;
}