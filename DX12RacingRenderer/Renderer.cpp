#include "Renderer.h"

#include <format>

#include "Win32App.h"
#include "Logger.h"
#include "StringUtils.h"

using Microsoft::WRL::ComPtr;

Renderer::Renderer(Win32App& winApp) :
	window_(winApp),
	rtvDescriptorSize_(0),
	fenceValue_(0),
	frameFenceValues_{},
	fenceEvent_(nullptr),
	isTearingSupported_(false),
	isVSync_(false)
{
}

bool Renderer::Init()
{
	UINT factoryFlags = 0;
	HRESULT hr = S_OK;

#ifdef _DEBUG
	// デバッグレイヤーを有効にする
	ComPtr<ID3D12Debug> debug;
	hr = D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
	if (SUCCEEDED(hr))
	{
		debug->EnableDebugLayer();
		factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
	}
	else
	{
		Logger::Warn("Failed to enable debug layer.");
	}
#endif // _DEBUG

	// アダプタの列挙とスワップチェーンの作成に使う
	ComPtr<IDXGIFactory6> factory;
	hr = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory));
	if (CheckFailed(hr, "Failed to create DXGI factory.")) { return false; }

	// ティアリングに対応しているか
	BOOL tearingSupported = FALSE;
	hr = factory->CheckFeatureSupport(
		DXGI_FEATURE_PRESENT_ALLOW_TEARING,
		&tearingSupported,
		sizeof(tearingSupported)
	);
	if (FAILED(hr))
	{
		Logger::Warn("Failed to check feature support.");
		tearingSupported = FALSE;
	}

	// 対応の可否をメンバに保存
	isTearingSupported_ = static_cast<bool>(tearingSupported);

	// アダプタの列挙
	ComPtr<IDXGIAdapter1> adapter;
	for (
		UINT adapterIndex = 0;
		SUCCEEDED(factory->EnumAdapterByGpuPreference(
			adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)));
		++adapterIndex)
	{
		DXGI_ADAPTER_DESC1 desc = {};
		adapter->GetDesc1(&desc);

		// ソフトウェアアダプタの判定
		if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
		{
			continue;
		}

		std::wstring strAdapter = desc.Description;
		Logger::Info("adapter name = " + StringUtils::Utf16ToUtf8(strAdapter));

		break;
	}

	// 使えるアダプタがなかった場合はエラーにする
	if (adapter == nullptr)
	{
		Logger::Error("No available adapter found.");
		return false;
	}

	// デバイスの作成（機能レベル 12_0 以上に対応した GPU が必要）
	hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device_));
	if (CheckFailed(hr, "Failed to create D3D12 device.")) { return false; }

	Logger::Info("Created D3D12 device.");

	/* コマンド関連の初期化 */
	// コマンドキュー
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	hr = device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&cmdQueue_));
	if (CheckFailed(hr, "Failed to create command queue.")) { return false; }

	// コマンドアロケータ
	for (UINT i = 0; i < Game::kFrameCount; ++i)
	{
		hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAllocators_[i]));
		if (CheckFailed(hr, "Failed to create command allocator.")) { return false; }
	}

	// コマンドリスト
	hr = device_->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		cmdAllocators_[0].Get(),
		nullptr,
		IID_PPV_ARGS(&cmdList_)
	);
	if (CheckFailed(hr, "Failed to create command list.")) { return false; }

	// リストは開いた状態で作成されるので Close しておく
	cmdList_->Close();

	// スワップチェーン
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	// バックバッファの大きさ
	swapChainDesc.Width = window_.GetWindowWidth();
	swapChainDesc.Height = window_.GetWindowHeight();
	// RGBA 各成分が 8bit で格納される
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	// マルチサンプルなし
	swapChainDesc.SampleDesc.Count = 1;
	// 描画先として使用する
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	// バックバッファの数
	swapChainDesc.BufferCount = Game::kFrameCount;
	// 入れ替えの方式(フリップ)
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	// 対応している場合のみティアリングを許容する
	if (isTearingSupported_)
	{
		swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
	}
	else
	{
		// 許容しない
		swapChainDesc.Flags = 0;
	}

	ComPtr<IDXGISwapChain1> swapChain1;
	hr = factory->CreateSwapChainForHwnd(
		cmdQueue_.Get(),
		window_.GetHwnd(),
		&swapChainDesc,
		nullptr,
		nullptr,
		&swapChain1
	);
	if (CheckFailed(hr, "Failed to create swap chain.")) { return false; }

	// SwapChain4 を取得する
	hr = swapChain1.As(&swapChain_);
	if (CheckFailed(hr, "Failed to cast swap chain.")) { return false; }

	// Alt + Enter を無効化
	hr = factory->MakeWindowAssociation(window_.GetHwnd(), DXGI_MWA_NO_ALT_ENTER);
	if (CheckFailed(hr, "Failed to disable Alt + Enter.")) { return false; }

	// RTVの作成
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.NumDescriptors = Game::kFrameCount;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	hr = device_->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap_));
	if (CheckFailed(hr, "Failed to create RTV descriptor heap.")) { return false; }

	rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	for (UINT i = 0; i < Game::kFrameCount; ++i)
	{
		hr = swapChain_->GetBuffer(i, IID_PPV_ARGS(&backBuffers_[i]));
		if (CheckFailed(hr, "Failed to get back buffer.")) { return false; }
		// rtvハンドルを作成
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
		rtvHandle.ptr += static_cast<SIZE_T>(i) * rtvDescriptorSize_;
		device_->CreateRenderTargetView(backBuffers_[i].Get(), nullptr, rtvHandle);
	}

	// フェンスの生成
	hr = device_->CreateFence(fenceValue_, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
	if (CheckFailed(hr, "Failed to create fence.")) { return false; }

	// フェンスイベントの作成
	// 失敗した場合は nullptr を返す
	fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (fenceEvent_ == nullptr)
	{
		Logger::Error("Failed to create event.");
		return false;
	}

	// GPUの処理が終わるまでCPUを待機させる
	if (!WaitForGpu()) { return false; }

	return true;
}

