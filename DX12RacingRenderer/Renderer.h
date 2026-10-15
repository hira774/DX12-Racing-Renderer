#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <dxgi1_6.h>

#include <string>
#include <array>

#include "Game.h"

class Win32App;
class Renderer
{
public:
	/// @brief コンストラクタ
	/// @param winApp 描画先のウィンドウ
	Renderer(Win32App& winApp);

	/// @brief 初期化処理
	/// @return 成功したか
	bool Init();

	/// @brief 描画処理
	/// @return 成功したか
	bool Render();

	/// @brief 終了処理
	/// @return 成功したか
	bool Terminate();

private:
	/// @brief 失敗していたらエラーメッセージを出力する
	/// @param hr 調べる先の関数の戻り値
	/// @param errMsg 出力するエラーメッセージ
	/// @return 失敗していたら true
	bool CheckFailed(HRESULT hr, const std::string& errMsg) const;

	/// @brief GPUの処理がすべて終わるのを待つ
	/// @return 成功したか
	bool WaitForGpu();

private:
	Win32App& window_;

	// デバイス
	// Direct3D12 のオブジェクトの作成元
	Microsoft::WRL::ComPtr<ID3D12Device> device_;

	// コマンドキュー
	// GPUに命令を渡す
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> cmdQueue_;

	// コマンドアロケータ
	// 記録した命令をメモリに置く
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> cmdAllocator_;
	// コマンドリスト
	// 命令を記録する
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmdList_;

	// スワップチェーン
	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;

	// レンダーターゲットビューを並べて置くヒープ
	// バックバッファ1枚につき1つ
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;

	// RTVのディスクリプタ1つ分の大きさ (GPUごとに違う)
	UINT rtvDescriptorSize_;

	// バックバッファ
	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, Game::kFrameCount> backBuffers_;

	// フェンス
	// GPUの進み具合を表す
	Microsoft::WRL::ComPtr<ID3D12Fence> fence_;

	// 最後に Signal で指定した値 (CPUが待つ時の目標値)
	// unsigned long long
	UINT64 fenceValue_;

	// フェンスイベント
	// GPUの処理の完了を待つ
	HANDLE fenceEvent_;

	// 背景色 RGBA
	float backgroundColor_[4] = { 0.0f, 1.0f, 1.0f, 1.0f };
};
