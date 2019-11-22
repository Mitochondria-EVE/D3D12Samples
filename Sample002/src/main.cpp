﻿#include <sl12/device.h>
#include <sl12/swapchain.h>
#include <sl12/command_queue.h>
#include <sl12/command_list.h>
#include <sl12/descriptor_heap.h>
#include <sl12/descriptor.h>
#include <sl12/texture.h>
#include <sl12/texture_view.h>
#include <sl12/sampler.h>
#include <sl12/fence.h>
#include <sl12/buffer.h>
#include <sl12/buffer_view.h>
#include <sl12/shader.h>
#include <sl12/gui.h>
#include <sl12/pipeline_state.h>
#include <sl12/root_signature.h>
#include <sl12/descriptor_set.h>
#include <DirectXTex.h>
#include <windowsx.h>

#include "file.h"


namespace
{
	struct FFTKind
	{
		enum Type
		{
			X,
			Y,
			InvX,
			InvY,

			Max
		};
	};

	static const wchar_t* kWindowTitle = L"D3D12Sample";
	static const int kWindowWidth = 1920;
	static const int kWindowHeight = 1080;
	//static const DXGI_FORMAT	kDepthBufferFormat = DXGI_FORMAT_R32G8X24_TYPELESS;
	//static const DXGI_FORMAT	kDepthViewFormat = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
	static const DXGI_FORMAT	kDepthBufferFormat = DXGI_FORMAT_R32_TYPELESS;
	static const DXGI_FORMAT	kDepthViewFormat = DXGI_FORMAT_D32_FLOAT;
	static const int kMaxFrameCount = sl12::Swapchain::kMaxBuffer;
	static const int kMaxComputeCmdList = 10;

	HWND	g_hWnd_;

	sl12::Device		g_Device_;
	sl12::CommandList	g_mainCmdLists_[kMaxFrameCount];
	sl12::CommandList	g_computeCmdLists_[kMaxComputeCmdList];
	sl12::CommandList	g_copyCmdList_;

	sl12::Texture			g_DepthBuffer_;
	sl12::DepthStencilView	g_DepthBufferView_;

	sl12::Texture				g_FFTTargets_[6];
	sl12::TextureView			g_FFTTargetSRVs_[_countof(g_FFTTargets_)];
	sl12::UnorderedAccessView	g_FFTTargetUAVs_[_countof(g_FFTTargets_)];
	sl12::Fence					g_FFTFence_;

	sl12::Buffer				g_CBScenes_[kMaxFrameCount];
	void*						g_pCBSceneBuffers_[kMaxFrameCount] = { nullptr };
	sl12::ConstantBufferView	g_CBSceneViews_[kMaxFrameCount];

	sl12::Buffer			g_vbuffers_[3];
	sl12::VertexBufferView	g_vbufferViews_[3];

	sl12::Buffer			g_ibuffer_;
	sl12::IndexBufferView	g_ibufferView_;

	sl12::Texture			g_texture_;
	sl12::TextureView		g_textureView_;
	sl12::Sampler			g_sampler_;

	sl12::Shader			g_VShader_, g_PShader_;
	sl12::Shader			g_FFTViewShader_;
	sl12::Shader			g_FFTShaders_[FFTKind::Max];

	sl12::RootSignature		g_rootSigTex_;
	sl12::RootSignature		g_rootSigFFT_;
	sl12::RootSignature		g_rootSigCompute_;

	sl12::GraphicsPipelineState	g_psoTex_;
	sl12::GraphicsPipelineState	g_psoFFT_;
	sl12::ComputePipelineState	g_psoComputes_[4];

	sl12::DescriptorSet		g_descSetTex_;
	sl12::DescriptorSet		g_descSetCompute_;

	sl12::Gui	g_Gui_;
	sl12::InputData	g_InputData_{};

	struct FFTPipeType
	{
		enum Type
		{
			Graphics,
			Sync_Compute,
			ASync_Compute,

			Max
		};
	};
	struct FFTViewType
	{
		enum Type
		{
			Source,
			FFT_Result,
			IFFT_Result,

			Max
		};
	};
	bool				g_isFFTCalcing = false;	// FFTの計算中フラグ
	bool				g_isFFTCalced = false;	// FFTの計算が終了しているか
	FFTPipeType::Type	g_fftPipeType_ = FFTPipeType::ASync_Compute;
	FFTViewType::Type	g_fftViewType_ = FFTViewType::Source;
	int					g_FrameCountToCalced = 0;
	int					g_SyncInterval = 1;
	bool				g_useResourceBarrier_ = true;

}

