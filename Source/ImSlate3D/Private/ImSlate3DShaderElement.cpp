// Copyright Epic Games, Inc. All Rights Reserved.
#include "ImSlate3DShaderElement.h"  // IWYU: must be the first include (matches .cpp name)
#include "ImSlate3DShaderInternal.h"

#if defined(IMSLATE3D_API)

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "CommonRenderResources.h"
#include "GlobalRenderResources.h"  // GWhiteTexture
#include "PipelineStateCache.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "RHIResourceUtils.h"

namespace ImSlate
{
TGlobalResource<FImSlate3DVertexDeclaration> GImSlate3DVertexDeclaration;
TGlobalResource<FImSlate3DQuadIndexBuffer> GImSlate3DQuadIndexBuffer;

void FImSlate3DVertexDeclaration::InitRHI(FRHICommandListBase& RHICmdList)
{
	FVertexDeclarationElementList Elements;
	const uint16 Stride = sizeof(FImSlate3DVertex);
	// ATTRIBUTE0: ScreenPosInvW (float4 — xy=screen px, z=1/clipW)
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FImSlate3DVertex, ScreenPosInvW), VET_Float4, 0, Stride));
	// ATTRIBUTE1: UV (float2)
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FImSlate3DVertex, UV), VET_Float2, 1, Stride));
	// ATTRIBUTE2: Color (float4, linear)
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FImSlate3DVertex, Color), VET_Float4, 2, Stride));
	// ATTRIBUTE3: LocalUV (float2, box-local 0..1 for rounded-box SDF)
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FImSlate3DVertex, LocalUV), VET_Float2, 3, Stride));
	VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
}

void FImSlate3DQuadIndexBuffer::InitRHI(FRHICommandListBase& RHICmdList)
{
	static const uint16 Indices[6] = { 0, 1, 2, 0, 2, 3 };  // TL,TR,BR,BL
	IndexBufferRHI = UE::RHIResourceUtils::CreateIndexBufferFromArray(
		RHICmdList, TEXT("ImSlate3DQuadIB"), EBufferUsageFlags::Static, MakeConstArrayView(Indices));
}

// Combined pass parameters for the raster pass: nested VS/PS param structs (standard UE pattern,
// cf. CapsuleShadowRendering.cpp:678-679) + the vertex buffer (declared so RDG tracks its
// upload→read dependency) + the render target binding.
BEGIN_SHADER_PARAMETER_STRUCT(FIm3DPassParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FImSlate3DVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FImSlate3DPS::FParameters, PS)
	RDG_BUFFER_ACCESS(VertexBuffer, ERHIAccess::VertexOrIndexBuffer)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void FIm3DShaderElement::Draw_RenderThread(FRDGBuilder& GraphBuilder, const FDrawPassInputs& Inputs)
{
	check(IsInRenderingThread());
	if (Quads.Num() == 0 || !Inputs.OutputTexture)
	{
		return;
	}

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FImSlate3DVS> VertexShader(ShaderMap);
	TShaderMapRef<FImSlate3DPS> PixelShader(ShaderMap);

	// TWO DIFFERENT rects — this is the fix for the whole-3D render offset:
	//  • ProjViewRect = the rect FImProjector::Project() projected into (ConstrainedViewRect, origin 0,0).
	//    The vertex screen coords are in THIS space, so screen->NDC must use it.
	//  • Inputs.SceneViewRect = where the game view sits inside the OutputTexture (origin may be 58,212).
	//    SetViewport must use THIS so NDC maps to the right pixels of the output.
	// Previously ScreenToClip used SceneViewRect's Min as bias while the verts were in ProjViewRect's
	// 0-origin space → the whole sheet (and the hit cross) shifted by SceneViewRect.Min. (R: render offset)
	const FIntRect ClipRect = ProjViewRect;             // screen->NDC basis (verts are in this space)
	const FIntRect ViewportRect = Inputs.SceneViewRect; // NDC->output pixels (raster target region)
	const FIntPoint OutExtent = Inputs.OutputTexture->Desc.Extent;
#if !UE_BUILD_SHIPPING
	// One-shot: if ProjViewRect (verts' space) size ≠ SceneViewRect (raster region) size, the 3D content gets
	// scaled vs the hit/probe space → render & cursor diverge. ("world RT ≠ viewport".)
	static bool bLoggedRects = false;
	if (!bLoggedRects)
	{
		bLoggedRects = true;
		UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][rects] ProjView=%dx%d@(%d,%d) SceneView=%dx%d@(%d,%d) OutTex=%dx%d"),
			ClipRect.Width(), ClipRect.Height(), ClipRect.Min.X, ClipRect.Min.Y,
			ViewportRect.Width(), ViewportRect.Height(), ViewportRect.Min.X, ViewportRect.Min.Y,
			OutExtent.X, OutExtent.Y);
	}