bool Renderer::Render()
{
	HRESULT hr = S_OK;
	// 描画先の選択 (毎フレーム変わる)
	UINT frameIndex = swapChain_->GetCurrentBackBufferIndex();

	// このフレームのアロケータの命令が終わるまで待つ
	if (!WaitForFenceValue(frameFenceValues_[frameIndex])) { return false; }

	/* コマンドの記録を空にする */
	// 命令が置かれていたメモリを空にして使い回す
	hr = cmdAllocators_[frameIndex]->Reset();
	if (CheckFailed(hr, "Failed to reset command allocator.")) { return false; }
	// リストを記録中の状態に戻す
	hr = cmdList_->Reset(cmdAllocators_[frameIndex].Get(), nullptr);
	if (CheckFailed(hr, "Failed to reset command list.")) { return false; }

	// リソースバリアの設定
	// バックバッファの状態を表示に使用できる状態から描画に使用できる状態に変更する という命令をリストに記録
	D3D12_RESOURCE_BARRIER barrier1 = {};
	// 状態を変更するリソースバリアの種類
	barrier1.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier1.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	/* D3D12_RESOURCE_TRANSITION_BARRIER 構造体 */
	// 変更するリソース
	barrier1.Transition.pResource = backBuffers_[frameIndex].Get();
	// すべてのサブリソースを同時に移行する
	barrier1.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	// 変更前の状態
	barrier1.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;			// 表示に使用できる状態
	// 変更後の状態
	barrier1.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;	// 描画に使用できる状態

	// バリアを記録
	cmdList_->ResourceBarrier(1, &barrier1);

	// RTVのハンドルを計算
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
	rtvHandle.ptr += static_cast<SIZE_T>(frameIndex) * rtvDescriptorSize_;
	// 描画の出力先を指定する
	cmdList_->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	// 単色で塗りつぶす
	cmdList_->ClearRenderTargetView(rtvHandle, backgroundColor_, 0, nullptr);

	// バリア2を記録
	D3D12_RESOURCE_BARRIER barrier2 = barrier1;
	barrier2.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;	// 描画に使用できる状態
	barrier2.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;			// 表示に使用できる状態

	cmdList_->ResourceBarrier(1, &barrier2);

	// cmdListを閉じる
	hr = cmdList_->Close();
	// コマンドリストの記録に間違いがないかを確認
	if (CheckFailed(hr, "Failed to close command list.")) { return false; }

	// コマンドリストをGPUに渡す
	ID3D12CommandList* lists[] = { cmdList_.Get() };
	cmdQueue_->ExecuteCommandLists(1, lists);

	// 垂直同期 ON の値で初期化
	UINT syncInterval = 1;
	UINT presentFlags = 0;

	// 垂直同期を行わない場合
	if (!isVSync_)
	{
		syncInterval = 0;
		// ティアリングを許可する
		if (isTearingSupported_) { presentFlags = DXGI_PRESENT_ALLOW_TEARING; }
	}

	// 画面を入れ替える
	hr = swapChain_->Present(syncInterval, presentFlags);
	if (CheckFailed(hr, "Failed to present.")) { return false; }

	// フェンスの値を1増やす
	++fenceValue_;
	// コマンドキューにシグナルを追加
	hr = cmdQueue_->Signal(fence_.Get(), fenceValue_);
	if (CheckFailed(hr, "Failed to add signal to the command queue.")) { return false; }
	// このフレームのアロケータを Reset してよくなる値
	frameFenceValues_[frameIndex] = fenceValue_;

	// 更新されているか確認用
	{
		backgroundColor_[0] += 0.005f;
		if (backgroundColor_[0] > 1.0f) { backgroundColor_[0] = 0.0f; }
	}

	return true;
}

