#include "Win32App.h"

#include "Logger.h"

Win32App::Win32App() :
	hwnd_(nullptr),
	hInstance_(nullptr),
	windowWidth_(0),
	windowHeight_(0)
{
}

LRESULT CALLBACK Win32App::WindowProcedure(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	// ウィンドウが破棄されたら呼ばれる
	if (msg == WM_DESTROY)
	{
		PostQuitMessage(0);
		return 0;
	}
	// 既定の処理
	return DefWindowProcW(hwnd, msg, wparam, lparam);
}

bool Win32App::Init(const unsigned int width, const unsigned int height, const std::wstring& title)
{
	WNDCLASSEXW w = {};
	w.cbSize = sizeof(WNDCLASSEXW);
	w.lpfnWndProc = WindowProcedure;	// コールバック関数の指定
	w.lpszClassName = title.c_str();	// ウィンドウクラス名(適当でよい)
	w.hInstance = GetModuleHandleW(nullptr);	// ハンドルの取得
	// ウィンドウクラスを登録
	if (!RegisterClassExW(&w))
	{
		Logger::Error("Failed to register the window class.");
		return false;
	}

	windowWidth_ = width;
	windowHeight_ = height;

	// サイズ変更・最大化を禁止したウィンドウスタイル
	const DWORD windowStyle = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);

	RECT wrc = { 0, 0, static_cast<LONG>(windowWidth_), static_cast<LONG>(windowHeight_) };
	AdjustWindowRect(&wrc, windowStyle, FALSE);

	hwnd_ = CreateWindowW(
		w.lpszClassName,
		title.c_str(),
		windowStyle,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		wrc.right - wrc.left,
		wrc.bottom - wrc.top,
		nullptr,
		nullptr,
		w.hInstance,
		nullptr
	);
	if (hwnd_ == nullptr)
	{
		Logger::Error("Failed to create the window.");
		return false;
	}

	hInstance_ = w.hInstance;
	return true;
}

void Win32App::Show()
{
	ShowWindow(hwnd_, SW_SHOW);
	Logger::Info("Show window");
}

bool Win32App::ProcessMessage()
{
	MSG msg = {};

	// メッセージをすべて処理
	while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
	{
		// 終了
		if (msg.message == WM_QUIT) { return false; }

		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	return true;
}