#endif

	const float VW = (float)ClipRect.Width();
	const float VH = (float)ClipRect.Height();
	const FVector4f ScreenToClip(
		 2.0f / VW,
		-2.0f / VH,
		(float)(ClipRect.Min.X + ClipRect.Max.X) / -VW,
		(float)(ClipRect.Min.Y + ClipRect.Max.Y) /  VH);

	// One pass per quad. We DON'T clear the output — we composite on top of whatever Slate already
	// drew (ELoad), since the 3D box replaces only its own pixels.
	for (const FIm3DQuad& Quad : Quads)
	{

		// Upload this quad's 4 verts as an RDG vertex buffer (index buffer is the static global one).
		FRDGBufferRef VB = CreateVertexBuffer(GraphBuilder, TEXT("ImSlate3D.VB"),
			FRDGBufferDesc::CreateBufferDesc(sizeof(FImSlate3DVertex), 4),
			Quad.Verts, sizeof(FImSlate3DVertex) * 4);

		// Element texture: register the captured FRHITexture into RDG, or fall back to white.
		FRHITexture* SrcTex = (Quad.bUseTexture && Quad.Texture.IsValid()) ? Quad.Texture.GetReference()
			: (GWhiteTexture ? GWhiteTexture->TextureRHI.GetReference() : nullptr);
		if (!SrcTex)
		{
			continue;
		}
		FRDGTextureRef TexRDG = RegisterExternalTexture(GraphBuilder, SrcTex, TEXT("ImSlate3D.ElementTex"));

		FIm3DPassParameters* Pass = GraphBuilder.AllocParameters<FIm3DPassParameters>();
		Pass->VS.ScreenToClip = ScreenToClip;
		Pass->PS.ElementTexture = TexRDG;
		Pass->PS.ElementSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Pass->PS.Kind = (float)(uint8)Quad.Kind;
		Pass->PS.RoundedSize = Quad.RoundedSize;
		Pass->PS.CornerRadii = Quad.CornerRadii;
		Pass->PS.OutlineWeight = Quad.OutlineWeight;
		Pass->PS.OutlineColor = Quad.OutlineColor;
		Pass->VertexBuffer = VB;
		Pass->RenderTargets[0] = FRenderTargetBinding(Inputs.OutputTexture, ERenderTargetLoadAction::ELoad);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("ImSlate3DQuad"),
			Pass,
			ERDGPassFlags::Raster,
			[VertexShader, PixelShader, Pass, VB, ViewportRect](FRHICommandList& RHICmdList)
			{
				// Viewport = SceneViewRect (game view region in the OutputTexture). NDC (built from
				// ProjViewRect's 0-origin space) maps here → correct output pixels.
				RHICmdList.SetViewport(ViewportRect.Min.X, ViewportRect.Min.Y, 0.0f, ViewportRect.Max.X, ViewportRect.Max.Y, 1.0f);

				FGraphicsPipelineStateInitializer PSOInit;
				RHICmdList.ApplyCachedRenderTargets(PSOInit);
				// Premultiplied-style UI alpha blend (SrcAlpha, InvSrcAlpha for color; keep dst alpha sane).
				PSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha,
					BO_Add, BF_InverseDestAlpha, BF_One>::GetRHI();
				PSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();  // double-sided; backface handled on CPU
				PSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
				PSOInit.PrimitiveType = PT_TriangleList;
				PSOInit.BoundShaderState.VertexDeclarationRHI = GImSlate3DVertexDeclaration.VertexDeclarationRHI;
				PSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				PSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				SetGraphicsPipelineState(RHICmdList, PSOInit, 0);

				SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), Pass->VS);
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), Pass->PS);

				RHICmdList.SetStreamSource(0, VB->GetRHI(), 0);
				RHICmdList.DrawIndexedPrimitive(GImSlate3DQuadIndexBuffer.IndexBufferRHI, 0, 0, 4, 0, 2, 1);
			});
	}
}

}  // namespace ImSlate

// Bind the C++ shader classes to the .usf via the VIRTUAL path. The "/Plugin/ImSlate" prefix is
// registered with AddShaderSourceDirectoryMapping in this module's StartupModule; the physical file
// lives at <ImSlate plugin>/Shaders/Private/ImSlate3DElement.usf (shared — the .usf stays under the
// ImSlate plugin dir, this module just maps + uses it).
IMPLEMENT_GLOBAL_SHADER(ImSlate::FImSlate3DVS, "/Plugin/ImSlate/Private/ImSlate3DElement.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(ImSlate::FImSlate3DPS, "/Plugin/ImSlate/Private/ImSlate3DElement.usf", "MainPS", SF_Pixel);

#endif  // defined(IMSLATE3D_API)
