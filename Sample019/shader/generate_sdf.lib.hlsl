#include "common.hlsli"

struct HitData
{
	float	distance;
};

#define TMax			100000.0
#define	PI				3.14159265358979

// global
RaytracingAccelerationStructure		TLAS			: register(t0, space0);
StructuredBuffer<float3>			TraceDirections	: register(t1);
ConstantBuffer<BoxInfo>				cbBoxInfo		: register(b0);
ConstantBuffer<TraceInfo>			cbTraceInfo		: register(b1);
RWTexture3D<float>					rwSdf			: register(u0);


HitData GetInitialPayload()
{
	HitData ret;
	ret.distance = cbBoxInfo.extentWhole * 3;
	return ret;
}

bool IsHit(const HitData h)
{
	return (h.distance < cbBoxInfo.extentWhole * 3);
}

[shader("raygeneration")]
void RayGenerator()
{
	uint3 tex_pos = DispatchRaysIndex();
	float max_distance = cbBoxInfo.extentWhole * 3 * 2;
	float signed_distance = rwSdf[tex_pos] * max_distance;
	float3 origin = (cbBoxInfo.centerPos - cbBoxInfo.extentWhole.xxx) + (float3(tex_pos) + 0.5) * cbBoxInfo.sizeVoxel;

	// �w��񐔂̃��C�g���[�V���O
	for (uint c = 0; c < cbTraceInfo.dirCount; c++)
	{
		// ���C�g���[�X���ċ���������
		uint ray_flags = 0;
		//RayDesc ray = { origin, 0.0, TraceDirections[cbTraceInfo.dirOffset + c], TMax };
		RayDesc ray = { origin, 0.0, TraceDirections[cbTraceInfo.dirOffset + c], max_distance };
		HitData payload = GetInitialPayload();
		TraceRay(
			TLAS,
			ray_flags,
			~0,
			0,
			1,
			0,
			ray,
			payload);

		// �����̍X�V���K�v�Ȃ�X�V
		if (IsHit(payload))
		{
			if (abs(signed_distance) > abs(payload.distance))
			{
				signed_distance = payload.distance;
			}
		}
	}

	// ���K�����Ċi�[
	rwSdf[tex_pos] = signed_distance / max_distance;
}

[shader("closesthit")]
void ClosestHitProcessor(inout HitData payload : SV_RayPayload, in BuiltInTriangleIntersectionAttributes attr : SV_IntersectionAttributes)
{
	payload.distance = RayTCurrent();
	if (HitKind() != HIT_KIND_TRIANGLE_FRONT_FACE)
	{
		payload.distance = -payload.distance;
	}
}

[shader("miss")]
void MissProcessor(inout HitData payload : SV_RayPayload)
{
}

// EOF
