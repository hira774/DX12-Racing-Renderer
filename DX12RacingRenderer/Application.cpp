#include "Application.h"

#include <format>
#include <objbase.h>

#include "imgui.h"

#include "Game.h"
#include "Logger.h"
#include "Input.h"

Application::Application() :
	renderer_(winApp_),
	windowWidth_(Game::kWindowWidth),
	windowHeight_(Game::kWindowHeight),
	requestExit_(false),
	fpsStartTime_(std::chrono::steady_clock::now()),
	lastFrameTime_(std::chrono::steady_clock::now()),
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
	// コンソールの文字コードを UTF-8 に設定
	if (SetConsoleOutputCP(CP_UTF8) == 0)
	{
		Logger::Warn("Failed to set console output code page.");
	}

	// COM の初期化
	// DirectXTex の関数より先に初期化する
	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(hr))
	{
		Logger::Error("Failed to initialize COM.");
		return false;
	}

	if (!winApp_.Init(
		windowWidth_, windowHeight_,
		Game::kWindowTitle))
	{
		return false;
	}

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
	lastFrameTime_ = std::chrono::steady_clock::now();
	fpsFrameCount_ = 0;

	// メインループ
	while (winApp_.ProcessMessage() && !requestExit_)
	{
		// キーの入力状態を取得
		const Input& input = winApp_.GetInput();

		// ESC でアプリを終了
		if (input.IsTriggered(VK_ESCAPE)) { RequestExit(); }

		// 今の時間を取得
		auto now = std::chrono::steady_clock::now();
		// 前のフレームからの時間 (秒)
		std::chrono::duration<float> elapsed = now - lastFrameTime_;
		lastFrameTime_ = now;

		// ImGui の新しいフレームを始める
		renderer_.BeginImGuiFrame();

		const auto& io = ImGui::GetIO();

		// ImGui がマウスもキーボードも使っていないときだけ
		// どちらとも関数じゃなく、構造体の変数
		if (!io.WantCaptureMouse && !io.WantCaptureKeyboard)
		{
			// カメラを動かす
			camera_.Update(input, elapsed.count());
		}

		// 更新
		renderer_.Update(elapsed.count(), camera_);

		// デバッグ用のUIを表示
		DrawDebugUI();

		// 描画
		if (!renderer_.Render()) { RequestExit(); }

		// fps計測
		{
			++fpsFrameCount_;
			// 2つの時刻の差を 秒(double) で受け取る
			std::chrono::duration<double> fpsElapsed = now - fpsStartTime_;
			// 数値(秒)
			double seconds = fpsElapsed.count();
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
	renderer_.Terminate();
	// COM の終了処理
	CoUninitialize();
}

void Application::RequestExit()
{
	Logger::Info("Exit requested.");
	requestExit_ = true;
}

void Application::DrawDebugUI()
{
	// 初期位置の設定
	ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
	// ウィンドウを開始
	ImGui::Begin("Debug");

	const auto& io = ImGui::GetIO();
	// FPSとフレーム時間(ミリ秒)を表示
	float fps = io.Framerate;
	float frameTime = 1000.0f / fps;
	ImGui::Text("FPS: %.1f (%.2f ms)", fps, frameTime);

	// カメラの位置を表示
	const auto& pos = camera_.GetPosition();
	ImGui::Text("Camera Position: %.2f, %.2f, %.2f", pos.x, pos.y, pos.z);

	// 横線を引く
	ImGui::Separator();

	// 変数の値を変更する
	// カメラの移動速度
	float moveSpeed = camera_.GetMoveSpeed();
	if (ImGui::SliderFloat("Move Speed", &moveSpeed, 0.5f, 20.0f, "%.1f"))
	{
		camera_.SetMoveSpeed(moveSpeed);
	}
	// カメラのマウス感度
	float sens = camera_.GetMouseSensitivity();
	if (ImGui::SliderFloat("Mouse Sensitivity", &sens, 0.001f, 0.01f, "%.4f"))
	{
		camera_.SetMouseSensitivity(sens);
	}
	// 立方体の回転の速度(度数法表記)
	float rotaSpeed = renderer_.GetRotationSpeedRad();
	if (ImGui::SliderAngle("Rotation Speed (deg)", &rotaSpeed, 0.0f, 360.0f, "%.1f deg/s"))
	{
		renderer_.SetRotationSpeedRad(rotaSpeed);
	}
	// 立方体の回転の速度(弧度法表記)
	if (ImGui::SliderFloat("Rotation Speed (rad)", &rotaSpeed, 0.0f, 6.283f, "%.3f rad/s"))
	{
		renderer_.SetRotationSpeedRad(rotaSpeed);
	}
	// 背景色
	// 配列の先頭アドレスを取得
	const float* bg = renderer_.GetBackgroundColor();
	// ローカル変数に写す
	float color[3] = { bg[0], bg[1], bg[2] };
	if (ImGui::ColorEdit3("Background", color))
	{
		renderer_.SetBackgroundColor(color);
	}
	// 光の向き
	auto dir = renderer_.GetLightDirection();
	if (ImGui::SliderFloat3("Light Direction", &dir.x, -1.0f, 1.0f, "%.2f"))
	{
		renderer_.SetLightDirection(dir);
	}

	// ウィンドウを終了
	ImGui::End();
}
