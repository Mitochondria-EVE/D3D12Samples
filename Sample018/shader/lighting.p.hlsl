#include "common.hlsli"

struct PSInput
{
	float4	position	: SV_POSITION;
	float3	normal		: NORMAL;
	float2	uv			: TEXCOORD0;
};

ConstantBuffer<SceneCB>		cbScene			: register(b0);
ConstantBuffer<LightCB>		cbLight			: register(b1);

Texture2D		texColor	: register(t0);
SamplerState	texColor_s	: register(s0);

float4 main(PSInput In) : SV_TARGET0
{
	uint2 pixel = uint2(In.position.xy);

	float4 baseColor = texColor.Sample(texColor_s, In.uv);
	float3 normal = normalize(In.normal);
	float LoN = saturate(dot(normal, -cbLight.lightDir.rgb));
	float3 directColor = cbLight.lightColor.rgb * LoN;
	float3 skyColor = SkyColor(normal) * cbLight.skyPower;
	return float4((directColor + skyColor) * baseColor.rgb, 1);
}
