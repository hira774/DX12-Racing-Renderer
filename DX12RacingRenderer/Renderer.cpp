#include "Renderer.h"

#include <format>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include <DirectXTex.h>

#include "Win32App.h"
#include "Logger.h"
#include "StringUtils.h"
#include "Camera.h"
#include "ObjLoader.h"

using Microsoft::WRL::ComPtr;

namespace
{
	// 定数バッファの大きさ(256バイトの倍数に切り上げ)
	constexpr size_t kConstantBufferSize = (sizeof(SceneConstants) + 255) & ~255;

	/* カメラの設定 */
	// 視野角 45° (degree)
	constexpr float kFovAngle = 45.0f;
	// カメラからニアクリッピング面までの距離
	constexpr float kNearClipping = 0.1f;
	// カメラからファークリッピング面までの距離
	constexpr float kFarClipping = 100.0f;

	// ImGui とテクスチャ用の SRV ヒープに置くディスクリプタの数
	constexpr UINT kSrvHeapSize = 64;
}

Renderer::Renderer(Win32App& winApp) :
	window_(winApp),
	rtvDescriptorSize_(0),
	fenceValue_(0),
	frameFenceValues_{},
	fenceEvent_(nullptr),
	isTearingSupported_(false),
	isVSync_(false),
	vertexBufferView_{},
	indexBufferView_{},
	indexCount_(0),
	textureSrvCpuHandle_{},
	textureSrvGpuHandle_{},
	viewport_{},
	scissorRect_{},
	rotationY_(0.0f),
	rotationSpeed_(1.57f),
	wvpMatrix_{},
	worldMatrix_{},
	lightDirection_{ 1.0f, -1.0f, 1.0f },
	constantBuffers_{},
	mappedConstants_{},
	srvDescriptorSize_(0)
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

	// DSVの作成
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	hr = device_->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap_));
	if (CheckFailed(hr, "Failed to create DSV descriptor heap.")) { return false; }

	D3D12_HEAP_PROPERTIES depthHeapProps = {};
	depthHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;	// GPU専用のメモリ (CPUからは書き込まない)
	depthHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	depthHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	depthHeapProps.CreationNodeMask = 1;
	depthHeapProps.VisibleNodeMask = 1;

	// 最適化クリア値
	// GPUのクリアを速くできる
	D3D12_CLEAR_VALUE depthClearValue = {};
	depthClearValue.Format = DXGI_FORMAT_D32_FLOAT;
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.DepthStencil.Stencil = 0;

	D3D12_RESOURCE_DESC depthResourceDesc = {};
	depthResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthResourceDesc.Width = window_.GetWindowWidth();
	depthResourceDesc.Height = window_.GetWindowHeight();
	depthResourceDesc.Format = DXGI_FORMAT_D32_FLOAT;
	depthResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	depthResourceDesc.Alignment = 0;
	depthResourceDesc.DepthOrArraySize = 1;
	depthResourceDesc.MipLevels = 1;
	depthResourceDesc.SampleDesc.Count = 1;
	depthResourceDesc.SampleDesc.Quality = 0;

	// リソースとリソースを置くためのメモリを作成
	hr = device_->CreateCommittedResource(
		&depthHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&depthResourceDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,	// 深度バッファは描画のたびに書き込む
		&depthClearValue,
		IID_PPV_ARGS(&depthBuffer_)
	);
	if (CheckFailed(hr, "Failed to create depth buffer.")) { return false; }

	// DSVを作成
	device_->CreateDepthStencilView(
		depthBuffer_.Get(),
		nullptr,
		dsvHeap_->GetCPUDescriptorHandleForHeapStart()
	);

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

	// 頂点バッファとインデックスバッファの作成
	{
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		if (!LoadObj(L"Assets/Cube.obj", vertices, indices)) { return false; }
		//if (!LoadObj(L"Assets/Sphere.obj", vertices, indices)) { return false; }

		// 頂点の中身のバイト数
		const UINT64 vertexBufferSize = vertices.size() * sizeof(Vertex);
		// インデックスの中身のバイト数
		const UINT64 indexBufferSize = indices.size() * sizeof(uint32_t);
		// 描画するインデックスの数
		indexCount_ = static_cast<UINT>(indices.size());

		// ヒープの設定
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
		// 頂点データのバイト数を代入
		resourceDesc.Width = vertexBufferSize;
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
		std::memcpy(mappedResource, vertices.data(), vertexBufferSize);
		vertexBuffer_->Unmap(0, nullptr);
		// バッファビューの設定
		vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
		vertexBufferView_.StrideInBytes = static_cast<UINT>(sizeof(Vertex));
		vertexBufferView_.SizeInBytes = static_cast<UINT>(vertexBufferSize);

		// インデックスバッファの作成
		// 大きさだけを変えて使い回す
		resourceDesc.Width = indexBufferSize;
		// リソースとリソースを置くためのメモリを作成
		hr = device_->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&indexBuffer_)
		);
		if (CheckFailed(hr, "Failed to create index buffer.")) { return false; }

		void* mappedIndexResource = nullptr;
		hr = indexBuffer_->Map(0, &readRange, &mappedIndexResource);
		if (CheckFailed(hr, "Failed to map index buffer.")) { return false; }
		// 頂点のインデックスを GPU から見えるメモリにコピー
		std::memcpy(mappedIndexResource, indices.data(), indexBufferSize);
		indexBuffer_->Unmap(0, nullptr);
		// バッファビューの設定
		indexBufferView_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
		indexBufferView_.SizeInBytes = static_cast<UINT>(indexBufferSize);
		indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
	}

	// テクスチャの読み込み
	if (!LoadTexture(L"Assets/Checker.png")) { return false; }

	// 定数バッファの作成(バックバッファの枚数分)
	for (UINT i = 0; i < Game::kFrameCount; ++i)
	{
		// ヒープの設定
		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
		heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapProps.CreationNodeMask = 1;
		heapProps.VisibleNodeMask = 1;

		// リソースの設定
		D3D12_RESOURCE_DESC resourceDesc = {};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Alignment = 0;
		resourceDesc.Width = kConstantBufferSize;
		resourceDesc.Height = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		resourceDesc.SampleDesc.Count = 1;
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
			IID_PPV_ARGS(&constantBuffers_[i])
		);
		if (CheckFailed(hr, "Failed to create constant buffer.")) { return false; }

		D3D12_RANGE readRange = { 0, 0 };
		hr = constantBuffers_[i]->Map(
			0,
			&readRange,
			reinterpret_cast<void**>(&mappedConstants_[i])
		);
		if (CheckFailed(hr, "Failed to map constant buffer.")) { return false; }
	}

	// シェーダーの読み込み
	std::vector<char> vsCode;
	std::vector<char> psCode;
	if (!LoadShaderFile(L"BasicVS.cso", vsCode)) { return false; }
	if (!LoadShaderFile(L"BasicPS.cso", psCode)) { return false; }

	// ルートパラメータの作成
	D3D12_ROOT_PARAMETER rootParameters[2] = {};
	// CBV
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[0].Descriptor.ShaderRegister = 0;
	rootParameters[0].Descriptor.RegisterSpace = 0;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;	// すべてのシェーダーから見える

	// SRVのテーブルの中身の形を作成
	D3D12_DESCRIPTOR_RANGE srvRange = {};
	srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;	// SRV の範囲
	srvRange.NumDescriptors = 1;		// 1個
	srvRange.BaseShaderRegister = 0;	// t0
	srvRange.RegisterSpace = 0;
	srvRange.OffsetInDescriptorsFromTableStart = 0;			// テーブルの先頭から
	// SRV用のルートパラメータ
	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
	rootParameters[1].DescriptorTable.pDescriptorRanges = &srvRange;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;	// ピクセルシェーダーから見える

	// 静的サンプラを作成
	D3D12_STATIC_SAMPLER_DESC staticSampler = {};
	staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	// UVの値が 0~1 の範囲外の時、繰り返す
	staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	// 使用しない
	staticSampler.MaxAnisotropy = 0;
	staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	staticSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	staticSampler.MinLOD = 0.0f;
	staticSampler.MaxLOD = D3D12_FLOAT32_MAX;
	// シェーダーの設定
	staticSampler.ShaderRegister = 0;	// s0
	staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;	// ピクセルシェーダーから見える

	// ルートシグネチャの作成
	// シリアライズの結果用
	ComPtr<ID3DBlob> blob;
	// エラーメッセージ用
	ComPtr<ID3DBlob> errorBlob;
	// ルートシグネチャの設定
	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
	rootSignatureDesc.NumParameters = _countof(rootParameters);	// ルートパラメータの個数
	rootSignatureDesc.pParameters = rootParameters;	// ルートパラメータをルートシグネチャにわたす
	rootSignatureDesc.NumStaticSamplers = 1;		// 静的サンプラの個数
	rootSignatureDesc.pStaticSamplers = &staticSampler;
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
		// 法線
		{
			"NORMAL",
			0,	// 同じ名前を複数使用する場合は変更する
			DXGI_FORMAT_R32G32B32_FLOAT,	// 32ビットの float が3つ
			0,
			12,	// 12バイトずらす ( = 32ビット × 3つ)
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
			0
		},
		// UV
		{
			"TEXCOORD",
			0,
			DXGI_FORMAT_R32G32_FLOAT,	// 32ビットの float が2つ
			0,
			24, // POSITION + NORMAL 分のオフセット
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
			0
		}
	};

	// パイプラインステートの設定
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	// シェーダーと入出力の形式
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
	psoDesc.DepthStencilState.DepthEnable = TRUE;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

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

	// ImGui の初期化
	if (!InitImGui()) { return false; }

	// SRV を作る
	// ヒープの空いている番号を探す
	AllocateSrvDescriptor(&textureSrvCpuHandle_, &textureSrvGpuHandle_);

	// SRV の設定
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = texture_->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;	// RGBAをそのまま渡す
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;	// ミップマップは無し
	device_->CreateShaderResourceView(
		texture_.Get(),
		&srvDesc,
		textureSrvCpuHandle_
	);

	return true;
}

