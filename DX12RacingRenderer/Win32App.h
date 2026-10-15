#pragma once
#include <Windows.h>
#include <string>

class Win32App
{
public:
	Win32App();

	/// @brief ウィンドウクラスの登録、ウィンドウの作成
	/// @param width ウィンドウ幅
	/// @param height ウィンドウ高さ
	/// @param title ウィンドウタイトル
	/// @return 登録、作成が成功したか
	bool Init(const unsigned int width, const unsigned int height, const std::wstring& title);
	
	/// @brief ウィンドウを表示
	void Show();

	/// @brief ウィンドウメッセージを処理
	/// @return 続けるなら true、WM_QUIT を受け取ったら false
	bool ProcessMessage();

	/// @brief ウィンドウハンドルを取得
	HWND GetHwnd() const { return hwnd_; }

	/// @brief ウィンドウ幅を取得
	unsigned int GetWindowWidth() const
	{
		return windowWidth_;
	}

	/// @brief ウィンドウ高さを取得
	unsigned int GetWindowHeight() const
	{
		return windowHeight_;
	}

	/// @brief ウィンドウタイトルを変更
	/// @param title 変更後のウィンドウタイトル
	void SetWindowTitle(const std::wstring& title)
	{
		SetWindowTextW(hwnd_, title.c_str());
	}

private:
	/// @brief ウィンドウプロシージャ
	/// @details ウィンドウへの入力やシステムからの通知(メッセージ)などを受け取り
	/// 処理するコールバック関数(別の関数に引数として渡される)
	static LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

private:
	// ウィンドウハンドル
	HWND hwnd_;
	// アプリケーションインスタンスへのハンドル
	HINSTANCE hInstance_;
	// 画面の幅
	unsigned int windowWidth_;
	// 画面の高さ
	unsigned int windowHeight_;
};
