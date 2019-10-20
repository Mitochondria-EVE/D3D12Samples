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
#include <sl12/root_signature.h>
#include <sl12/pipeline_state.h>
#include <sl12/descriptor_set.h>
#include <sl12/gui.h>
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

	struct FFTTarget
	{
		static const int	kMaxBuffer = 8;

		sl12::Texture				tex_[kMaxBuffer];
		sl12::TextureView			srv_[kMaxBuffer];
		sl12::UnorderedAccessView	uav_[kMaxBuffer];

		void Destroy()
		{
			for (auto&& v : uav_) v.Destroy();
			for (auto&& v : srv_) v.Destroy();
			for (auto&& v : tex_) v.Destroy();
		}
	};	// struct FFTTarget

	struct TextureSet
	{
		sl12::Texture			tex_;
		sl12::TextureView		srv_;

		void Destroy()
		{
			srv_.Destroy();
			tex_.Destroy();
		}
	};	// struct TextureSet

	struct ConstantSet
	{
		sl12::Buffer				cb_;
		sl12::ConstantBufferView	cbv_;
		void*						ptr_;

		void Destroy()
		{
			cbv_.Destroy();
			cb_.Destroy();
		}
	};	// struct ConstantSet

	struct FilterCb
	{
		float	distance_scale[2];
		float	radius;
		float	inverse;
	};	// struct FilterCb

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

	TextureSet					g_srcTexture_;
	FFTTarget					g_srcTarget_;
	ConstantSet					g_FilterCb_[kMaxFrameCount];

	sl12::Fence					g_FFTFence_;

	sl12::Buffer				g_CBScenes_[kMaxFrameCount];
	void*						g_pCBSceneBuffers_[kMaxFrameCount] = { nullptr };
	sl12::ConstantBufferView	g_CBSceneViews_[kMaxFrameCount];

	sl12::Buffer			g_vbuffers_[3];
	sl12::VertexBufferView	g_vbufferViews_[3];

	sl12::Buffer			g_ibuffer_;
	sl12::IndexBufferView	g_ibufferView_;

	sl12::Sampler			g_sampler_;

	sl12::Shader			g_VShader_, g_PShader_;
	sl12::Shader			g_FFTViewShader_;
	sl12::Shader			g_FFTShaders_[FFTKind::Max];
	sl12::Shader			g_FFTFilterShader_;

	sl12::RootSignature		g_rootSigTex_;
	sl12::RootSignature		g_rootSigFFT_;
	sl12::RootSignature		g_rootSigCompute_;
	sl12::RootSignature		g_rootSigComputeFilter_;

	sl12::GraphicsPipelineState	g_psoTex_;
	sl12::GraphicsPipelineState	g_psoFFT_;
	sl12::ComputePipelineState	g_psoComputeFFTs_[4];
	sl12::ComputePipelineState	g_psoComputeFilter_;

	sl12::DescriptorSet		g_descSet_;

	sl12::Gui	g_Gui_;
	sl12::InputData	g_InputData_{};

	struct FFTViewType
	{
		enum Type
		{
			Source,
			FFT_Result,
			Filter_Result,
			IFFT_Result,

			Max
		};
	};
	FFTViewType::Type	g_fftViewType_ = FFTViewType::Source;
	int					g_SyncInterval = 1;

}

// FFTターゲットを初期化する
bool InitializeFFTTarget(FFTTarget* pTarget, sl12::u32 width, sl12::u32 height)
{
	sl12::TextureDesc texDesc;
	texDesc.dimension = sl12::TextureDimension::Texture2D;
	texDesc.width = width;
	texDesc.height = height;
	texDesc.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	texDesc.isUav = true;

	for (int i = 0; i < FFTTarget::kMaxBuffer; ++i)
	{
		if (!pTarget->tex_[i].Initialize(&g_Device_, texDesc))
		{
			return false;
		}

		if (!pTarget->srv_[i].Initialize(&g_Device_, &pTarget->tex_[i]))
		{
			return false;
		}

		if (!pTarget->uav_[i].Initialize(&g_Device_, &pTarget->tex_[i]))
		{
			return false;
		}
	}

	return true;
}