bool Renderer::Render()
{
	HRESULT hr = S_OK;
	// 描画先の選択 (毎フレーム変わる)
	UINT frameIndex = swapChain_->GetCurrentBackBufferIndex();

	// このフレームのアロケータの命令が終わるまで待つ
	if (!WaitForFenceValue(frameFenceValues_[frameIndex])) { return false; }

	/* 定数バッファに WVP・worldMatrix・lightDirection を書き込む */
	// XMFLOAT4X4 を XMMATRIX に変換
	const auto wvp = DirectX::XMLoadFloat4x4(&wvpMatrix_);
	const auto world = DirectX::XMLoadFloat4x4(&worldMatrix_);

	// HLSL の cbuffer では行列は列優先なので転置しておく
	// (平行移動の成分が、DirectXMath は4行目、HLSL では4列目に見える)
	const auto transposedWvp = DirectX::XMMatrixTranspose(wvp);
	const auto transposedWorld = DirectX::XMMatrixTranspose(world);

	// 定数バッファに書き込む
	// XMStoreFloat4x4(書き込み先のアドレス, 転置した行列)
	// wvp行列
	DirectX::XMStoreFloat4x4(&mappedConstants_[frameIndex]->wvp, transposedWvp);
	// world行列
	DirectX::XMStoreFloat4x4(&mappedConstants_[frameIndex]->world, transposedWorld);
	// 光の向き
	// ベクトルなので転置しなくていい
	mappedConstants_[frameIndex]->lightDirection = lightDirection_;

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

	// DSVのハンドルを作成
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();

	// 描画の出力先を指定する
	cmdList_->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

	// 単色で塗りつぶす
	cmdList_->ClearRenderTargetView(rtvHandle, backgroundColor_, 0, nullptr);

	// 深度を塗りつぶす
	cmdList_->ClearDepthStencilView(
		dsvHandle,
		D3D12_CLEAR_FLAG_DEPTH,
		1.0f,
		0,
		0,
		nullptr
	);

	// テクスチャと ImGui の SRV があるヒープを設定する
	ID3D12DescriptorHeap* heaps[] = { srvHeap_.Get() };
	cmdList_->SetDescriptorHeaps(1, heaps);

	// ルートシグネチャを設定
	cmdList_->SetGraphicsRootSignature(rootSignature_.Get());

	// ルートパラメータの0番目にこのフレームの定数バッファのGPUアドレスを設定
	cmdList_->SetGraphicsRootConstantBufferView(0, constantBuffers_[frameIndex]->GetGPUVirtualAddress());

	// ルートパラメータの1番目にテクスチャの SRVテーブルを設定
	cmdList_->SetGraphicsRootDescriptorTable(1, textureSrvGpuHandle_);

	cmdList_->SetPipelineState(pipelineState_.Get());
	// ビューポートを使用する
	cmdList_->RSSetViewports(1, &viewport_);
	// シザー矩形を使用する
	cmdList_->RSSetScissorRects(1, &scissorRect_);
	// 頂点の並べ方を設定
	cmdList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList_->IASetVertexBuffers(0, 1, &vertexBufferView_);
	cmdList_->IASetIndexBuffer(&indexBufferView_);
	// インデックスを何個読むか、それを何個描くか
	// DrawIndexedInstanced(インデックス数、インスタンス数、開始インデックス、頂点番号に足す数、開始インスタンス)
	cmdList_->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);

	/* ImGui を描く */
	// UI の描画データを作る (CPU の処理)
	ImGui::Render();
	// 描画データを DX12 の命令にして、コマンドリストに記録する
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList_.Get());

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

	// ImGui の終了処理
	// 作ったのと逆の順
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	// 開いている fenceEvent_ を閉じる
	if (CloseHandle(fenceEvent_) == 0) { flag = false; }

	return flag;
}

