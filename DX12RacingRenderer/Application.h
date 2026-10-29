#pragma once

#include <chrono>

// 実体を持つのでヘッダでincludeする
#include "Win32App.h"
#include "Renderer.h"
#include "Camera.h"

class Application
{
public:
	/// @brief アプリケーションクラスの実体にアクセスできる唯一の入り口
	/// @return アプリケーションクラスの実体
	static Application& Instance();

public:
	/// @brief 起動処理 (初期化)
	/// @return 成功したか
	bool Init();

	/// @brief メインループ
	void Run();

	/// @brief 終了処理
	void Terminate();

	/// @brief アプリケーション終了要求
	void RequestExit();

public:
	/// @brief ウィンドウの幅を取得
	unsigned int GetWindowWidth() const { return windowWidth_; }
	/// @brief ウィンドウの高さを取得
	unsigned int GetWindowHeight() const { return windowHeight_; }

private:
	/// @brief コンストラクタ
	/// @details 外部から実体を作ることはできない
	Application();
	/// @brief コピーコンストラクタを禁止する
	Application(const Application&) = delete;
	/// @brief コピー代入演算子を禁止する
	Application& operator=(const Application&) = delete;

	/// @brief デバッグ用の UI を作る
	void DrawDebugUI();

private:
	// ウィンドウの作成とメッセージの処理
	Win32App winApp_;
	// 描画を行う
	Renderer renderer_;
	// カメラ
	Camera camera_;

	// ウィンドウ幅
	unsigned int windowWidth_;
	// ウィンドウ高さ
	unsigned int windowHeight_;

	// アプリケーション終了フラグ
	bool requestExit_;

	// フレームのカウントを始めた時刻
	std::chrono::steady_clock::time_point fpsStartTime_;
	// 前のフレームの時刻
	std::chrono::steady_clock::time_point lastFrameTime_;
	// フレームのカウント数
	unsigned int fpsFrameCount_;
};
