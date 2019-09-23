﻿#pragma once

#include "sl12/types.h"

#include <DirectXMath.h>
#include <cereal/cereal.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>


namespace sl12
{

	struct ResourceMeshBoundingSphere
	{
		float	centerX, centerY, centerZ;
		float	radius;

		template <class Archive>
		void serialize(Archive& ar)
		{
			ar(CEREAL_NVP(centerX), CEREAL_NVP(centerY), CEREAL_NVP(centerZ), CEREAL_NVP(radius));
		}
	};	// class ResourceMeshBoundingSphere

	class ResourceMeshMaterial
	{
		friend class cereal::access;

	public:
		ResourceMeshMaterial()
		{}
		~ResourceMeshMaterial()
		{}

	private:
		std::string					name_;
		std::vector<std::string>	textureNames_;


		template <class Archive>
		void serialize(Archive& ar)
		{
			ar(CEREAL_NVP(name_), CEREAL_NVP(textureNames_));
		}
	};	// class ResourceMeshMaterial

	class ResourceMeshMeshlet
	{
		friend class cereal::access;

	public:
		ResourceMeshMeshlet()
		{}
		~ResourceMeshMeshlet()
		{}

	private:
		u32							ibOffset_;
		u32							ibCount_;
		ResourceMeshBoundingSphere	boundingSphere_;


		template <class Archive>
		void serialize(Archive& ar)
		{
			ar(CEREAL_NVP(ibOffset_),
				CEREAL_NVP(ibCount_),
				CEREAL_NVP(boundingSphere_));
		}
	};	// class ResourceMeshMeshlet

	class ResourceMeshSubmesh
	{
		friend class cereal::access;

	public:
		ResourceMeshSubmesh()
		{}
		~ResourceMeshSubmesh()
		{}

	private:
		int									materialIndex_;
		u32									vbOffset_;
		u32									ibOffset_;
		std::vector<ResourceMeshMeshlet>	meshlets_;
		ResourceMeshBoundingSphere			boundingSphere_;


		template <class Archive>
		void serialize(Archive& ar)
		{
			ar(CEREAL_NVP(materialIndex_),
				CEREAL_NVP(vbOffset_),
				CEREAL_NVP(ibOffset_),
				CEREAL_NVP(meshlets_),
				CEREAL_NVP(boundingSphere_));
		}
	};	// class ResourceMeshSubmesh

	class ResourceMesh
	{
		friend class cereal::access;

	public:
		ResourceMesh()
		{}
		~ResourceMesh()
		{}

	private:
		std::vector<ResourceMeshMaterial>	materials_;
		std::vector< ResourceMeshSubmesh>	submeshes_;
		ResourceMeshBoundingSphere			boundingSphere_;

		std::vector<u8>						vbPosition_;
		std::vector<u8>						vbNormal_;
		std::vector<u8>						vbTangent_;
		std::vector<u8>						vbTexcoord_;
		std::vector<u8>						indexBuffer_;


		template <class Archive>
		void serialize(Archive& ar)
		{
			ar(CEREAL_NVP(materials_),
				CEREAL_NVP(submeshes_),
				CEREAL_NVP(boundingSphere_),
				CEREAL_NVP(vbPosition_),
				CEREAL_NVP(vbNormal_),
				CEREAL_NVP(vbTangent_),
				CEREAL_NVP(vbTexcoord_),
				CEREAL_NVP(indexBuffer_));
		}
	};	// class ResourceMesh

}	// namespace sl12


//	EOF