void Renderer::Update(float deltaTime, const Camera& camera)
{
	// 画面の縦横比 (幅/高さ)
	float aspectRatio = window_.GetWindowWidthF() / window_.GetWindowHeightF();

	// 回転させる
	rotationY_ += rotationSpeed_ * deltaTime;
	// 2πを超えたら2πを引く
	if (rotationY_ > DirectX::XM_2PI)
	{
		rotationY_ -= DirectX::XM_2PI;
	}

	// ワールド行列
	const auto worldMatrix = DirectX::XMMatrixRotationY(rotationY_);
	// ビュー行列
	const auto viewMatrix = camera.GetViewMatrix();
	// 射影行列
	const auto projectionMatrix = DirectX::XMMatrixPerspectiveFovLH(
		DirectX::XMConvertToRadians(kFovAngle),
		aspectRatio,
		kNearClipping,
		kFarClipping
	);

	// WVP行列を計算
	DirectX::XMStoreFloat4x4(&wvpMatrix_, worldMatrix * viewMatrix * projectionMatrix);
	// ワールド行列をメンバ変数に保存
	DirectX::XMStoreFloat4x4(&worldMatrix_, worldMatrix);
}

void Renderer::BeginImGuiFrame()
{
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
}

void Renderer::SetBackgroundColor(const float* color)
{
	for (int i = 0; i < 3; ++i)
	{
		backgroundColor_[i] = color[i];
	}
	// 3番、アルファ値は 1.0f にする
	backgroundColor_[3] = 1.0f;
}

