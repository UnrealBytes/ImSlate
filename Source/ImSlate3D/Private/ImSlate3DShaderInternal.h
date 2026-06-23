// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ImSlate3DShaderElement.h"

#if defined(IMSLATE3D_API)

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphResources.h"
#include "RHIStaticStates.h"

namespace ImSlate
{
// Vertex declaration for FImSlate3DVertex. Global static (TGlobalResource), mirrors FFilterVertexDeclaration.
class FImSlate3DVertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;
	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
	virtual void ReleaseRHI() override { VertexDeclarationRHI.SafeRelease(); }
};
extern TGlobalResource<FImSlate3DVertexDeclaration> GImSlate3DVertexDeclaration;

// Static index buffer for one quad with vertex order TL(0) TR(1) BR(2) BL(3): {0,1,2, 0,2,3}.
class FImSlate3DQuadIndexBuffer : public FIndexBuffer
{
public:
	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
};
extern TGlobalResource<FImSlate3DQuadIndexBuffer> GImSlate3DQuadIndexBuffer;

// Vertex shader: passes clip-space position straight through (HW does the perspective divide).
class FImSlate3DVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FImSlate3DVS);
	SHADER_USE_PARAMETER_STRUCT(FImSlate3DVS, FGlobalShader);

	// ScreenToClip: Slate's ortho screen->NDC transform (xy=scale, zw=bias), built C++-side from the
	// OutputTexture view rect so positions match Slate's own elements exactly.
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector4f, ScreenToClip)
	END_SHADER_PARAMETER_STRUCT()
};

// Pixel shader: samples the captured element texture and modulates by the vertex color.
class FImSlate3DPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FImSlate3DPS);
	SHADER_USE_PARAMETER_STRUCT(FImSlate3DPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ElementTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, ElementSampler)
		SHADER_PARAMETER(float, Kind)            // 0=Textured 1=RoundedBox 2=Glyph
		SHADER_PARAMETER(FVector2f, RoundedSize)
		SHADER_PARAMETER(FVector4f, CornerRadii)
		SHADER_PARAMETER(float, OutlineWeight)
		SHADER_PARAMETER(FVector4f, OutlineColor)
	END_SHADER_PARAMETER_STRUCT()
};

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
