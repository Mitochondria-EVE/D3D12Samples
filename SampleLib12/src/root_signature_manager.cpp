﻿#include <sl12/root_signature_manager.h>

#include <d3dcompiler.h>
#include <sl12/crc.h>


namespace sl12
{
	//-------------------------------------------------
	// ハンドルを無効化する
	//-------------------------------------------------
	void RootSignatureHandle::Invalid()
	{
		if (pInstance_)
		{
			pManager_->ReleaseRootSignature(crc_, pInstance_);
			pManager_ = nullptr;
			pInstance_ = nullptr;
		}
	}


	//-------------------------------------------------
	// 初期化
	//-------------------------------------------------
	bool RootSignatureManager::Initialize(Device* pDev)
	{
		pDevice_ = pDev;
		return (pDev != nullptr);
	}

	//-------------------------------------------------
	// 破棄
	//-------------------------------------------------
	void RootSignatureManager::Destroy()
	{
		if (pDevice_)
		{
			for (auto&& v : instanceMap_) delete v.second;
			instanceMap_.clear();
			pDevice_ = nullptr;
		}
	}

	//-------------------------------------------------
	// ルートシグネチャを生成する
	//-------------------------------------------------
	RootSignatureHandle RootSignatureManager::CreateRootSignature(const RootSignatureCreateDesc& desc)
	{
		// シェーダからCRC32を計算する
		u32 crc;
		if (desc.pCS)
		{
			// コンピュートシェーダ
			crc = CalcCrc32(desc.pCS->GetData(), desc.pCS->GetSize());
		}
		else
		{
			// ラスタライザ
			u8 zero = 0;
			crc = desc.pVS != nullptr ? CalcCrc32(desc.pVS->GetData(), desc.pVS->GetSize()) : CalcCrc32(&zero, sizeof(zero));
			crc = desc.pPS != nullptr ? CalcCrc32(desc.pPS->GetData(), desc.pPS->GetSize(), crc) : CalcCrc32(&zero, sizeof(zero), crc);
			crc = desc.pGS != nullptr ? CalcCrc32(desc.pGS->GetData(), desc.pGS->GetSize(), crc) : CalcCrc32(&zero, sizeof(zero), crc);
			crc = desc.pDS != nullptr ? CalcCrc32(desc.pDS->GetData(), desc.pDS->GetSize(), crc) : CalcCrc32(&zero, sizeof(zero), crc);
			crc = desc.pHS != nullptr ? CalcCrc32(desc.pHS->GetData(), desc.pHS->GetSize(), crc) : CalcCrc32(&zero, sizeof(zero), crc);
		}

		// CRCから生成済みルートシグネチャを検索する
		// CRCの衝突は起きないことを祈る
		auto it = instanceMap_.find(crc);
		if (it != instanceMap_.end())
		{
			// 見つかった
			return RootSignatureHandle(this, it->first, it->second);
		}

		std::vector<RootParameter> rootParams;
		std::map<std::string, std::vector<int>> paramMap;
		auto ReflectShader = [&](Shader* pShader, u32 shaderVisibility)
		{
			ID3D12ShaderReflection* pReflection = nullptr;
			auto hr = D3DReflect(pShader->GetData(), pShader->GetSize(), IID_PPV_ARGS(&pReflection));
			if (FAILED(hr))
			{
				return false;
			}

			D3D12_SHADER_DESC sdesc;
			hr = pReflection->GetDesc(&sdesc);
			if (FAILED(hr))
			{
				return false;
			}

			// バインドリソースを列挙する
			for (u32 i = 0; i < sdesc.BoundResources; i++)
			{
				D3D12_SHADER_INPUT_BIND_DESC bd;
				pReflection->GetResourceBindingDesc(i, &bd);

				RootParameterType::Type paramType = RootParameterType::ConstantBuffer;
				switch (bd.Type)
				{
				case D3D_SHADER_INPUT_TYPE::D3D_SIT_CBUFFER:
					paramType = RootParameterType::ConstantBuffer; break;
				case D3D_SHADER_INPUT_TYPE::D3D_SIT_SAMPLER:
					paramType = RootParameterType::Sampler; break;
				case D3D_SHADER_INPUT_TYPE::D3D_SIT_TEXTURE:
				case D3D_SHADER_INPUT_TYPE::D3D_SIT_STRUCTURED:
				case D3D_SHADER_INPUT_TYPE::D3D_SIT_BYTEADDRESS:
					paramType = RootParameterType::ShaderResource; break;
				case D3D_SHADER_INPUT_TYPE::D3D_SIT_UAV_RWSTRUCTURED:
				case D3D_SHADER_INPUT_TYPE::D3D_SIT_UAV_RWBYTEADDRESS:
				case D3D_SHADER_INPUT_TYPE::D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
				case D3D_SHADER_INPUT_TYPE::D3D_SIT_UAV_APPEND_STRUCTURED:
				case D3D_SHADER_INPUT_TYPE::D3D_SIT_UAV_CONSUME_STRUCTURED:
					paramType = RootParameterType::UnorderedAccess; break;
				default:
					return false;
				}

				auto findIt = paramMap.find(bd.Name);
				if (findIt != paramMap.end())
				{
					// すでに存在している
					bool isStored = false;
					for (auto index : findIt->second)
					{
						auto&& param = rootParams[index];
						if (param.type != paramType)
						{
							// 同名のリソースは同一タイプのみを許容
							return false;
						}
						if (param.registerIndex == bd.BindPoint)
						{
							param.shaderVisibility |= shaderVisibility;
							isStored = true;
							break;
						}
					}
					if (!isStored)
					{
						RootParameter param;
						param.type = paramType;
						param.shaderVisibility = shaderVisibility;
						param.registerIndex = bd.BindPoint;
						findIt->second.push_back(rootParams.size());
						rootParams.push_back(param);
					}
				}
				else
				{
					// 新規追加
					RootParameter param;
					param.type = paramType;
					param.shaderVisibility = shaderVisibility;
					param.registerIndex = bd.BindPoint;

					std::vector<int> indices;
					indices.push_back(rootParams.size());
					paramMap[bd.Name] = indices;
					rootParams.push_back(param);
				}
			}
			return true;
		};

		// 各シェーダのリソースを列挙する
		bool isGraphics = true;
		if (desc.pCS)
		{
			if (!ReflectShader(desc.pCS, ShaderVisibility::Compute))
			{
				return RootSignatureHandle(nullptr, 0, nullptr);
			}
			isGraphics = false;
		}
		else
		{
			if (desc.pVS && !ReflectShader(desc.pVS, ShaderVisibility::Vertex))
			{
				return RootSignatureHandle(nullptr, 0, nullptr);
			}
			if (desc.pPS && !ReflectShader(desc.pPS, ShaderVisibility::Pixel))
			{
				return RootSignatureHandle(nullptr, 0, nullptr);
			}
			if (desc.pGS && !ReflectShader(desc.pGS, ShaderVisibility::Geometry))
			{
				return RootSignatureHandle(nullptr, 0, nullptr);
			}
			if (desc.pDS && !ReflectShader(desc.pDS, ShaderVisibility::Domain))
			{
				return RootSignatureHandle(nullptr, 0, nullptr);
			}
			if (desc.pHS && !ReflectShader(desc.pHS, ShaderVisibility::Hull))
			{
				return RootSignatureHandle(nullptr, 0, nullptr);
			}
		}

		// 新規ルートシグネチャを生成する
		RootSignatureInstance* pNewInstance = new RootSignatureInstance();
		pNewInstance->isGraphics_ = isGraphics;
		pNewInstance->slotMap_ = paramMap;

		RootSignatureDesc rsDesc;
		rsDesc.numParameters = rootParams.size();
		rsDesc.pParameters = rootParams.data();
		if (!pNewInstance->rootSig_.Initialize(pDevice_, rsDesc))
		{
			delete pNewInstance;
			return RootSignatureHandle(nullptr, 0, nullptr);
		}

		// マップに登録
		instanceMap_[crc] = pNewInstance;

		return RootSignatureHandle(this, crc, pNewInstance);
	}

	//-------------------------------------------------
	// ルートシグネチャを解放する
	//-------------------------------------------------
	void RootSignatureManager::ReleaseRootSignature(u32 crc, RootSignatureInstance* pInst)
	{
		auto findIt = instanceMap_.find(crc);
		if (findIt != instanceMap_.end())
		{
			if (findIt->second == pInst)
			{
				pInst->referenceCounter_--;
				if (pInst->referenceCounter_ == 0)
				{
					delete pInst;
					instanceMap_.erase(findIt);
				}
			}
		}
	}

}	// namespace sl12


//	EOF
