//--------------------------------------------------------------------------------------
// CombinedUVPS.hlsl
//--------------------------------------------------------------------------------------
Texture2D txInputU : register(t0);
Texture2D txInputV : register(t1);
Texture1D txInputShift : register(t2);

SamplerState GenericSampler : register(s0);

struct PS_INPUT
{
	float4 Pos : SV_POSITION;
	float2 Tex : TEXCOORD;
};

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float CombinedUVPS(PS_INPUT input) : SV_Target
{
	float fShift = txInputShift.Sample(GenericSampler, input.Tex.x);

	if(fShift == 0.0f)
		return txInputU.Sample(GenericSampler, input.Tex);
	else
		return txInputV.Sample(GenericSampler, input.Tex);
}