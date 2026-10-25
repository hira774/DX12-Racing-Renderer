#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>

#include <string>
#include <array>
#include <vector>

#include "Game.h"

struct Vertex
{
	float position[3] = {};
	float color[4] = {};
};
static_assert(sizeof(Vertex) == 28);

// 定数バッファの中身
// 頂点シェーダーで読む
struct SceneConstants
{
	DirectX::XMFLOAT4X4 wvp;
};

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

	/// @brief 毎フレームの処理
	/// @details rotationY_ の値を増やし、 WVP行列を更新する
	/// @param deltaTime 前のフレームからの経過時間(秒)
	void Update(float deltaTime);

private:
	/// @brief 失敗していたらエラーメッセージを出力する
	/// @param hr 調べる先の関数の戻り値
	/// @param errMsg 出力するエラーメッセージ
	/// @return 失敗していたら true
	bool CheckFailed(HRESULT hr, const std::string& errMsg) const;

	/// @brief GPUの処理がすべて終わるのを待つ
	/// @return 成功したか
	bool WaitForGpu();

	/// @brief 指定した値までCPUを待機させる
	/// @param value この値まで待機
	/// @return 成功したか
	bool WaitForFenceValue(UINT64 value);

	/// @brief シェーダー(.cso)をロードする
	/// @param fileName ファイル名
	/// @param shaderCode 読み込んだバイト列の書き込み先
	/// @return 成功したか
	bool LoadShaderFile(const std::wstring& fileName, std::vector<char>& shaderCode) const;

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
	// バックバッファの個数分用意する(GPU が読む間にもう一つのアロケータに CPU が書き込めるようにする)
	std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, Game::kFrameCount> cmdAllocators_;

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

	// DSV用のディスクリプタヒープ
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;

	// 深度バッファ
	Microsoft::WRL::ComPtr<ID3D12Resource> depthBuffer_;

	// バックバッファ
	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, Game::kFrameCount> backBuffers_;

	// フェンス
	// GPUの進み具合を表す
	Microsoft::WRL::ComPtr<ID3D12Fence> fence_;

	// 最後に Signal で指定した値 (CPUが待つ時の目標値)
	// unsigned long long
	UINT64 fenceValue_;

	// バックバッファごとに、最後に Signal した値
	// (GPU がこの値まで進めば、そのアロケータを Reset できる)
	std::array<UINT64, Game::kFrameCount> frameFenceValues_;

	// フェンスイベント
	// GPUの処理の完了を待つ
	HANDLE fenceEvent_;

	// ティアリングに対応しているか
	bool isTearingSupported_;

	// 垂直同期を使用するか
	bool isVSync_;

	// 頂点バッファ
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;

	// 頂点バッファビュー
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_;

	// インデックスバッファ
	Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;

	// インデックスバッファビュー
	D3D12_INDEX_BUFFER_VIEW indexBufferView_;

	// ルートシグネチャ
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;

	// パイプラインステート
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

	// ビューポート
	D3D12_VIEWPORT viewport_;

	// シザー矩形
	D3D12_RECT scissorRect_;

	// 今の角度
	float rotationY_;

	// ワールド・ビュー・射影をかけた行列
	DirectX::XMFLOAT4X4 wvpMatrix_;

	// 定数バッファ
	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, Game::kFrameCount> constantBuffers_;

	// Map で取得した定数バッファの書き込み先
	std::array<SceneConstants*, Game::kFrameCount> mappedConstants_;

	// 背景色 RGBA
	float backgroundColor_[4] = { 0.0f, 1.0f, 1.0f, 1.0f };
};
