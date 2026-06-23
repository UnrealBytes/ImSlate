// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

// This module hosts the ImSlate 3D custom global shader (VS/PS) + its ICustomSlateElement. It exists
// SEPARATELY from the main ImSlate module for ONE reason: a global shader's type is registered at DLL
// load (static init of IMPLEMENT_GLOBAL_SHADER), which must happen BEFORE the engine compiles global
// shaders (LaunchEngineLoop CompileGlobalShaderMap). That requires LoadingPhase=PostConfigInit. The
// main ImSlate module can't be PostConfigInit — it links GMP/GenericStorages which are PreDefault, so
// loading ImSlate earlier would fail (their DLLs aren't loaded yet). This tiny module depends only on
// Core/RHI/RenderCore/SlateCore/Projects, so it can safely load at PostConfigInit. The main module
// (PreDefault, later) depends on this one, so these symbols are ready when ImSlate loads.

#include "ImSlate3DAPI.h"  // IMSLATE_3D_TRANSFORM gate (shared with main module via copy)

#if defined(IMSLATE3D_API)

#include "CoreMinimal.h"
#include "RHIResources.h"
#include "Rendering/RenderingCommon.h"  // ICustomSlateElement

namespace ImSlate
{
// How a quad's pixels are shaded. Selects the PS branch.
enum class EIm3DQuadKind : uint8
{
	Textured = 0,   // box/border: sample Texture (or white) * Color
	RoundedBox = 1, // rounded box: SDF rounded rect + outline (Slate's GetRoundedBoxElementColorInternal)
	Glyph = 2,      // shaped-text glyph: sample font atlas alpha (R8) as coverage * Color
};

// One vertex of a projected 3D quad. Position is the SCREEN-SPACE pixel from FImProjector::Project
// (same as the correct CPU path → identical on-screen position), plus InvW (= 1/clipW from the real
// camera) for perspective-correct UV recovery in the PS. LocalUV is the box-local 0..1 coord used by
// the rounded-box SDF (distinct from UV which is the texture/atlas sub-region).
struct FImSlate3DVertex
{
	FVector4f ScreenPosInvW;  // ATTRIBUTE0 — xy = screen px (Project), z = 1/clipW, w = unused
	FVector2f UV;             // ATTRIBUTE1 — texture/atlas UV
	FVector4f Color;          // ATTRIBUTE2 — linear per-vertex tint
	FVector2f LocalUV;        // ATTRIBUTE3 — box-local 0..1 (rounded-box SDF); 0 for others
};

// One projected quad to draw on the render thread. A GAME-THREAD SNAPSHOT: the 4 verts carry
// screen-space positions + InvW; the render thread only rasterizes. Texture is an FRHITexture*
// captured on the game thread (render-safe); null falls back to white.
struct FIm3DQuad
{
	FImSlate3DVertex Verts[4];   // TL, TR, BR, BL
	FTextureRHIRef   Texture;    // element brush / font atlas texture; null for solid fill
	bool             bUseTexture = false;
	EIm3DQuadKind    Kind = EIm3DQuadKind::Textured;

	// RoundedBox-only params (Slate GetRoundedBoxElementColorInternal): box size in px, 4 corner radii
	// (TL,TR,BR,BL), outline thickness px, outline linear color.
	FVector2f RoundedSize = FVector2f::ZeroVector;
	FVector4f CornerRadii = FVector4f(0, 0, 0, 0);
	float     OutlineWeight = 0.f;
	FVector4f OutlineColor = FVector4f(0, 0, 0, 0);
};

/**
 * FIm3DShaderElement — ICustomSlateElement that draws a batch of perspective quads with this module's
 * custom VS/PS (true GPU perspective, no RT). Submitted via FSlateDrawElement::MakeCustom from the main
 * module's OnPaint (the sole render path for the 3D box). Lifetime: held by a ThreadSafe TSharedPtr; the
 * quad snapshot is immutable after construction so Draw_RenderThread reads it freely.
 */
class IMSLATE3D_API FIm3DShaderElement : public ICustomSlateElement
{
public:
	// ProjViewRect = the FImProjector's constrained view rect (the pixel basis its clip coords map into).
	// We need it because ICustomSlateElement's Inputs.SceneViewRect may be in a different (DPI-scaled)
	// pixel space than the camera projection used to build the clip coords — the viewport must be set in
	// the space the clip NDC actually corresponds to.
	FIm3DShaderElement(TArray<FIm3DQuad>&& InQuads, FIntRect InProjViewRect)
		: Quads(MoveTemp(InQuads)), ProjViewRect(InProjViewRect) {}

	virtual void Draw_RenderThread(FRDGBuilder& GraphBuilder, const FDrawPassInputs& Inputs) override;

private:
	TArray<FIm3DQuad> Quads;
	FIntRect ProjViewRect;
};

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