// Window Proc
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	// Handle destroy/shutdown messages.
	switch (message)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	case WM_LBUTTONDOWN:
		g_InputData_.mouseButton |= sl12::MouseButton::Left;
		return 0;
	case WM_RBUTTONDOWN:
		g_InputData_.mouseButton |= sl12::MouseButton::Right;
		return 0;
	case WM_MBUTTONDOWN:
		g_InputData_.mouseButton |= sl12::MouseButton::Middle;
		return 0;
	case WM_LBUTTONUP:
		g_InputData_.mouseButton &= ~sl12::MouseButton::Left;
		return 0;
	case WM_RBUTTONUP:
		g_InputData_.mouseButton &= ~sl12::MouseButton::Right;
		return 0;
	case WM_MBUTTONUP:
		g_InputData_.mouseButton &= ~sl12::MouseButton::Middle;
		return 0;
	case WM_MOUSEMOVE:
		g_InputData_.mouseX = GET_X_LPARAM(lParam);
		g_InputData_.mouseY = GET_Y_LPARAM(lParam);
		return 0;
	}

	// Handle any messages the switch statement didn't.
	return DefWindowProc(hWnd, message, wParam, lParam);
}

// Windowの初期化
void InitWindow(HINSTANCE hInstance, int nCmdShow)
{
	// Initialize the window class.
	WNDCLASSEX windowClass = { 0 };
	windowClass.cbSize = sizeof(WNDCLASSEX);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = WindowProc;
	windowClass.hInstance = hInstance;
	windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	windowClass.lpszClassName = L"WindowClass1";
	RegisterClassEx(&windowClass);

	RECT windowRect = { 0, 0, kWindowWidth, kWindowHeight };
	AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

	// Create the window and store a handle to it.
	g_hWnd_ = CreateWindowEx(NULL,
		L"WindowClass1",
		kWindowTitle,
		WS_OVERLAPPEDWINDOW,
		300,
		300,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		NULL,		// We have no parent window, NULL.
		NULL,		// We aren't using menus, NULL.
		hInstance,
		NULL);		// We aren't using multiple windows, NULL.

	ShowWindow(g_hWnd_, nCmdShow);
}