void Renderer::SetLightDirection(const DirectX::XMFLOAT3& direction)
{
	// シェーダー側での 0除算を防ぐ
	// ベクトルの大きさの2乗としきい値の2乗を比べる
	if (direction.x * direction.x + direction.y * direction.y + direction.z * direction.z < 0.001f * 0.001f) { return; }
	lightDirection_ = direction;
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
	// .exe と同じフォルダの
	// シェーダーファイルのパスを作成
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
		// UTF-16 でファイルパスを取得し、UTF-8 に変換して Logger に渡す
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

bool Renderer::InitImGui()
{
	HRESULT hr = S_OK;

	// SRV 用のディスクリプタヒープの作成 (シェーダーから見える)
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.NumDescriptors = kSrvHeapSize;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	hr = device_->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap_));
	if (CheckFailed(hr, "Failed to create SRV descriptor heap.")) { return false; }

	srvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// 空いている番号の一覧を作る
	// 最後から取り出すので、逆から入れる
	freeSrvIndices_.reserve(kSrvHeapSize);
	for (UINT i = kSrvHeapSize; i > 0; --i)
	{
		freeSrvIndices_.push_back(i - 1);
	}

	// ImGui 本体の作成
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();

	// Win32 のバックエンドの初期化
	if (!ImGui_ImplWin32_Init(window_.GetHwnd()))
	{
		Logger::Error("Failed to initialize ImGui Win32 backend.");
		return false;
	}

	// DX12 のバックエンドの初期化
	ImGui_ImplDX12_InitInfo initInfo = {};
	initInfo.Device = device_.Get();
	initInfo.CommandQueue = cmdQueue_.Get();
	initInfo.NumFramesInFlight = static_cast<int>(Game::kFrameCount);
	initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	initInfo.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	// 借りる・返す関数から Renderer に戻るため
	initInfo.UserData = this;
	initInfo.SrvDescriptorHeap = srvHeap_.Get();
	// キャプチャなしのラムダ式は通常の関数ポインタに変換できる
	initInfo.SrvDescriptorAllocFn = [](
		ImGui_ImplDX12_InitInfo* info,
		D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandle)
		{
			static_cast<Renderer*>(info->UserData)->AllocateSrvDescriptor(cpuHandle, gpuHandle);
		};
	initInfo.SrvDescriptorFreeFn = [](
		ImGui_ImplDX12_InitInfo* info,
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE)
		{
			static_cast<Renderer*>(info->UserData)->FreeSrvDescriptor(cpuHandle);
		};

	if (!ImGui_ImplDX12_Init(&initInfo))
	{
		Logger::Error("Failed to initialize ImGui DX12 backend.");
		return false;
	}

	return true;
}

