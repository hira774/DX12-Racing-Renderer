#include "Renderer.h"

#include <format>
#include <cstring>
#include <filesystem>
#include <fstream>

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
	isVSync_(false),
	vertexBufferView_{},
	viewport_{},
	scissorRect_{}
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

	// 頂点バッファの作成
	{
		Vertex triangle[3] =
		{
			{ { 0.0f, 0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
			{ { 0.5f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
			{ { -0.5f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
		};

		D3D12_HEAP_PROPERTIES heapProps = {};
		// CPUから書き込める
		heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
		heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		// GPU が1つなら1
		heapProps.CreationNodeMask = 1;
		heapProps.VisibleNodeMask = 1;

		// リソースの設定
		D3D12_RESOURCE_DESC resourceDesc = {};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		// バッファでは 0 か 64KB しか使えない (0は既定の値(64KB)を使用する)
		resourceDesc.Alignment = 0;
		// 頂点の配列のバイト数を代入 (84Byte)
		resourceDesc.Width = sizeof(triangle);
		// バッファでは必ず1
		resourceDesc.Height = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		// バッファでは1
		resourceDesc.SampleDesc.Count = 1;
		// バッファでは0
		resourceDesc.SampleDesc.Quality = 0;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		// リソースと、リソースを置くためのメモリを作成
		hr = device_->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&vertexBuffer_)
		);
		if (CheckFailed(hr, "Failed to create vertex buffer.")) { return false; }

		// CPU は読まないので空
		D3D12_RANGE readRange = { 0, 0 };

		void* mappedResource = nullptr;
		hr = vertexBuffer_->Map(0, &readRange, &mappedResource);
		if (CheckFailed(hr, "Failed to map vertex buffer.")) { return false; }
		// 頂点データを GPU から見えるメモリにコピー
		std::memcpy(mappedResource, triangle, sizeof(triangle));
		vertexBuffer_->Unmap(0, nullptr);
		// バッファビューの設定
		vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
		vertexBufferView_.StrideInBytes = static_cast<UINT>(sizeof(Vertex));
		vertexBufferView_.SizeInBytes = static_cast<UINT>(sizeof(triangle));
	}

	// シェーダーの読み込み
	std::vector<char> vsCode;
	std::vector<char> psCode;
	if (!LoadShaderFile(L"BasicVS.cso", vsCode)) { return false; }
	if (!LoadShaderFile(L"BasicPS.cso", psCode)) { return false; }

	// ルートシグネチャの作成
	// シリアライズの結果用
	ComPtr<ID3DBlob> blob;
	// エラーメッセージ用
	ComPtr<ID3DBlob> errorBlob;
	// ルートシグネチャの設定
	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
	rootSignatureDesc.NumParameters = 0;
	rootSignatureDesc.pParameters = nullptr;
	rootSignatureDesc.NumStaticSamplers = 0;
	rootSignatureDesc.pStaticSamplers = nullptr;
	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// ルートシグネチャのシリアライズ
	hr = D3D12SerializeRootSignature(
		&rootSignatureDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		&blob,
		&errorBlob
	);
	if (FAILED(hr))
	{
		std::string errMsg = "Failed to serialize root signature.";
		if (errorBlob)
		{
			errMsg += " ";
			errMsg += static_cast<const char*>(errorBlob->GetBufferPointer());
		}
		Logger::Error(errMsg);
		return false;
	}

	hr = device_->CreateRootSignature(
		0,
		blob->GetBufferPointer(),
		blob->GetBufferSize(),
		IID_PPV_ARGS(&rootSignature_)
	);
	if (CheckFailed(hr, "Failed to create root signature.")) { return false; }

	// 入力レイアウト
	// IA が頂点バッファを読むときにどれをシェーダーのどの入力に入れるかを決める
	D3D12_INPUT_ELEMENT_DESC inputElements[] =
	{
		// 位置
		{
			"POSITION",						// セマンティクスの名前
			0,								// セマンティクスの番号
			DXGI_FORMAT_R32G32B32_FLOAT,	// フォーマット。32ビットの float が3つ
			0,								// 入力スロット 0 ~ 31
			0,								// 先頭からこの要素までのオフセット(バイト)
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,	// 入力データクラス。頂点ごとのデータ
			0
		},
		// 色
		{
			"COLOR",
			0,	// 同じ名前を複数使用する場合は変更する
			DXGI_FORMAT_R32G32B32A32_FLOAT,	// 32ビットの float が4つ
			0,
			12,	// 12バイトずらす ( = 32ビット × 3つ)
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
			0
		}
	};

	// パイプラインステートの設定
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	// 三角形に関係するもの
	psoDesc.pRootSignature = rootSignature_.Get();
	psoDesc.VS = { vsCode.data(), vsCode.size() };
	psoDesc.PS = { psCode.data(), psCode.size() };
	psoDesc.InputLayout = { inputElements, _countof(inputElements) };
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;
	// ラスタライザ
	psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;	// 裏面は描画しない
	psoDesc.RasterizerState.FrontCounterClockwise = FALSE;		// 時計回りが表
	psoDesc.RasterizerState.DepthClipEnable = TRUE;
	// ラスタライザの残りは 0のまま
	// ブレンド
	psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
	psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
	psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
	psoDesc.BlendState.IndependentBlendEnable = FALSE;
	// 深度ステンシル
	psoDesc.DepthStencilState.DepthEnable = FALSE;
	psoDesc.DepthStencilState.StencilEnable = FALSE;

	hr = device_->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState_));
	if (CheckFailed(hr, "Failed to create pipeline state.")) { return false; }

	// ビューポートの設定
	viewport_.TopLeftX = 0.0f;
	viewport_.TopLeftY = 0.0f;
	viewport_.Width = static_cast<FLOAT>(window_.GetWindowWidth());
	viewport_.Height = static_cast<FLOAT>(window_.GetWindowHeight());
	viewport_.MinDepth = 0.0f;
	viewport_.MaxDepth = 1.0f;

	// シザー矩形の設定
	scissorRect_.left = 0;
	scissorRect_.top = 0;
	scissorRect_.right = static_cast<LONG>(window_.GetWindowWidth());
	scissorRect_.bottom = static_cast<LONG>(window_.GetWindowHeight());






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

	cmdList_->SetGraphicsRootSignature(rootSignature_.Get());
	cmdList_->SetPipelineState(pipelineState_.Get());
	// ビューポートを使用する
	cmdList_->RSSetViewports(1, &viewport_);
	// シザー矩形を使用する
	cmdList_->RSSetScissorRects(1, &scissorRect_);
	// 頂点の並べ方を設定
	cmdList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList_->IASetVertexBuffers(0, 1, &vertexBufferView_);
	// 頂点を何個読むか、それを何個描くか
	// DrawInstanced(頂点数、インスタンス数、開始頂点、開始インスタンス)
	cmdList_->DrawInstanced(3, 1, 0, 0);




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

bool Renderer::LoadShaderFile(const std::wstring& fileName, std::vector<char>& shaderCode) const
{
	wchar_t exePath[MAX_PATH]{};
	// exeのパスを取得
	DWORD length = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
	// 0 が返ってきたなら失敗
	// MAX_PATH と同じなら途中で切れている
	if (length == 0 || length == MAX_PATH)
	{
		Logger::Error("Failed to get module file name.");
		return false;
	}
	// プログラムファイルのパスを作成
	std::filesystem::path filePath = std::filesystem::path(exePath).parent_path() / fileName;

	// エラーコードを格納。デフォルト値は 0
	std::error_code ec;
	// ファイルの大きさを取得
	auto fileSize = std::filesystem::file_size(filePath, ec);
	// エラーが発生した場合
	if (ec)
	{
		Logger::Error(
			"Failed to get file size. " +
			StringUtils::Utf16ToUtf8(fileName) +
			" " + ec.message()
		);
		return false;
	}
	// 取得した大きさにリサイズ
	shaderCode.resize(fileSize);

	// ファイルを開く
	// std::ios::binary 中身をそのまま読む
	std::ifstream file(filePath, std::ios::binary);
	// 開けたか確認
	if (!file)
	{
		// ファイルパスに日本語が含まれていた時のために
		// UTF-16 でファイルパスを取得するし、UTF-8 に変換して Logger に渡す
		Logger::Error("Failed to open shader file. " + StringUtils::Utf16ToUtf8(filePath.wstring()));
		return false;
	}
	// ファイルを読む
	file.read(shaderCode.data(), static_cast<std::streamsize>(fileSize));
	// 指定したバイト数を読めたか確認
	if (!file)
	{
		Logger::Error("Failed to read shader file. " + StringUtils::Utf16ToUtf8(filePath.wstring()));
		return false;
	}

	Logger::Info("Loaded shader file: " + StringUtils::Utf16ToUtf8(filePath.wstring()));
	return true;
}