bool InitializeAssets()
{
	ID3D12Device* pDev = g_Device_.GetDeviceDep();

	g_copyCmdList_.Reset();

	// 深度バッファを作成
	{
		sl12::TextureDesc texDesc;
		texDesc.dimension = sl12::TextureDimension::Texture2D;
		texDesc.width = kWindowWidth;
		texDesc.height = kWindowHeight;
		texDesc.format = kDepthViewFormat;
		texDesc.isDepthBuffer = true;

		if (!g_DepthBuffer_.Initialize(&g_Device_, texDesc))
		{
			return false;
		}

		if (!g_DepthBufferView_.Initialize(&g_Device_, &g_DepthBuffer_))
		{
			return false;
		}
	}

	// FFTターゲットを作成する
	for (int i = 0; i < _countof(g_FFTTargets_); i++)
	{
		sl12::TextureDesc texDesc;
		texDesc.dimension = sl12::TextureDimension::Texture2D;
		texDesc.width = texDesc.height = 256;
		texDesc.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		texDesc.isUav = true;

		if (!g_FFTTargets_[i].Initialize(&g_Device_, texDesc))
		{
			return false;
		}

		if (!g_FFTTargetSRVs_[i].Initialize(&g_Device_, &g_FFTTargets_[i]))
		{
			return false;
		}

		if (!g_FFTTargetUAVs_[i].Initialize(&g_Device_, &g_FFTTargets_[i]))
		{
			return false;
		}
	}
	if (!g_FFTFence_.Initialize(&g_Device_))
	{
		return false;
	}

	// 定数バッファを作成
	{
		D3D12_HEAP_PROPERTIES prop{};
		prop.Type = D3D12_HEAP_TYPE_UPLOAD;
		prop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		prop.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		prop.CreationNodeMask = 1;
		prop.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC desc{};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Alignment = 0;
		desc.Width = 256;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		desc.Flags = D3D12_RESOURCE_FLAG_NONE;

		for (int i = 0; i < _countof(g_CBScenes_); i++)
		{
			if (!g_CBScenes_[i].Initialize(&g_Device_, sizeof(DirectX::XMFLOAT4X4) * 3, 1, sl12::BufferUsage::ConstantBuffer, true, false))
			{
				return false;
			}

			if (!g_CBSceneViews_[i].Initialize(&g_Device_, &g_CBScenes_[i]))
			{
				return false;
			}

			g_pCBSceneBuffers_[i] = g_CBScenes_[i].Map(&g_mainCmdLists_[i]);
		}
	}

	// 頂点バッファを作成
	{
		{
			float positions[] = {
				-1.0f,  1.0f, 0.0f,
				1.0f,  1.0f, 0.0f,
				-1.0f, -1.0f, 0.0f,
				1.0f, -1.0f, 0.0f,
			};

			if (!g_vbuffers_[0].Initialize(&g_Device_, sizeof(positions), sizeof(float) * 3, sl12::BufferUsage::VertexBuffer, false, false))
			{
				return false;
			}
			if (!g_vbufferViews_[0].Initialize(&g_Device_, &g_vbuffers_[0]))
			{
				return false;
			}

			g_vbuffers_[0].UpdateBuffer(&g_Device_, &g_copyCmdList_, positions, sizeof(positions));
		}
		{
			uint32_t colors[] = {
				0xff0000ff, 0xff00ff00, 0xffff0000, 0xffffffff
			};

			if (!g_vbuffers_[1].Initialize(&g_Device_, sizeof(colors), sizeof(sl12::u32), sl12::BufferUsage::VertexBuffer, false, false))
			{
				return false;
			}
			if (!g_vbufferViews_[1].Initialize(&g_Device_, &g_vbuffers_[1]))
			{
				return false;
			}

			g_vbuffers_[1].UpdateBuffer(&g_Device_, &g_copyCmdList_, colors, sizeof(colors));
		}
		{
			float uvs[] = {
				0.0f, 0.0f,
				1.0f, 0.0f,
				0.0f, 1.0f,
				1.0f, 1.0f
			};

			if (!g_vbuffers_[2].Initialize(&g_Device_, sizeof(uvs), sizeof(float) * 2, sl12::BufferUsage::VertexBuffer, false, false))
			{
				return false;
			}
			if (!g_vbufferViews_[2].Initialize(&g_Device_, &g_vbuffers_[2]))
			{
				return false;
			}

			g_vbuffers_[2].UpdateBuffer(&g_Device_, &g_copyCmdList_, uvs, sizeof(uvs));
		}
	}
	// インデックスバッファを作成
	{
		uint32_t indices[] = {
			0, 1, 2, 1, 3, 2
		};

		if (!g_ibuffer_.Initialize(&g_Device_, sizeof(indices), sizeof(sl12::u32), sl12::BufferUsage::IndexBuffer, false, false))
		{
			return false;
		}
		if (!g_ibufferView_.Initialize(&g_Device_, &g_ibuffer_))
		{
			return false;
		}

		g_ibuffer_.UpdateBuffer(&g_Device_, &g_copyCmdList_, indices, sizeof(indices));
	}

	// テクスチャロード
	{
		File texFile("data/icon.tga");

		if (!g_texture_.InitializeFromTGA(&g_Device_, &g_copyCmdList_, texFile.GetData(), texFile.GetSize(), 1, false))
		{
			return false;
		}
		if (!g_textureView_.Initialize(&g_Device_, &g_texture_))
		{
			return false;
		}
	}
	// サンプラ作成
	{
		D3D12_SAMPLER_DESC desc{};
		desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		if (!g_sampler_.Initialize(&g_Device_, desc))
		{
			return false;
		}
	}

	// シェーダロード
	if (!g_VShader_.Initialize(&g_Device_, sl12::ShaderType::Vertex, "data/VSSample.cso"))
	{
		return false;
	}
	if (!g_PShader_.Initialize(&g_Device_, sl12::ShaderType::Pixel, "data/PSSample.cso"))
	{
		return false;
	}
	if (!g_FFTViewShader_.Initialize(&g_Device_, sl12::ShaderType::Pixel, "data/PSFFTView.cso"))
	{
		return false;
	}
	int i = 0;
	static const char* kFFTShaderNames[] = {
		"data/CSFFTx.cso",
		"data/CSFFTy.cso",
		"data/CSIFFTx.cso",
		"data/CSIFFTy.cso",
	};
	for (auto& v : g_FFTShaders_)
	{
		if (!v.Initialize(&g_Device_, sl12::ShaderType::Compute, kFFTShaderNames[i++]))
		{
			return false;
		}
	}

	// ルートシグネチャを作成
	{
		if (!g_rootSigTex_.Initialize(&g_Device_, &g_VShader_, &g_PShader_, nullptr, nullptr, nullptr))
		{
			return false;
		}
		if (!g_rootSigFFT_.Initialize(&g_Device_, &g_VShader_, &g_FFTViewShader_, nullptr, nullptr, nullptr))
		{
			return false;
		}
		if (!g_rootSigCompute_.Initialize(&g_Device_, &g_FFTShaders_[0]))
		{
			return false;
		}
	}

	// PSOを作成
	{
		sl12::GraphicsPipelineStateDesc desc;
		desc.pRootSignature = &g_rootSigTex_;
		desc.pVS = &g_VShader_;
		desc.pPS = &g_PShader_;

		desc.blend.sampleMask = UINT_MAX;
		desc.blend.rtDesc[0].isBlendEnable = false;
		desc.blend.rtDesc[0].blendOpColor = D3D12_BLEND_OP_ADD;
		desc.blend.rtDesc[0].srcBlendColor = D3D12_BLEND_SRC_ALPHA;
		desc.blend.rtDesc[0].dstBlendColor = D3D12_BLEND_INV_SRC_ALPHA;
		desc.blend.rtDesc[0].blendOpAlpha = D3D12_BLEND_OP_ADD;
		desc.blend.rtDesc[0].srcBlendAlpha = D3D12_BLEND_ONE;
		desc.blend.rtDesc[0].dstBlendAlpha = D3D12_BLEND_ZERO;
		desc.blend.rtDesc[0].writeMask = D3D12_COLOR_WRITE_ENABLE_ALL;

		desc.rasterizer.cullMode = D3D12_CULL_MODE_NONE;
		desc.rasterizer.fillMode = D3D12_FILL_MODE_SOLID;
		desc.rasterizer.isDepthClipEnable = false;
		desc.rasterizer.isFrontCCW = false;

		desc.depthStencil.isDepthEnable = true;
		desc.depthStencil.isDepthWriteEnable = true;
		desc.depthStencil.depthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

		D3D12_INPUT_ELEMENT_DESC input_elems[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,  1, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    2, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};
		desc.inputLayout.numElements = ARRAYSIZE(input_elems);
		desc.inputLayout.pElements = input_elems;

		desc.primTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		desc.numRTVs = 0;
		desc.rtvFormats[desc.numRTVs++] = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.dsvFormat = g_DepthBuffer_.GetTextureDesc().format;
		desc.multisampleCount = 1;

		if (!g_psoTex_.Initialize(&g_Device_, desc))
		{
			return false;
		}

		desc.pRootSignature = &g_rootSigFFT_;
		desc.pPS = &g_FFTViewShader_;

		if (!g_psoFFT_.Initialize(&g_Device_, desc))
		{
			return false;
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &g_rootSigCompute_;
		for (int i = 0; i < _countof(g_psoComputes_); i++)
		{
			desc.pCS = &g_FFTShaders_[i];
			if (!g_psoComputes_[i].Initialize(&g_Device_, desc))
			{
				return false;
			}
		}
	}

	// GUIの初期化
	if (!g_Gui_.Initialize(&g_Device_, DXGI_FORMAT_R8G8B8A8_UNORM, g_DepthBuffer_.GetTextureDesc().format))
	{
		return false;
	}
	if (!g_Gui_.CreateFontImage(&g_Device_, g_copyCmdList_))
	{
		return false;
	}

	// コピーコマンドの実行と終了待ち
	g_copyCmdList_.Close();
	g_copyCmdList_.Execute();

	sl12::Fence fence;
	fence.Initialize(&g_Device_);
	fence.Signal(g_copyCmdList_.GetParentQueue());
	fence.WaitSignal();

	fence.Destroy();

	return true;
}

void DestroyAssets()
{
	g_Gui_.Destroy();

	for (auto&& v : g_psoComputes_)
	{
		v.Destroy();
	}
	g_psoTex_.Destroy();
	g_psoFFT_.Destroy();

	g_rootSigTex_.Destroy();
	g_rootSigFFT_.Destroy();
	g_rootSigCompute_.Destroy();

	for (auto& v : g_FFTShaders_)
	{
		v.Destroy();
	}
	g_VShader_.Destroy();
	g_PShader_.Destroy();

	g_sampler_.Destroy();
	g_textureView_.Destroy();
	g_texture_.Destroy();

	for (auto& v : g_vbufferViews_)
	{
		v.Destroy();
	}
	for (auto& v : g_vbuffers_)
	{
		v.Destroy();
	}

	g_ibufferView_.Destroy();
	g_ibuffer_.Destroy();

	for (auto& v : g_CBSceneViews_)
	{
		v.Destroy();
	}
	for (auto& v : g_CBScenes_)
	{
		v.Unmap();
		v.Destroy();
	}

	g_FFTFence_.Destroy();
	for (auto& v : g_FFTTargetUAVs_)
	{
		v.Destroy();
	}
	for (auto& v : g_FFTTargetSRVs_)
	{
		v.Destroy();
	}
	for (auto& v : g_FFTTargets_)
	{
		v.Destroy();
	}

	g_DepthBufferView_.Destroy();
	g_DepthBuffer_.Destroy();
}

void LoadFFTCommand(sl12::CommandList& cmdList, int numLoop)
{
	ID3D12GraphicsCommandList* pCmdList = cmdList.GetCommandList();

	for (int i = 0; i < numLoop; i++)
	{
		// row pass
		g_descSetCompute_.Reset();
		g_descSetCompute_.SetCsSrv(0, g_textureView_.GetDescInfo().cpuHandle);
		g_descSetCompute_.SetCsSrv(1, g_textureView_.GetDescInfo().cpuHandle);
		g_descSetCompute_.SetCsUav(0, g_FFTTargetUAVs_[0].GetDescInfo().cpuHandle);
		g_descSetCompute_.SetCsUav(1, g_FFTTargetUAVs_[1].GetDescInfo().cpuHandle);
		pCmdList->SetPipelineState(g_psoComputes_[0].GetPSO());
		cmdList.SetComputeRootSignatureAndDescriptorSet(&g_rootSigCompute_, &g_descSetCompute_);
		pCmdList->Dispatch(1, g_texture_.GetTextureDesc().height, 1);

		if (g_useResourceBarrier_)
		{
			// バリアを張る
			cmdList.UAVBarrier(&g_FFTTargets_[0]);
			cmdList.UAVBarrier(&g_FFTTargets_[1]);
		}

		// collumn pass
		g_descSetCompute_.Reset();
		g_descSetCompute_.SetCsSrv(0, g_FFTTargetUAVs_[0].GetDescInfo().cpuHandle);
		g_descSetCompute_.SetCsSrv(1, g_FFTTargetUAVs_[1].GetDescInfo().cpuHandle);
		g_descSetCompute_.SetCsUav(0, g_FFTTargetUAVs_[2].GetDescInfo().cpuHandle);
		g_descSetCompute_.SetCsUav(1, g_FFTTargetUAVs_[3].GetDescInfo().cpuHandle);
		pCmdList->SetPipelineState(g_psoComputes_[1].GetPSO());
		cmdList.SetComputeRootSignatureAndDescriptorSet(&g_rootSigCompute_, &g_descSetCompute_);
		pCmdList->Dispatch(1, g_texture_.GetTextureDesc().width, 1);

		if (g_useResourceBarrier_)
		{
			// バリアを張る
			cmdList.UAVBarrier(&g_FFTTargets_[2]);
			cmdList.UAVBarrier(&g_FFTTargets_[3]);
		}

		// invert row pass
		g_descSetCompute_.Reset();
		g_descSetCompute_.SetCsSrv(0, g_FFTTargetUAVs_[2].GetDescInfo().cpuHandle);
		g_descSetCompute_.SetCsSrv(1, g_FFTTargetUAVs_[3].GetDescInfo().cpuHandle);
		g_descSetCompute_.SetCsUav(0, g_FFTTargetUAVs_[0].GetDescInfo().cpuHandle);
		g_descSetCompute_.SetCsUav(1, g_FFTTargetUAVs_[1].GetDescInfo().cpuHandle);
		pCmdList->SetPipelineState(g_psoComputes_[2].GetPSO());
		cmdList.SetComputeRootSignatureAndDescriptorSet(&g_rootSigCompute_, &g_descSetCompute_);
		pCmdList->Dispatch(1, g_texture_.GetTextureDesc().height, 1);

		if (g_useResourceBarrier_)
		{
			// バリアを張る
			cmdList.UAVBarrier(&g_FFTTargets_[0]);
			cmdList.UAVBarrier(&g_FFTTargets_[1]);
		}

		// invert collumn pass
		g_descSetCompute_.Reset();
		g_descSetCompute_.SetCsSrv(0, g_FFTTargetUAVs_[0].GetDescInfo().cpuHandle);
		g_descSetCompute_.SetCsSrv(1, g_FFTTargetUAVs_[1].GetDescInfo().cpuHandle);
		g_descSetCompute_.SetCsUav(0, g_FFTTargetUAVs_[4].GetDescInfo().cpuHandle);
		g_descSetCompute_.SetCsUav(1, g_FFTTargetUAVs_[5].GetDescInfo().cpuHandle);
		pCmdList->SetPipelineState(g_psoComputes_[3].GetPSO());
		cmdList.SetComputeRootSignatureAndDescriptorSet(&g_rootSigCompute_, &g_descSetCompute_);
		pCmdList->Dispatch(1, g_texture_.GetTextureDesc().width, 1);

		if (g_useResourceBarrier_)
		{
			// バリアを張る
			cmdList.UAVBarrier(&g_FFTTargets_[4]);
			cmdList.UAVBarrier(&g_FFTTargets_[5]);
		}
	}
}

void RenderScene()
{
	static int sFFTCalcLoop = 1000;

	sl12::s32 frameIndex = g_Device_.GetSwapchain().GetFrameIndex();
	sl12::s32 nextFrameIndex = (frameIndex + 1) % sl12::Swapchain::kMaxBuffer;

	sl12::CommandList& mainCmdList = g_mainCmdLists_[frameIndex];

	g_Device_.SyncKillObjects();
	g_Gui_.BeginNewFrame(&mainCmdList, kWindowWidth, kWindowHeight, g_InputData_);

	bool runCalcFFT = false;
	if (!g_isFFTCalcing)
	{
		ImGui::DragInt("Loop", &sFFTCalcLoop, 1.0f, 1, 1000);
		const char* kPipeNames[] = {
			"Graphics",
			"Sync Compute",
			"ASync Compute",
		};
		int currentItem = (int)g_fftPipeType_;
		if (ImGui::Combo("Pipe", &currentItem, kPipeNames, _countof(kPipeNames)))
		{
			g_fftPipeType_ = (FFTPipeType::Type)currentItem;
		}

		runCalcFFT = ImGui::Button("Calc FFT");

		const char* kViewNames[] = {
			"Source",
			"FFT Result",
			"IFFT Result",
		};
		currentItem = (int)g_fftViewType_;
		if (ImGui::Combo("View", &currentItem, kViewNames, _countof(kViewNames)))
		{
			g_fftViewType_ = (FFTViewType::Type)currentItem;
		}

		bool isSyncInterval = g_SyncInterval == 1;
		if (ImGui::Checkbox("Sync Interval", &isSyncInterval))
		{
			g_SyncInterval = isSyncInterval ? 1 : 0;
		}

		ImGui::Checkbox("Use Barrier", &g_useResourceBarrier_);

		ImGui::Text("Frame Count To Calc : %d", g_FrameCountToCalced);
	}
	else
	{
		// FFTの計算終了チェック
		// 非同期コンピュートの場合のみチェックを行う
		if (g_FFTFence_.CheckSignal())
		{
			g_isFFTCalced = true;
			g_isFFTCalcing = false;
		}
		else
		{
			g_FrameCountToCalced++;
		}
	}

	// コンピュートキューでのFFT計算
	if (runCalcFFT)
	{
		if (g_fftPipeType_ != FFTPipeType::Graphics)
		{
			// FFTを実行する
			int loopPerCmdList = sFFTCalcLoop / _countof(g_computeCmdLists_);
			int loopCount = 0;
			int loop = 0;
			for (; loop < _countof(g_computeCmdLists_) - 1; loop++)
			{
				g_computeCmdLists_[loop].Reset();
				LoadFFTCommand(g_computeCmdLists_[loop], loopPerCmdList);
				g_computeCmdLists_[loop].Close();
				g_computeCmdLists_[loop].Execute();
				loopCount += loopPerCmdList;
			}
			{
				// ラストは残り全部
				g_computeCmdLists_[loop].Reset();
				LoadFFTCommand(g_computeCmdLists_[loop], sFFTCalcLoop - loopCount);
				g_computeCmdLists_[loop].Close();
				g_computeCmdLists_[loop].Execute();
			}

			// シグナル
			g_FFTFence_.Signal(g_computeCmdLists_[0].GetParentQueue());

			if (g_fftPipeType_ == FFTPipeType::ASync_Compute)
			{
				// 非同期コンピュートの場合はフェンスを立て、シグナルを待ってから描画可能になる
				// 各種パラメータ
				g_isFFTCalcing = true;
				g_isFFTCalced = false;
				g_FrameCountToCalced = 0;
			}
			else
			{
				// コンピュートパイプで動作させて同期する場合はグラフィクスパイプでフェンスのシグナルを待つ
				g_FFTFence_.WaitSignal(mainCmdList.GetParentQueue());

				// フェンスを待つので、次のフレームにはFFT計算は完了している
				g_isFFTCalced = true;
			}
		}
	}

	// グラフィクスコマンドロードの開始
	mainCmdList.Reset();

	// FFT計算
	if (runCalcFFT && (g_fftPipeType_ == FFTPipeType::Graphics))
	{
		// グラフィクスパイプで実行する場合はそのままグラフィクスキューのコマンドリストに積む
		// シグナルや描画待ちはバリアだけでOK
		LoadFFTCommand(mainCmdList, sFFTCalcLoop);
		g_isFFTCalced = true;
	}

	mainCmdList.TransitionBarrier(&g_texture_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	for (auto& v : g_vbuffers_)
	{
		mainCmdList.TransitionBarrier(&v, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	}
	mainCmdList.TransitionBarrier(&g_ibuffer_, D3D12_RESOURCE_STATE_INDEX_BUFFER);

	auto scTex = g_Device_.GetSwapchain().GetCurrentTexture(1);
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_Device_.GetSwapchain().GetDescHandle(nextFrameIndex);
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = g_DepthBufferView_.GetDescInfo().cpuHandle;
	ID3D12GraphicsCommandList* pCmdList = mainCmdList.GetCommandList();

	mainCmdList.TransitionBarrier(scTex, D3D12_RESOURCE_STATE_RENDER_TARGET);

	// 画面クリア
	const float kClearColor[] = { 0.0f, 0.0f, 0.6f, 1.0f };
	pCmdList->ClearRenderTargetView(rtvHandle, kClearColor, 0, nullptr);
	pCmdList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, g_DepthBuffer_.GetTextureDesc().clearDepth, g_DepthBuffer_.GetTextureDesc().clearStencil, 0, nullptr);

	// Viewport + Scissor設定
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, (float)kWindowWidth, (float)kWindowHeight, 0.0f, 1.0f };
	D3D12_RECT scissor{ 0, 0, kWindowWidth, kWindowHeight };
	pCmdList->RSSetViewports(1, &viewport);
	pCmdList->RSSetScissorRects(1, &scissor);

	// Scene定数バッファを更新
	auto&& cbSceneDescInfo = g_CBSceneViews_[frameIndex].GetDescInfo();
	{
		static float sAngle = 0.0f;
		void* p0 = g_pCBSceneBuffers_[frameIndex];
		DirectX::XMFLOAT4X4* pMtxs = reinterpret_cast<DirectX::XMFLOAT4X4*>(p0);
		DirectX::XMMATRIX mtxW = DirectX::XMMatrixRotationY(sAngle * DirectX::XM_PI / 180.0f) * DirectX::XMMatrixScaling(4.0f, 4.0f, 1.0f);
		DirectX::FXMVECTOR eye = DirectX::XMLoadFloat3(&DirectX::XMFLOAT3(0.0f, 0.0f, 10.0f));
		DirectX::FXMVECTOR focus = DirectX::XMLoadFloat3(&DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
		DirectX::FXMVECTOR up = DirectX::XMLoadFloat3(&DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f));
		DirectX::XMMATRIX mtxV = DirectX::XMMatrixLookAtRH(eye, focus, up);
		DirectX::XMMATRIX mtxP = DirectX::XMMatrixPerspectiveFovRH(60.0f * DirectX::XM_PI / 180.0f, (float)kWindowWidth / (float)kWindowHeight, 0.1f, 100.0f);
		DirectX::XMStoreFloat4x4(pMtxs + 0, mtxW);
		DirectX::XMStoreFloat4x4(pMtxs + 1, mtxV);
		DirectX::XMStoreFloat4x4(pMtxs + 2, mtxP);

		sAngle += 1.0f;
	}

	{
		// レンダーターゲット設定
		pCmdList->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);

		g_descSetTex_.Reset();
		g_descSetTex_.SetVsCbv(0, cbSceneDescInfo.cpuHandle);
		if (g_fftViewType_ == FFTViewType::Source)
		{
			// ソーステクスチャの描画
			g_descSetTex_.SetPsSrv(0, g_textureView_.GetDescInfo().cpuHandle);
			g_descSetTex_.SetPsSampler(0, g_sampler_.GetDescInfo().cpuHandle);
		}
		else if (g_fftViewType_ == FFTViewType::IFFT_Result)
		{
			// IFFTの結果の描画
			g_descSetTex_.SetPsSrv(0, g_FFTTargetSRVs_[4].GetDescInfo().cpuHandle);
			g_descSetTex_.SetPsSampler(0, g_sampler_.GetDescInfo().cpuHandle);
		}
		else
		{
			// FFTの結果の描画
			g_descSetTex_.SetPsSrv(0, g_FFTTargetSRVs_[2].GetDescInfo().cpuHandle);
			g_descSetTex_.SetPsSrv(1, g_FFTTargetSRVs_[3].GetDescInfo().cpuHandle);
			g_descSetTex_.SetPsSampler(0, g_sampler_.GetDescInfo().cpuHandle);
		}

		// PSOとルートシグネチャを設定
		if (g_fftViewType_ != FFTViewType::FFT_Result)
		{
			pCmdList->SetPipelineState(g_psoTex_.GetPSO());
			mainCmdList.SetGraphicsRootSignatureAndDescriptorSet(&g_rootSigTex_, &g_descSetTex_);
		}
		else
		{
			pCmdList->SetPipelineState(g_psoFFT_.GetPSO());
			mainCmdList.SetGraphicsRootSignatureAndDescriptorSet(&g_rootSigFFT_, &g_descSetTex_);
		}

		// DrawCall
		D3D12_VERTEX_BUFFER_VIEW views[] = { g_vbufferViews_[0].GetView(), g_vbufferViews_[1].GetView(), g_vbufferViews_[2].GetView() };
		pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCmdList->IASetVertexBuffers(0, _countof(views), views);
		pCmdList->IASetIndexBuffer(&g_ibufferView_.GetView());
		pCmdList->DrawIndexedInstanced(6, 1, 0, 0, 0);
	}

	ImGui::Render();

	mainCmdList.TransitionBarrier(scTex, D3D12_RESOURCE_STATE_PRESENT);

	mainCmdList.Close();
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
	InitWindow(hInstance, nCmdShow);

	std::array<uint32_t, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> kDescNums
	{ 100, 100, 20, 10 };
	auto ret = g_Device_.Initialize(g_hWnd_, kWindowWidth, kWindowHeight, kDescNums);
	assert(ret);
	for (auto& v : g_mainCmdLists_)
	{
		ret = v.Initialize(&g_Device_, &g_Device_.GetGraphicsQueue());
		assert(ret);
	}
	for (auto& v : g_computeCmdLists_)
	{
		ret = v.Initialize(&g_Device_, &g_Device_.GetComputeQueue());
		assert(ret);
	}
	assert(ret);
	ret = g_copyCmdList_.Initialize(&g_Device_, &g_Device_.GetCopyQueue());
	assert(ret);
	ret = InitializeAssets();
	assert(ret);

	// メインループ
	MSG msg = { 0 };
	while (true)
	{
		// Process any messages in the queue.
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);

			if (msg.message == WM_QUIT)
				break;
		}

		g_Device_.WaitPresent();

		RenderScene();

		// GPUによる描画待ち
		int frameIndex = g_Device_.GetSwapchain().GetFrameIndex();
		g_Device_.WaitDrawDone();
		g_Device_.Present(g_SyncInterval);

		// 前回フレームのコマンドを次回フレームの頭で実行
		// コマンドが実行中、次のフレーム用のコマンドがロードされる
		g_mainCmdLists_[frameIndex].Execute();
	}

	g_Device_.WaitDrawDone();
	DestroyAssets();
	g_copyCmdList_.Destroy();
	for (auto& v : g_mainCmdLists_)
		v.Destroy();
	for (auto& v : g_mainCmdLists_)
		v.Destroy();
	g_Device_.Destroy();

	return static_cast<char>(msg.wParam);
}