bool Renderer::LoadTexture(const std::wstring& fileName)
{
	HRESULT hr = S_OK;

	// exe のフォルダを基準に画像ファイルのパスを作る
	wchar_t exePath[MAX_PATH] = {};
	DWORD length = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
	if (length == 0 || length == MAX_PATH)
	{
		Logger::Error("Failed to get module file name.");
		return false;
	}
	std::filesystem::path filePath = std::filesystem::path(exePath).parent_path() / fileName;

	// 画像を CPU のメモリに読み込む
	// FORCE_RGB: BGR の並びも R8G8B8A8 で読む。
	// IGNORE_SRGB: SRGBのフォーマットにしない
	DirectX::TexMetadata metadata = {};
	DirectX::ScratchImage scratchImage;
	hr = DirectX::LoadFromWICFile(
		filePath.c_str(),
		DirectX::WIC_FLAGS_FORCE_RGB | DirectX::WIC_FLAGS_IGNORE_SRGB,
		&metadata,
		scratchImage
	);
	if (CheckFailed(hr, "Failed to load texture file. " + StringUtils::Utf16ToUtf8(fileName))) { return false; }

	// デバッグ用
	Logger::Info(std::format(
		"Texture: {} x {}, format {}",
		metadata.width, metadata.height,
		static_cast<int>(metadata.format)
	));

	// ヒープの設定
	D3D12_HEAP_PROPERTIES heapProps = {};
	// GPU 専用のメモリ。CPU からは Map できない
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
	heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProps.CreationNodeMask = 1;
	heapProps.VisibleNodeMask = 1;

	// テクスチャの設定
	D3D12_RESOURCE_DESC textureDesc = {};
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	textureDesc.Alignment = 0;
	// 幅と高さ (ピクセル数)
	textureDesc.Width = static_cast<UINT64>(metadata.width);
	textureDesc.Height = static_cast<UINT>(metadata.height);
	textureDesc.DepthOrArraySize = 1;
	// ミップマップ無し
	textureDesc.MipLevels = 1;
	textureDesc.Format = metadata.format;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	// メモリ上の並びはドライバ側が決める
	textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	// テクスチャを作る。最初の状態はコピー先 (COPY_DEST)
	hr = device_->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&textureDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&texture_)
	);
	if (CheckFailed(hr, "Failed to create texture.")) { return false; }

	// コピーするときの形(フットプリント)を GPU に合わせて求める
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	UINT numRows = 0;
	UINT64 rowSizeInBytes = 0;
	UINT64 uploadBufferSize = 0;
	device_->GetCopyableFootprints(
		&textureDesc,
		0,					// 最初のサブリソース
		1,					// サブリソースの数
		0,					// バッファの先頭からのずれ
		&footprint,			// 置き方 (行の間隔 RowPitch など)
		&numRows,			// 行の数
		&rowSizeInBytes,	// 1行の中身のバイト数
		&uploadBufferSize	// アップロード用バッファに必要なバイト数
	);

	// アップロード用バッファ
	// CPU が書ける、GPU が読める
	D3D12_HEAP_PROPERTIES uploadHeapProps = heapProps;
	uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC uploadDesc = {};
	uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	uploadDesc.Alignment = 0;
	uploadDesc.Width = uploadBufferSize;
	uploadDesc.Height = 1;
	uploadDesc.DepthOrArraySize = 1;
	uploadDesc.MipLevels = 1;
	uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
	uploadDesc.SampleDesc.Count = 1;
	uploadDesc.SampleDesc.Quality = 0;
	uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	uploadDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	// GPU へのコピーが終わったら捨てる
	ComPtr<ID3D12Resource> uploadBuffer;
	hr = device_->CreateCommittedResource(
		&uploadHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&uploadDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&uploadBuffer)
	);
	if (CheckFailed(hr, "Failed to create texture upload buffer.")) { return false; }

	// 画像のピクセルを 1行ずつアップロード用バッファにコピーする
	// 画像: rowPitch, バッファ: 256 バイトの倍数の RowPitch で行の間隔が違うので
	// 画像のデータをまとめてコピーすることはできない
	const DirectX::Image* image = scratchImage.GetImage(0, 0, 0);
	D3D12_RANGE readRange = { 0, 0 };
	void* mapped = nullptr;
	hr = uploadBuffer->Map(0, &readRange, &mapped);
	if (CheckFailed(hr, "Failed to map texture upload buffer.")) { return false; }
	uint8_t* dstStart = static_cast<uint8_t*>(mapped) + footprint.Offset;
	for (UINT row = 0; row < numRows; ++row)
	{
		std::memcpy(
			dstStart + row * footprint.Footprint.RowPitch,	// コピー先の行の先頭
			image->pixels + row * image->rowPitch,			// コピー元の行の先頭
			static_cast<size_t>(rowSizeInBytes)				// 1行の中身だけ
		);
	}
	uploadBuffer->Unmap(0, nullptr);

	// コピーの命令を記録する
	// GPU はまだ何もしていないので、0番のアロケータを使うことができる
	hr = cmdAllocators_[0]->Reset();
	if (CheckFailed(hr, "Failed to reset command allocator.")) { return false; }
	hr = cmdList_->Reset(cmdAllocators_[0].Get(), nullptr);
	if (CheckFailed(hr, "Failed to reset command list.")) { return false; }

	// コピー先: テクスチャのサブリソース
	D3D12_TEXTURE_COPY_LOCATION dst = {};
	dst.pResource = texture_.Get();
	dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst.SubresourceIndex = 0;

	// コピー元: アップロード用バッファ (フットプリントで並びを伝える)
	D3D12_TEXTURE_COPY_LOCATION src = {};
	src.pResource = uploadBuffer.Get();
	src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	src.PlacedFootprint = footprint;

	// dstの (0, 0, 0) の位置(先頭)に src 全体をコピーする
	cmdList_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

	// コピー先 → ピクセルシェーダーが読む状態にする
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = texture_.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	cmdList_->ResourceBarrier(1, &barrier);

	hr = cmdList_->Close();
	if (CheckFailed(hr, "Failed to close command list.")) { return false; }

	ID3D12CommandList* lists[] = { cmdList_.Get() };
	cmdQueue_->ExecuteCommandLists(1, lists);
	if (!WaitForGpu()) { return false; }

	return true;
}

void Renderer::AllocateSrvDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandle)
{
	// 空いている番号がなければ止める
	IM_ASSERT(!freeSrvIndices_.empty());

	// 一覧の最後の番号を使う
	const UINT index = freeSrvIndices_.back();
	freeSrvIndices_.pop_back();

	// ヒープの先頭 + 番号 × 1つ分の大きさ
	*cpuHandle = srvHeap_->GetCPUDescriptorHandleForHeapStart();
	cpuHandle->ptr += static_cast<SIZE_T>(index) * srvDescriptorSize_;
	*gpuHandle = srvHeap_->GetGPUDescriptorHandleForHeapStart();
	gpuHandle->ptr += static_cast<UINT64>(index) * srvDescriptorSize_;
}

void Renderer::FreeSrvDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle)
{
	// (ハンドル - ヒープの先頭) ÷ 1つ分の大きさ = 番号
	const SIZE_T heapStart = srvHeap_->GetCPUDescriptorHandleForHeapStart().ptr;
	const UINT index = static_cast<UINT>((cpuHandle.ptr - heapStart) / srvDescriptorSize_);
	freeSrvIndices_.push_back(index);
}
