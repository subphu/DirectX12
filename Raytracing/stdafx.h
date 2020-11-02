#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers.
#endif

#include <windows.h>

#include "Raytracing.h"

HWND hwnd;
Raytracing app;

UINT width = 900;
UINT height = 600;
LPCTSTR windowName = L"DX12Class";
LPCTSTR windowTitle = L"Raytracing";

void InitWindow(HINSTANCE hInstance, int nCmdShow);
void WindowLoop();
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