bool Renderer::Terminate()
{
	bool flag = true;

	// GPUの処理が終わるまでCPUを待機
	// 使用中のオブジェクトを解放することはできない
	if (!WaitForGpu()) { flag = false; }

	// 開いている fenceEvent_ を閉じる
	if (CloseHandle(fenceEvent_) == 0) { flag = false; }

	return flag;
}

bool Renderer::CheckFailed(HRESULT hr, const std::string& errMsg) const
{
	if (FAILED(hr))
	{
		// エラーの種類も出力する
		Logger::Error(errMsg + std::format(" (HRESULT = 0x{:08X})", static_cast<unsigned int>(hr)));
		return true;
	}

	return false;
}

bool Renderer::WaitForGpu()
{
	// フェンスの値を1つ増やす
	++fenceValue_;
	// コマンドキューにシグナルを追加
	HRESULT hr = cmdQueue_->Signal(fence_.Get(), fenceValue_);
	if (CheckFailed(hr, "Failed to add signal to the command queue.")) { return false; }

	// GPUの処理が終わっていなければCPUを待機
	if (!WaitForFenceValue(fenceValue_)) { return false; }

	return true;
}

bool Renderer::WaitForFenceValue(UINT64 value)
{
	HRESULT hr = S_OK;

	// まだGPUの処理が終わっていなければ
	// (GPUが最後に書き込んだ値がvalueより小さい)
	if (fence_->GetCompletedValue() < value)
	{
		// value の値になったらイベントを知らせる
		hr = fence_->SetEventOnCompletion(value, fenceEvent_);
		if (CheckFailed(hr, "Failed to set fence event.")) { return false; }

		// イベントが知らされるまでCPUを止める
		// INFINITE = 時間切れなし
		WaitForSingleObject(fenceEvent_, INFINITE);
	}

	return true;
}
