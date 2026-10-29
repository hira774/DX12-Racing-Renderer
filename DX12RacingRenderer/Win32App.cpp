#include "Win32App.h"

#include "imgui_impl_win32.h"

#include "Logger.h"

// imgui_impl_win32.h では宣言が消してあるためここで宣言する
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

Win32App::Win32App() :
	hwnd_(nullptr),
	hInstance_(nullptr),
	windowWidth_(0),
	windowHeight_(0)
{
}

LRESULT CALLBACK Win32App::WindowProcedure(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	// ImGui に入力を渡す。0 以外が返ったら ImGui が処理を済ませたので、ここで返す
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam))
	{
		return true;
	}

	// ウィンドウが破棄されたら呼ばれる
	if (msg == WM_DESTROY)
	{
		PostQuitMessage(0);
		return 0;
	}

	// 右ボタンを押している間は、カーソルがウィンドウの外に出てもマウスのメッセージを受け取る
	if (msg == WM_RBUTTONDOWN)
	{
		SetCapture(hwnd);
	}
	else if (msg == WM_RBUTTONUP)
	{
		ReleaseCapture();
	}

	// thisの代わり
	Win32App* self = reinterpret_cast<Win32App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

	if (self != nullptr)
	{
		self->input_.HandleMessage(msg, wparam, lparam);
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

	// ウィンドウの持ち主のアドレスを覚える
	// (WindowProcedure で取り出す)
	SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

	// マウスの RawInput を登録する
	if (!input_.Init(hwnd_)) { return false; }

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
	input_.BeginFrame();

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
