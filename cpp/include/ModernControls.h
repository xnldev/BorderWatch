#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <gdiplus.h>
#include <string>
#include <vector>

#define BW_TOGGLE_CLASS    L"BorderWatchToggle"
#define BW_SLIDER_CLASS    L"BorderWatchSlider"
#define BW_STEPPER_CLASS   L"BorderWatchStepper"
#define BW_DROPDOWN_CLASS  L"BorderWatchDropdown"
#define BW_HISTORY_CLASS   L"BorderWatchHistory"

void RegisterModernControls(HINSTANCE hInstance);
void ModernControls_SetDarkMode(bool isDark);
void ModernControls_SetDarkProgress(float progress);

// toggle
void Toggle_SetChecked(HWND hWnd, bool checked);
bool Toggle_GetChecked(HWND hWnd);

// slider
void Slider_SetValue(HWND hWnd, double val);
double Slider_GetValue(HWND hWnd);

// stepper
void Stepper_SetRange(HWND hWnd, int minVal, int maxVal, const std::wstring& suffix);
void Stepper_SetValue(HWND hWnd, int val);
int Stepper_GetValue(HWND hWnd);

// dropdown
void Dropdown_SetItems(HWND hWnd, const std::vector<std::wstring>& items);
void Dropdown_SetCurSel(HWND hWnd, int idx);
int Dropdown_GetCurSel(HWND hWnd);
std::wstring Dropdown_GetSelectedText(HWND hWnd);

// history
void History_AddLine(HWND hWnd, const std::wstring& line);
void History_SetLines(HWND hWnd, const std::vector<std::wstring>& lines);
void History_Clear(HWND hWnd);
