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
	float normal[3] = {};
	float uv[2] = {};
};
// Vertex の大きさが 32バイトではなかったらビルドエラーになる
static_assert(sizeof(Vertex) == 32);

// 定数バッファの中身
// 頂点シェーダーとピクセルシェーダーで読む
struct SceneConstants
{
	DirectX::XMFLOAT4X4 wvp;
	DirectX::XMFLOAT4X4 world;
	DirectX::XMFLOAT3 lightDirection;
	float padding;	// HLSL の16バイト単位に合わせるための詰め物
};

class Win32App;
class Camera;

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
	/// @details rotationY_ を進め、WVP行列とワールド行列を更新する
	/// @param deltaTime 前のフレームからの経過時間(秒)
	/// @param camera 視点のカメラ
	void Update(float deltaTime, const Camera& camera);

	/// @brief ImGui の新しいフレームを始める
	/// @details UI の関数 (ImGui::Begin など) を呼ぶ前に、毎フレーム呼ぶ
	void BeginImGuiFrame();

public:
	/// @brief 現在の向き (rad)
	float GetRotationYRad() const { return rotationY_; }
	/// @brief 現在の向き (degree)
	float GetRotationYDeg() const { return DirectX::XMConvertToDegrees(rotationY_); }

	/// @brief 回転の速さ (rad/秒)
	float GetRotationSpeedRad() const { return rotationSpeed_; }
	/// @brief 回転の速さ (度/秒)
	float GetRotationSpeedDeg() const { return DirectX::XMConvertToDegrees(rotationSpeed_); }
	/// @brief 回転の速さを新しい数値(rad)で置き換える
	/// @param speed 回転の速さ(rad/秒)
	void SetRotationSpeedRad(float speed) { rotationSpeed_ = speed; }
	/// @brief 回転の速さを新しい数値(degree)で置き換える
	/// @param speed 回転の速さ(度/秒)
	void SetRotationSpeedDeg(float speed) { rotationSpeed_ = DirectX::XMConvertToRadians(speed); }

	/// @brief 背景色を返す
	const float* GetBackgroundColor() const { return backgroundColor_; }
	/// @brief 背景色を書き換える
	/// @param color RGBの3つの値
	void SetBackgroundColor(const float* color);

	/// @brief 光の向きを返す
	const DirectX::XMFLOAT3& GetLightDirection() const { return lightDirection_; }
	/// @brief 光の向きを書き換える
	/// @param direction 光が進む向き (ワールド)
	void SetLightDirection(const DirectX::XMFLOAT3& direction);

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

	/// @brief ImGui の初期化
	/// @return 成功したか
	bool InitImGui();

	/// @brief 画像ファイルを読み込み、テクスチャのリソースを作る
	/// @param fileName exe のフォルダから見た画像のパス
	/// @return 成功したか
	bool LoadTexture(const std::wstring& fileName);

	/// @brief SRV 用のヒープから空いている番号を1つ持ってくる
	/// @param cpuHandle CPU 側のハンドルの書き込み先
	/// @param gpuHandle GPU 側のハンドルの書き込み先
	void AllocateSrvDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandle);

	/// @brief 番号を返す
	/// @param cpuHandle 返す番号の CPU 側のハンドル
	void FreeSrvDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle);

private:
	// 描画先のウィンドウ
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

	// 描画するインデックスの数
	UINT indexCount_;

	// テクスチャ (DEFAULT のヒープ。シェーダーが読む画像の本体)
	Microsoft::WRL::ComPtr<ID3D12Resource> texture_;

	// SRV を書く場所 (CPU側)
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvCpuHandle_;

	// 描画で渡す場所 (GPU側)
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvGpuHandle_;

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

	// 回転の速さ (rad/秒)
	float rotationSpeed_;

	// ワールド・ビュー・射影をかけた行列
	DirectX::XMFLOAT4X4 wvpMatrix_;

	// ワールド行列
	DirectX::XMFLOAT4X4 worldMatrix_;

	// 光が進む向き (ワールド空間)
	DirectX::XMFLOAT3 lightDirection_;

	// 定数バッファ
	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, Game::kFrameCount> constantBuffers_;

	// Map で取得した定数バッファの書き込み先
	std::array<SceneConstants*, Game::kFrameCount> mappedConstants_;

	// 背景色 RGBA
	float backgroundColor_[4] = { 0.0f, 1.0f, 1.0f, 1.0f };

	// ImGui とテクスチャの SRV 用のディスクリプタヒープ（シェーダーから見える）
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_;

	// SRV のディスクリプタ1つ分の大きさ (GPUごとに違う)
	UINT srvDescriptorSize_;

	// srvHeap_ の空いている番号の一覧
	std::vector<UINT> freeSrvIndices_;
};
