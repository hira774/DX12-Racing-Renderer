#include "Application.h"

#include <format>

#include "Game.h"

Application::Application() :
	renderer_(winApp_),
	windowWidth_(Game::kWindowWidth),
	windowHeight_(Game::kWindowHeight),
	requestExit_(false),
	fpsStartTime_(std::chrono::steady_clock::now()),
	fpsFrameCount_(0)
{
}

Application& Application::Instance()
{
	// Application の実体のメモリ位置は固定
	// 最初に呼ばれたときに一度だけつくられて、終了まで残る
	static Application instance;
	return instance;
}

bool Application::Init()
{
	if (!winApp_.Init(
		windowWidth_, windowHeight_,
		Game::kWindowTitle))
	{
		return false;
	}
	// ウィンドウを表示
	winApp_.Show();

	if (!renderer_.Init())
	{
		return false;
	}

	return true;
}

void Application::Run()
{
	// 初期化処理にかかった時間分、fpsが低く表示されるのを防ぐ
	fpsStartTime_ = std::chrono::steady_clock::now();
	fpsFrameCount_ = 0;

	// メインループ
	while (winApp_.ProcessMessage() && !requestExit_)
	{
		// 描画
		if (!renderer_.Render()) { RequestExit(); }

		// fps計測
		{
			++fpsFrameCount_;
			// 今の時刻を取得
			auto now = std::chrono::steady_clock::now();
			// 2つの時刻の差を 秒(double) で受け取る
			std::chrono::duration<double> elapsed = now - fpsStartTime_;
			// 数値(秒)
			double seconds = elapsed.count();
			// 1秒経過したら
			if (seconds >= 1.0)
			{
				// fps を計算
				auto fps = (fpsFrameCount_ / seconds);
				// fpsを表示
				winApp_.SetWindowTitle(std::format(L"{}  FPS: {:.1f}", Game::kWindowTitle, fps));
				// 次のfps計測のため、カウントをリセット
				fpsStartTime_ = now;
				fpsFrameCount_ = 0;
			}
		}
	}
}

void Application::Terminate()
{
	// TODO Renderer::Terminateが false の時の処理を作成する
	renderer_.Terminate();
}

void Application::RequestExit()
{
	requestExit_ = true;
}
