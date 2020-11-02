#include "stdafx.h"


_Use_decl_annotations_
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {

	InitWindow(hInstance, nCmdShow);
	app = Raytracing(width, height, windowTitle);
	app.Init();
	WindowLoop();
	app.Destroy();
	
	return 0;
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {

	case WM_KEYDOWN:
		app.KeyDown(static_cast<UINT8>(wParam));
		return 0;

	case WM_KEYUP:
		app.KeyUp(static_cast<UINT8>(wParam));
		return 0;

	case WM_PAINT:
		app.MainLoop();
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	// Handle any messages the switch statement didn't.
	return DefWindowProc(hWnd, message, wParam, lParam);
}


void WindowLoop() {
	MSG msg = {};
	while (msg.message != WM_QUIT) {
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
}

void InitWindow(HINSTANCE hInstance, int nCmdShow) {
	WNDCLASSEX windowClass = {};
	windowClass.cbSize = sizeof(WNDCLASSEX);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = WindowProc;
	windowClass.hInstance = hInstance;
	windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	windowClass.lpszClassName = windowName;
	RegisterClassEx(&windowClass);

	RECT windowRect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
	AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

	hwnd = CreateWindowEx(NULL,
		windowClass.lpszClassName,
		windowTitle,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		nullptr,		// We have no parent window.
		nullptr,		// We aren't using menus.
		hInstance,
		nullptr);

	ShowWindow(hwnd, nCmdShow);
	UpdateWindow(hwnd);
}