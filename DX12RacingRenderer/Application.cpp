#include "Application.h"

#include "Game.h"

Application::Application() :
	renderer_(winApp_),
	windowWidth_(Game::kWindowWidth),
	windowHeight_(Game::kWindowHeight),
	requestExit_(false)
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
	// メインループ
	while (winApp_.ProcessMessage() && !requestExit_)
	{
		// 描画
		if (!renderer_.Render()) { RequestExit(); }
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