// テクスチャを読み込む
bool LoadTexture(TextureSet* pTexSet, const char* filename)
{
	File texFile(filename);

	if (!pTexSet->tex_.InitializeFromTGA(&g_Device_, &g_copyCmdList_, texFile.GetData(), texFile.GetSize(), 1, false))
	{
		return false;
	}
	if (!pTexSet->srv_.Initialize(&g_Device_, &pTexSet->tex_))
	{
		return false;
	}

	return true;
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

	if (!g_FFTFence_.Initialize(&g_Device_))
	{
		return false;
	}

	// 定数バッファを作成
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

	g_copyCmdList_.Reset();

	// テクスチャロード
	if (!LoadTexture(&g_srcTexture_, "data/woman.tga"))
	{
		return false;
	}

	// FFTターゲット初期化
	if (!InitializeFFTTarget(&g_srcTarget_, g_srcTexture_.tex_.GetTextureDesc().width, g_srcTexture_.tex_.GetTextureDesc().height))
	{
		return false;
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

	// フィルタ用定数バッファ生成
	for (int i = 0; i < _countof(g_FilterCb_); i++)
	{
		if (!g_FilterCb_[i].cb_.Initialize(&g_Device_, sizeof(FilterCb), 1, sl12::BufferUsage::ConstantBuffer, true, false))
		{
			return false;
		}

		if (!g_FilterCb_[i].cbv_.Initialize(&g_Device_, &g_FilterCb_[i].cb_))
		{
			return false;
		}

		g_FilterCb_[i].ptr_ = g_FilterCb_[i].cb_.Map(&g_mainCmdLists_[i]);
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
	if (!g_FFTFilterShader_.Initialize(&g_Device_, sl12::ShaderType::Compute, "data/CSFFTFilter.cso"))
	{
		return false;
	}

	// ルートシグネチャを作成
	{
		if (!g_rootSigTex_.Initialize(&g_Device_, &g_VShader_, &g_PShader_, nullptr, nullptr, nullptr))
		{
			return false;
		}
		if (!g_rootSigFFT_.Initialize(&g_Device_, &g_VShader_, &g_PShader_, nullptr, nullptr, nullptr))
		{
			return false;
		}
		if (!g_rootSigCompute_.Initialize(&g_Device_, &g_FFTShaders_[0]))
		{
			return false;
		}
		if (!g_rootSigComputeFilter_.Initialize(&g_Device_, &g_FFTFilterShader_))
		{
			return false;
		}
	}

	// PSOを作成
	{
		D3D12_INPUT_ELEMENT_DESC elementDescs[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 2, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		sl12::GraphicsPipelineStateDesc desc{};

		desc.rasterizer.fillMode = D3D12_FILL_MODE_SOLID;
		desc.rasterizer.cullMode = D3D12_CULL_MODE_NONE;
		desc.rasterizer.isDepthClipEnable = true;

		desc.blend.sampleMask = UINT_MAX;
		desc.blend.rtDesc[0].isBlendEnable = false;
		desc.blend.rtDesc[0].writeMask = D3D12_COLOR_WRITE_ENABLE_ALL;

		desc.depthStencil.isDepthEnable = true;
		desc.depthStencil.isDepthWriteEnable = true;
		desc.depthStencil.depthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

		desc.inputLayout.numElements = _countof(elementDescs);
		desc.inputLayout.pElements = elementDescs;

		desc.pRootSignature = &g_rootSigTex_;
		desc.pVS = &g_VShader_;
		desc.pPS = &g_PShader_;
		desc.primTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		desc.numRTVs = 0;
		desc.rtvFormats[desc.numRTVs++] = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.dsvFormat = g_DepthBuffer_.GetTextureDesc().format;
		desc.multisampleCount = 1;

		if (!g_psoTex_.Initialize(&g_Device_, desc))
		{
			return false;
		}

		// FFT確認用
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
		for (int i = 0; i < _countof(g_psoComputeFFTs_); i++)
		{
			desc.pCS = &g_FFTShaders_[i];
			if (!g_psoComputeFFTs_[i].Initialize(&g_Device_, desc))
			{
				return false;
			}
		}

		desc.pRootSignature = &g_rootSigComputeFilter_;
		desc.pCS = &g_FFTFilterShader_;
		if (!g_psoComputeFilter_.Initialize(&g_Device_, desc))
		{
			return false;
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

	g_psoComputeFilter_.Destroy();
	for (auto&& v : g_psoComputeFFTs_) v.Destroy();
	g_psoTex_.Destroy();
	g_psoFFT_.Destroy();

	g_rootSigComputeFilter_.Destroy();
	g_rootSigCompute_.Destroy();
	g_rootSigFFT_.Destroy();
	g_rootSigTex_.Destroy();

	for (auto&& v : g_FilterCb_) v.Destroy();
	g_srcTarget_.Destroy();
	g_srcTexture_.Destroy();

	for (auto& v : g_FFTShaders_)
	{
		v.Destroy();
	}
	g_VShader_.Destroy();
	g_PShader_.Destroy();

	g_sampler_.Destroy();

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

	g_DepthBufferView_.Destroy();
	g_DepthBuffer_.Destroy();
}

void MakeFFT(sl12::CommandList& cmdList, FFTTarget* pTarget, TextureSet* pTex)
{
	ID3D12GraphicsCommandList* pCmdList = cmdList.GetCommandList();

	// row pass
	pCmdList->SetPipelineState(g_psoComputeFFTs_[0].GetPSO());
	g_descSet_.Reset();
	g_descSet_.SetCsSrv(0, pTex->srv_.GetDescInfo().cpuHandle);
	g_descSet_.SetCsSrv(1, pTex->srv_.GetDescInfo().cpuHandle);
	g_descSet_.SetCsUav(0, pTarget->uav_[0].GetDescInfo().cpuHandle);
	g_descSet_.SetCsUav(1, pTarget->uav_[1].GetDescInfo().cpuHandle);
	cmdList.SetComputeRootSignatureAndDescriptorSet(&g_rootSigCompute_, &g_descSet_);
	pCmdList->Dispatch(1, pTex->tex_.GetTextureDesc().height, 1);

	// バリアを張る
	cmdList.UAVBarrier(&pTarget->tex_[0]);
	cmdList.UAVBarrier(&pTarget->tex_[1]);

	// collumn pass
	pCmdList->SetPipelineState(g_psoComputeFFTs_[1].GetPSO());
	g_descSet_.Reset();
	g_descSet_.SetCsSrv(0, pTarget->uav_[0].GetDescInfo().cpuHandle);
	g_descSet_.SetCsSrv(1, pTarget->uav_[1].GetDescInfo().cpuHandle);
	g_descSet_.SetCsUav(0, pTarget->uav_[2].GetDescInfo().cpuHandle);
	g_descSet_.SetCsUav(1, pTarget->uav_[3].GetDescInfo().cpuHandle);
	cmdList.SetComputeRootSignatureAndDescriptorSet(&g_rootSigCompute_, &g_descSet_);
	pCmdList->Dispatch(1, pTex->tex_.GetTextureDesc().width, 1);

	// バリアを張る
	cmdList.UAVBarrier(&pTarget->tex_[2]);
	cmdList.UAVBarrier(&pTarget->tex_[3]);
}

void MakeFilterFFT(sl12::CommandList& cmdList, FFTTarget* pTarget, ConstantSet* pConst)
{
	ID3D12GraphicsCommandList* pCmdList = cmdList.GetCommandList();

	auto desc = pTarget->tex_[0].GetTextureDesc();

	// row pass
	pCmdList->SetPipelineState(g_psoComputeFilter_.GetPSO());
	g_descSet_.Reset();
	g_descSet_.SetCsCbv(0, pConst->cbv_.GetDescInfo().cpuHandle);
	g_descSet_.SetCsSrv(0, pTarget->uav_[2].GetDescInfo().cpuHandle);
	g_descSet_.SetCsSrv(1, pTarget->uav_[3].GetDescInfo().cpuHandle);
	g_descSet_.SetCsUav(0, pTarget->uav_[6].GetDescInfo().cpuHandle);
	g_descSet_.SetCsUav(1, pTarget->uav_[7].GetDescInfo().cpuHandle);
	cmdList.SetComputeRootSignatureAndDescriptorSet(&g_rootSigCompute_, &g_descSet_);
	pCmdList->Dispatch(desc.width / 32, desc.height / 32, 1);

	// バリアを張る
	cmdList.UAVBarrier(&pTarget->tex_[6]);
	cmdList.UAVBarrier(&pTarget->tex_[7]);
}

void MakeIFFT(sl12::CommandList& cmdList, FFTTarget* pTarget, TextureSet* pTex)
{
	ID3D12GraphicsCommandList* pCmdList = cmdList.GetCommandList();

	// invert row pass
	pCmdList->SetPipelineState(g_psoComputeFFTs_[2].GetPSO());
	g_descSet_.Reset();
	g_descSet_.SetCsSrv(0, pTarget->uav_[6].GetDescInfo().cpuHandle);
	g_descSet_.SetCsSrv(1, pTarget->uav_[7].GetDescInfo().cpuHandle);
	g_descSet_.SetCsUav(0, pTarget->uav_[0].GetDescInfo().cpuHandle);
	g_descSet_.SetCsUav(1, pTarget->uav_[1].GetDescInfo().cpuHandle);
	cmdList.SetComputeRootSignatureAndDescriptorSet(&g_rootSigCompute_, &g_descSet_);
	pCmdList->Dispatch(1, pTex->tex_.GetTextureDesc().height, 1);

	// バリアを張る
	cmdList.UAVBarrier(&pTarget->tex_[0]);
	cmdList.UAVBarrier(&pTarget->tex_[1]);

	// invert collumn pass
	pCmdList->SetPipelineState(g_psoComputeFFTs_[3].GetPSO());
	g_descSet_.Reset();
	g_descSet_.SetCsSrv(0, pTarget->uav_[0].GetDescInfo().cpuHandle);
	g_descSet_.SetCsSrv(1, pTarget->uav_[1].GetDescInfo().cpuHandle);
	g_descSet_.SetCsUav(0, pTarget->uav_[4].GetDescInfo().cpuHandle);
	g_descSet_.SetCsUav(1, pTarget->uav_[5].GetDescInfo().cpuHandle);
	cmdList.SetComputeRootSignatureAndDescriptorSet(&g_rootSigCompute_, &g_descSet_);
	pCmdList->Dispatch(1, pTex->tex_.GetTextureDesc().width, 1);

	// バリアを張る
	cmdList.UAVBarrier(&pTarget->tex_[4]);
	cmdList.UAVBarrier(&pTarget->tex_[5]);
}

void RenderScene()
{
	static int sFFTCalcLoop = 1000;

	sl12::s32 frameIndex = g_Device_.GetSwapchain().GetFrameIndex();
	sl12::s32 nextFrameIndex = (frameIndex + 1) % sl12::Swapchain::kMaxBuffer;

	sl12::CommandList& mainCmdList = g_mainCmdLists_[frameIndex];

	g_Device_.SyncKillObjects();
	g_Gui_.BeginNewFrame(&mainCmdList, kWindowWidth, kWindowHeight, g_InputData_);

	static FilterCb filter_cb = { 1.0f, 1.0f, 0.1f, 0.0f };
	{
		const char* kViewNames[] = {
			"Source",
			"FFT Result",
			"Filter Result",
			"IFFT Result",
		};
		auto currentItem = (int)g_fftViewType_;
		if (ImGui::Combo("View", &currentItem, kViewNames, _countof(kViewNames)))
		{
			g_fftViewType_ = (FFTViewType::Type)currentItem;
		}

		ImGui::DragFloat2("Distance Scale", filter_cb.distance_scale, 0.1f, 0.0f, 128.0f);
		ImGui::DragFloat("Radius", &filter_cb.radius, 0.01f, 0.01f, 1.0f);
		bool isInverseFlag = filter_cb.inverse != 0.0f;
		if (ImGui::Checkbox("Inverse", &isInverseFlag))
		{
			filter_cb.inverse = isInverseFlag ? 1.0f : 0.0f;
		}
	}

	// フィルタ用定数バッファを更新する
	auto&& cb_set = g_FilterCb_[frameIndex];
	memcpy(cb_set.ptr_, &filter_cb, sizeof(FilterCb));

	// グラフィクスコマンドロードの開始
	mainCmdList.Reset();

	// ソーステクスチャとカーネルテクスチャのFFT計算
	MakeFFT(mainCmdList, &g_srcTarget_, &g_srcTexture_);

	// FFTの乗算
	MakeFilterFFT(mainCmdList, &g_srcTarget_, &cb_set);

	// ソースのIFFT計算
	MakeIFFT(mainCmdList, &g_srcTarget_, &g_srcTexture_);

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

		//sAngle += 1.0f;
	}

	{
		// レンダーターゲット設定
		pCmdList->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);

		// PSOとルートシグネチャを設定
		auto pRootSig = &g_rootSigTex_;
		if ((g_fftViewType_ != FFTViewType::FFT_Result) && (g_fftViewType_ != FFTViewType::Filter_Result))
		{
			pCmdList->SetPipelineState(g_psoTex_.GetPSO());
		}
		else
		{
			pCmdList->SetPipelineState(g_psoFFT_.GetPSO());
			pRootSig = &g_rootSigFFT_;
		}

		// DescriptorHeapを設定
		g_descSet_.Reset();
		g_descSet_.SetVsCbv(0, g_CBSceneViews_[frameIndex].GetDescInfo().cpuHandle);
		g_descSet_.SetPsSampler(0, g_sampler_.GetDescInfo().cpuHandle);
		switch (g_fftViewType_)
		{
		case FFTViewType::Source:
			g_descSet_.SetPsSrv(0, g_srcTexture_.srv_.GetDescInfo().cpuHandle);
			break;
		case FFTViewType::FFT_Result:
			g_descSet_.SetPsSrv(0, g_srcTarget_.srv_[2].GetDescInfo().cpuHandle);
			g_descSet_.SetPsSrv(1, g_srcTarget_.srv_[3].GetDescInfo().cpuHandle);
			break;
		case FFTViewType::Filter_Result:
			g_descSet_.SetPsSrv(0, g_srcTarget_.srv_[6].GetDescInfo().cpuHandle);
			g_descSet_.SetPsSrv(1, g_srcTarget_.srv_[7].GetDescInfo().cpuHandle);
			break;
		case FFTViewType::IFFT_Result:
			g_descSet_.SetPsSrv(0, g_srcTarget_.srv_[4].GetDescInfo().cpuHandle);
			break;
		}
		mainCmdList.SetGraphicsRootSignatureAndDescriptorSet(pRootSig, &g_descSet_);

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
