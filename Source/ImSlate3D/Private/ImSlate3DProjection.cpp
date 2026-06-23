// Copyright Epic Games, Inc. All Rights Reserved.
#include "ImSlate3DProjection.h"

#if defined(IMSLATE3D_API)

#include "ImSlate3D.h"                       // FImProjector
#include "ImSlate3DShaderElement.h"          // FIm3DQuad / EIm3DQuadKind
#include "ImSlate3DHost.h"                   // FImSlate3DHost — camera proj data (no Engine dep)
#include "Rendering/DrawElements.h"
#include "Rendering/DrawElementTypes.h"
#include "Rendering/DrawElementCoreTypes.h"
#include "Rendering/RenderingCommon.h"
#include "Styling/CoreStyle.h"
#include "Textures/SlateShaderResource.h"   // FSlateShaderResourceProxy (box brush texture)
#include "Fonts/FontCache.h"                 // FSlateFontCache, glyph atlas data

namespace ImSlate
{

void AppendScreenQuad3D(const FVector2f& P0, const FVector2f& P1, const FVector2f& P2,
	const FVector2f& P3, const FVector4f& Color, TArray<FIm3DQuad>& OutQuads)
{
	FIm3DQuad Q;
	Q.Kind = EIm3DQuadKind::Textured;
	Q.bUseTexture = false;
	const FVector2f Pts[4] = { P0, P1, P2, P3 };
	for (int32 v = 0; v < 4; ++v)
	{
		Q.Verts[v].ScreenPosInvW = FVector4f(Pts[v].X, Pts[v].Y, 0.f, 0.f);  // InvW=0: flat colour
		Q.Verts[v].Color = Color;
		Q.Verts[v].UV = FVector2f(0.f, 0.f);
		Q.Verts[v].LocalUV = FVector2f(0.f, 0.f);
	}
	OutQuads.Add(Q);
}

bool AppendWorldSegmentQuad3D(const FImProjector& Proj, const FVector& A, const FVector& B,
	float ScreenThickness, const FVector4f& Color, TArray<FIm3DQuad>& OutQuads)
{
	const TOptional<FVector2f> SA = Proj.ProjectWorld(A);
	const TOptional<FVector2f> SB = Proj.ProjectWorld(B);
	if (!SA || !SB)
	{
		return false;  // behind camera
	}
	FVector2f Dir = *SB - *SA;
	if (Dir.SizeSquared() <= UE_SMALL_NUMBER)
	{
		return false;  // zero-length on screen
	}
	Dir.Normalize();
	const FVector2f Perp(-Dir.Y * ScreenThickness * 0.5f, Dir.X * ScreenThickness * 0.5f);
	AppendScreenQuad3D(*SA + Perp, *SB + Perp, *SB - Perp, *SA - Perp, Color, OutQuads);
	return true;
}

bool ImBuildProjector(FVector2f WidgetSize, const FImWorldPlacement& Placement, FImProjector& OutProj)
{
	// Camera matrices via the host hook (the Engine-coupled query lives in the main module).
	const FImSlate3DHost& Host = FImSlate3DHost::Get();
	if (!Host.GetCameraProjData)
	{
		return false;
	}
	const FImCameraProjData Data = Host.GetCameraProjData();
	if (!Data.bValid)
	{
		return false;
	}
	OutProj.Build(WidgetSize, Placement, Data.ViewProj, Data.ViewRect);
	return OutProj.IsVisible();
}

FImViewPoint ImGetViewCentre(double DistanceFromCamera)
{
	FImViewPoint Out;
	const FImSlate3DHost& Host = FImSlate3DHost::Get();
	if (!Host.GetCameraProjData) { return Out; }
	const FImCameraProjData Data = Host.GetCameraProjData();
	if (!Data.bValid) { return Out; }

	// Inverse view-proj: unproject the screen-CENTRE (NDC 0,0) at two depths to recover the camera ray. UE uses
	// reversed-Z, so clip z=1 → near plane, z=0 → far plane. Deproject both, perspective-divide, and the ray is
	// near→far; the near point ≈ the camera position (good enough to anchor placement + orientation).
	const FMatrix InvVP = Data.ViewProj.InverseFast();
	const FVector4 NearH = InvVP.TransformFVector4(FVector4(0.0, 0.0, 1.0, 1.0));
	const FVector4 FarH  = InvVP.TransformFVector4(FVector4(0.0, 0.0, 0.0, 1.0));
	if (FMath::IsNearlyZero(NearH.W) || FMath::IsNearlyZero(FarH.W)) { return Out; }
	const FVector Near(NearH.X / NearH.W, NearH.Y / NearH.W, NearH.Z / NearH.W);
	const FVector Far (FarH.X  / FarH.W,  FarH.Y  / FarH.W,  FarH.Z  / FarH.W);
	const FVector Forward = (Far - Near).GetSafeNormal();
	if (Forward.IsNearlyZero()) { return Out; }

	Out.bValid = true;
	Out.CameraPos = Near;
	Out.Forward = Forward;
	Out.Location = Near + Forward * DistanceFromCamera;
	// Orthonormal view basis from Forward (world up reference; fall back if looking straight up/down). UE convention
	// (left-handed): Right = WorldUp × Forward (forward +X → right +Y), Up = Forward × Right.
	FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
	if (Right.IsNearlyZero()) { Right = FVector::CrossProduct(FVector::ForwardVector, Forward).GetSafeNormal(); }
	const FVector Up = FVector::CrossProduct(Forward, Right).GetSafeNormal();
	Out.Right = Right;
	Out.Up = Up;
	Out.Orientation = FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat();  // X=forward, Z=up (UE convention)
	return Out;
}

void EmitCapturedElementQuads(const FSlateWindowElementList& CaptureList,
	const FImProjector& Proj, FSlateFontCache* FontCache, TArray<FIm3DQuad>& OutQuads)
{
	const FSlateDrawElementMap& Elements = CaptureList.GetUncachedDrawElements();

	auto EmitBoxArray = [&](const auto& BoxArray)
	{
		for (int32 i = 0; i < BoxArray.Num(); ++i)
		{
			const auto& Box = BoxArray[i];
			const FSlateRenderTransform& RT = Box.GetRenderTransform();
			const FVector2f Sz  = (FVector2f)Box.GetLocalSize();

			// Element corners in child-local px. The element's RenderTransform ALREADY includes the
			// translation to its final position (Slate's own ElementBatcher::AddBoxElement transforms a
			// LOCAL 0..LocalSize rect — it never adds GetPosition()). Adding GetPosition() here double-counts
			// the offset (a box at pos P lands at 2P), flinging nested elements off-screen. So transform the
			// local 0..Sz rect, matching Slate exactly. (R: double-position bug, see regressions.md)
			const FVector2f L0 = RT.TransformPoint(FVector2f(0.f, 0.f));
			const FVector2f L1 = RT.TransformPoint(FVector2f(Sz.X, 0.f));
			const FVector2f L2 = RT.TransformPoint(FVector2f(Sz.X, Sz.Y));
			const FVector2f L3 = RT.TransformPoint(FVector2f(0.f, Sz.Y));

			// Position each corner with the SAME FImProjector::Project() the (correct) CPU path uses —
			// so the on-screen position is identical. Pack screen px (xy) + InvW (1/clipW, for the PS's
			// perspective-correct UV) into ScreenPosInvW. ProjectToClip gives the real clip W.
			const TOptional<FVector2f> S0 = Proj.Project(L0);
			const TOptional<FVector2f> S1 = Proj.Project(L1);
			const TOptional<FVector2f> S2 = Proj.Project(L2);
			const TOptional<FVector2f> S3 = Proj.Project(L3);
			if (!(S0 && S1 && S2 && S3))
			{
				continue;  // a corner behind the camera
			}
			const float W0 = Proj.ProjectToClip(L0).W;
			const float W1 = Proj.ProjectToClip(L1).W;
			const float W2 = Proj.ProjectToClip(L2).W;
			const float W3 = Proj.ProjectToClip(L3).W;

			FIm3DQuad Quad;
			Quad.Verts[0].ScreenPosInvW = FVector4f(S0->X, S0->Y, W0 != 0.f ? 1.f / W0 : 0.f, 0.f);
			Quad.Verts[1].ScreenPosInvW = FVector4f(S1->X, S1->Y, W1 != 0.f ? 1.f / W1 : 0.f, 0.f);
			Quad.Verts[2].ScreenPosInvW = FVector4f(S2->X, S2->Y, W2 != 0.f ? 1.f / W2 : 0.f, 0.f);
			Quad.Verts[3].ScreenPosInvW = FVector4f(S3->X, S3->Y, W3 != 0.f ? 1.f / W3 : 0.f, 0.f);

			// Texture: pull the element's own brush texture (NativeTexture proxies expose a
			// FSlateTexture2DRHIRef → FTextureRHIRef via GetRHIRef). UV = the proxy's atlas sub-region
			// so atlased / 9-slice brushes sample correctly. The FTextureRHIRef is refcounted → safe to
			// hand to the render thread.
			FVector2f UVMin(0.f, 0.f), UVMax(1.f, 1.f);
			const FSlateShaderResourceProxy* Proxy = Box.GetResourceProxy();
			if (Proxy && Proxy->Resource && Proxy->Resource->GetType() == ESlateShaderResource::NativeTexture)
			{
				FTextureRHIRef Tex = static_cast<TSlateTexture<FTextureRHIRef>*>(Proxy->Resource)->GetTypedResource();
				if (Tex.IsValid())
				{
					Quad.Texture = Tex;
					Quad.bUseTexture = true;
					UVMin = Proxy->StartUV;
					UVMax = Proxy->StartUV + Proxy->SizeUV;
				}
			}

			const FLinearColor Tint = Box.GetTint();  // linear; shader expects linear color
			const FVector4f Col(Tint.R, Tint.G, Tint.B, Tint.A);
			Quad.Verts[0].UV = FVector2f(UVMin.X, UVMin.Y);
			Quad.Verts[1].UV = FVector2f(UVMax.X, UVMin.Y);
			Quad.Verts[2].UV = FVector2f(UVMax.X, UVMax.Y);
			Quad.Verts[3].UV = FVector2f(UVMin.X, UVMax.Y);
			Quad.Verts[0].Color = Quad.Verts[1].Color = Quad.Verts[2].Color = Quad.Verts[3].Color = Col;
			// box-local 0..1 for the rounded-box SDF (TL=0,0  TR=1,0  BR=1,1  BL=0,1).
			Quad.Verts[0].LocalUV = FVector2f(0.f, 0.f);
			Quad.Verts[1].LocalUV = FVector2f(1.f, 0.f);
			Quad.Verts[2].LocalUV = FVector2f(1.f, 1.f);
			Quad.Verts[3].LocalUV = FVector2f(0.f, 1.f);

			// RoundedBox elements carry corner radii + outline → drive the SDF branch in the PS.
			if constexpr (std::is_same_v<std::decay_t<decltype(Box)>, FSlateRoundedBoxElement>)
			{
				Quad.Kind = EIm3DQuadKind::RoundedBox;
				Quad.RoundedSize = Sz;  // box px size (LocalUV*Size = pos inside the SDF)
				Quad.CornerRadii = Box.GetRadius();           // TL,TR,BR,BL
				Quad.OutlineWeight = Box.GetOutlineWeight();
				const FLinearColor OC = Box.GetOutlineColor();
				Quad.OutlineColor = FVector4f(OC.R, OC.G, OC.B, OC.A);
			}

			OutQuads.Add(Quad);
		}
	};

	EmitBoxArray(Elements.Get<(uint8)EElementType::ET_Box>());
	EmitBoxArray(Elements.Get<(uint8)EElementType::ET_RoundedBox>());
	EmitBoxArray(Elements.Get<(uint8)EElementType::ET_Border>());

	// --- Shaped text: one Glyph quad per glyph, sampling the font atlas (R8 coverage). Same layout math
	// as the CPU path's EmitShapedTextArray (mirrors FSlateElementBatcher::BuildShapedTextSequence). ---
	// FontCache is passed in by the host (it lives in the Slate module via FSlateApplication, which this
	// low-dependency module deliberately doesn't link). Null FontCache → text simply isn't emitted.
	const FFontOutlineSettings NoOutline = FFontOutlineSettings::NoOutline;

	// Core: project ONE shaped-glyph sequence (shared by ShapedText elements and freshly-shaped ET_Text).
	// RT already includes the text element's position; baseline/MaxHeight place glyphs relative to local 0.
	auto EmitGlyphSequence = [&](const FShapedGlyphSequencePtr& Seq, const FSlateRenderTransform& RT, const FLinearColor& Tint)
	{
		if (!FontCache || !Seq.IsValid())
		{
			return;
		}
		const FVector4f Col(Tint.R, Tint.G, Tint.B, Tint.A);
		const int16 Baseline = Seq->GetTextBaseline();
		const uint16 MaxHeight = Seq->GetMaxTextHeight();  // glyph top = (MaxHeight + Baseline) above baseline

		float LineX = 0.f;
		for (const FShapedGlyphEntry& G : Seq->GetGlyphsToRender())
		{
			if (G.bIsVisible)
			{
				const FShapedGlyphFontAtlasData AD = FontCache->GetShapedGlyphFontAtlasData(G, NoOutline);
				if (AD.Valid)
				{
					ISlateFontTexture* FontTex = FontCache->GetFontTexture(AD.TextureIndex);
					FSlateShaderResource* AtlasRes = FontTex ? FontTex->GetSlateTexture() : nullptr;
					FTextureRHIRef AtlasRHI;
					if (AtlasRes && AtlasRes->GetType() == ESlateShaderResource::NativeTexture)
					{
						AtlasRHI = static_cast<TSlateTexture<FTextureRHIRef>*>(AtlasRes)->GetTypedResource();
					}
					if (AtlasRHI.IsValid())
					{
						// X/Y from the text-LOCAL 0 origin (baseline-relative), NOT GetPosition(): the text
						// element's RenderTransform already includes the translation to its position, exactly
						// like Slate's AddShapedTextElement (StartLineX = TopLeft(0,0)+HOffset, position lives
						// in the RenderTransform). Adding Origin double-counts. (R: double-position bug)
						const float X = LineX + AD.HorizontalOffset + G.XOffset;
						// Match Slate's BuildShapedTextSequence (ElementBatcher.cpp:183):
						//   Y = LineY(0) - VerticalOffset + YOffset + (MaxHeight + TextBaseline)
						// We previously dropped the MaxHeight term, so glyphs sat too high (above the button).
						const float Y = (float)(MaxHeight + Baseline) - AD.VerticalOffset + G.YOffset;
						const float GW = (float)AD.USize;
						const float GH = (float)AD.VSize;

						const FVector2f L0 = RT.TransformPoint(FVector2f(X, Y));
						const FVector2f L1 = RT.TransformPoint(FVector2f(X + GW, Y));
						const FVector2f L2 = RT.TransformPoint(FVector2f(X + GW, Y + GH));
						const FVector2f L3 = RT.TransformPoint(FVector2f(X, Y + GH));

						const TOptional<FVector2f> S0 = Proj.Project(L0);
						const TOptional<FVector2f> S1 = Proj.Project(L1);
						const TOptional<FVector2f> S2 = Proj.Project(L2);
						const TOptional<FVector2f> S3 = Proj.Project(L3);
						if (S0 && S1 && S2 && S3)
						{
							const float AtlasW = (float)AtlasRes->GetWidth();
							const float AtlasH = (float)AtlasRes->GetHeight();
							const float U0 = AD.StartU / AtlasW, V0 = AD.StartV / AtlasH;
							const float U1 = (AD.StartU + AD.USize) / AtlasW, V1 = (AD.StartV + AD.VSize) / AtlasH;

							const float Wg0 = Proj.ProjectToClip(L0).W, Wg1 = Proj.ProjectToClip(L1).W;
							const float Wg2 = Proj.ProjectToClip(L2).W, Wg3 = Proj.ProjectToClip(L3).W;

							FIm3DQuad Q;
							Q.Kind = EIm3DQuadKind::Glyph;
							Q.Texture = AtlasRHI;
							Q.bUseTexture = true;
							auto SetGV = [&](int idx, const FVector2f& S, float Wv, float U, float V)
							{
								Q.Verts[idx].ScreenPosInvW = FVector4f(S.X, S.Y, Wv != 0.f ? 1.f / Wv : 0.f, 0.f);
								Q.Verts[idx].UV = FVector2f(U, V);
								Q.Verts[idx].Color = Col;
								Q.Verts[idx].LocalUV = FVector2f(0.f, 0.f);
							};
							SetGV(0, *S0, Wg0, U0, V0);
							SetGV(1, *S1, Wg1, U1, V0);
							SetGV(2, *S2, Wg2, U1, V1);
							SetGV(3, *S3, Wg3, U0, V1);
							OutQuads.Add(Q);
						}
					}
				}
			}
			LineX += G.XAdvance;
		}
	};

	// ShapedText elements already carry a glyph sequence.
	auto EmitGlyphArray = [&](const auto& TextArray)
	{
		for (int32 i = 0; i < TextArray.Num(); ++i)
		{
			const auto& Elem = TextArray[i];
			EmitGlyphSequence(Elem.GetShapedGlyphSequence(), Elem.GetRenderTransform(), Elem.GetTint());
		}
	};

	// Plain ET_Text elements (from STextBlock's FSlateDrawElement::MakeText) have NO pre-shaped sequence —
	// shape them here on the fly, then reuse the same glyph projection. This is why "Click"/title text was blank.
	auto EmitTextArray = [&](const auto& TextArray)
	{
		if (!FontCache)
		{
			return;
		}
		for (int32 i = 0; i < TextArray.Num(); ++i)
		{
			const auto& Elem = TextArray[i];
			const FString Text = Elem.GetText();
			if (Text.IsEmpty())
			{
				continue;
			}
			// FontScale = 1: the element's RenderTransform already carries layout scale (capture uses an identity
			// child geometry), and EmitGlyphSequence applies RT to glyph-local coords — using the RT scale here too
			// would double-count. Glyph size comes from FontInfo.Size. (Adjust if text reads too small/large.)
			const FShapedGlyphSequenceRef Seq = FontCache->ShapeUnidirectionalText(
				Text, Elem.GetFontInfo(), 1.0f, TextBiDi::ETextDirection::LeftToRight, ETextShapingMethod::Auto);
			EmitGlyphSequence(Seq, Elem.GetRenderTransform(), Elem.GetTint());
		}
	};

	EmitGlyphArray(Elements.Get<(uint8)EElementType::ET_ShapedText>());
	EmitTextArray(Elements.Get<(uint8)EElementType::ET_Text>());

	// --- Lines: each polyline segment becomes a solid quad (thickness expanded along the segment
	// normal), projected per-corner like boxes. Lets self-painted leaf widgets (e.g. the gizmo arrows,
	// drawn with FSlateDrawElement::MakeLines) ride the same 3D projection pipeline. No new shader —
	// segments rasterize as the existing Textured kind with the white texture (solid fill). ---
	auto EmitLineArray = [&](const auto& LineArray)
	{
		for (int32 i = 0; i < LineArray.Num(); ++i)
		{
			const auto& Line = LineArray[i];
			const FSlateRenderTransform& RT = Line.GetRenderTransform();  // already includes line position
			const TArray<FVector2f>& Pts = Line.GetPoints();
			if (Pts.Num() < 2)
			{
				continue;
			}
			const TArray<FLinearColor>& PtCols = Line.GetPointColors();
			const float HalfW = FMath::Max(Line.GetThickness(), 1.f) * 0.5f;
			const FLinearColor BaseTint = Line.GetTint();

			for (int32 s = 0; s + 1 < Pts.Num(); ++s)
			{
				const FVector2f A = Pts[s];
				const FVector2f B = Pts[s + 1];
				FVector2f Dir = B - A;
				if (Dir.SizeSquared() <= UE_SMALL_NUMBER)
				{
					continue;  // degenerate segment
				}
				Dir.Normalize();
				const FVector2f Perp(-Dir.Y * HalfW, Dir.X * HalfW);  // segment normal * half thickness

				// Quad corners in child-local px (same RT.TransformPoint→Project path as boxes, R003-safe:
				// the points are local; RT already carries the position, never add GetPosition()).
				const FVector2f C0 = A + Perp, C1 = B + Perp, C2 = B - Perp, C3 = A - Perp;
				const FVector2f L0 = RT.TransformPoint(C0);
				const FVector2f L1 = RT.TransformPoint(C1);
				const FVector2f L2 = RT.TransformPoint(C2);
				const FVector2f L3 = RT.TransformPoint(C3);

				const TOptional<FVector2f> S0 = Proj.Project(L0);
				const TOptional<FVector2f> S1 = Proj.Project(L1);
				const TOptional<FVector2f> S2 = Proj.Project(L2);
				const TOptional<FVector2f> S3 = Proj.Project(L3);
				if (!(S0 && S1 && S2 && S3))
				{
					continue;  // a corner behind the camera
				}
				const float W0 = Proj.ProjectToClip(L0).W;
				const float W1 = Proj.ProjectToClip(L1).W;
				const float W2 = Proj.ProjectToClip(L2).W;
				const float W3 = Proj.ProjectToClip(L3).W;

				// Per-point colors if supplied, else the element tint (start color → both ends of the segment).
				const FLinearColor ColA = PtCols.IsValidIndex(s)     ? PtCols[s]     : BaseTint;
				const FLinearColor ColB = PtCols.IsValidIndex(s + 1) ? PtCols[s + 1] : BaseTint;
				const FVector4f CA(ColA.R, ColA.G, ColA.B, ColA.A);
				const FVector4f CB(ColB.R, ColB.G, ColB.B, ColB.A);

				FIm3DQuad Quad;
				Quad.Kind = EIm3DQuadKind::Textured;
				Quad.bUseTexture = false;  // solid fill (white texture)
				Quad.Verts[0].ScreenPosInvW = FVector4f(S0->X, S0->Y, W0 != 0.f ? 1.f / W0 : 0.f, 0.f);
				Quad.Verts[1].ScreenPosInvW = FVector4f(S1->X, S1->Y, W1 != 0.f ? 1.f / W1 : 0.f, 0.f);
				Quad.Verts[2].ScreenPosInvW = FVector4f(S2->X, S2->Y, W2 != 0.f ? 1.f / W2 : 0.f, 0.f);
				Quad.Verts[3].ScreenPosInvW = FVector4f(S3->X, S3->Y, W3 != 0.f ? 1.f / W3 : 0.f, 0.f);
				// Corners 0,3 sit on point A; 1,2 on point B → color accordingly.
				Quad.Verts[0].Color = CA; Quad.Verts[3].Color = CA;
				Quad.Verts[1].Color = CB; Quad.Verts[2].Color = CB;
				for (int32 v = 0; v < 4; ++v)
				{
					Quad.Verts[v].UV = FVector2f(0.f, 0.f);
					Quad.Verts[v].LocalUV = FVector2f(0.f, 0.f);
				}
				OutQuads.Add(Quad);
			}
		}
	};

	EmitLineArray(Elements.Get<(uint8)EElementType::ET_Line>());

	// Shared helper: a thick polyline (local pts) → projected quads. Reused by Line(above, inlined) & Spline.
	auto EmitThickPolyline = [&](const TArray<FVector2f>& Pts, float Thickness, const FSlateRenderTransform& RT, const FVector4f& Col)
	{
		if (Pts.Num() < 2) { return; }
		const float HalfW = FMath::Max(Thickness, 1.f) * 0.5f;
		for (int32 s = 0; s + 1 < Pts.Num(); ++s)
		{
			FVector2f Dir = Pts[s + 1] - Pts[s];
			if (Dir.SizeSquared() <= UE_SMALL_NUMBER) { continue; }
			Dir.Normalize();
			const FVector2f Perp(-Dir.Y * HalfW, Dir.X * HalfW);
			const FVector2f L0 = RT.TransformPoint(Pts[s] + Perp),     L1 = RT.TransformPoint(Pts[s + 1] + Perp);
			const FVector2f L2 = RT.TransformPoint(Pts[s + 1] - Perp), L3 = RT.TransformPoint(Pts[s] - Perp);
			const TOptional<FVector2f> S0 = Proj.Project(L0), S1 = Proj.Project(L1), S2 = Proj.Project(L2), S3 = Proj.Project(L3);
			if (!(S0 && S1 && S2 && S3)) { continue; }
			const float W0 = Proj.ProjectToClip(L0).W, W1 = Proj.ProjectToClip(L1).W, W2 = Proj.ProjectToClip(L2).W, W3 = Proj.ProjectToClip(L3).W;
			FIm3DQuad Q; Q.Kind = EIm3DQuadKind::Textured; Q.bUseTexture = false;
			Q.Verts[0].ScreenPosInvW = FVector4f(S0->X, S0->Y, W0 != 0.f ? 1.f / W0 : 0.f, 0.f);
			Q.Verts[1].ScreenPosInvW = FVector4f(S1->X, S1->Y, W1 != 0.f ? 1.f / W1 : 0.f, 0.f);
			Q.Verts[2].ScreenPosInvW = FVector4f(S2->X, S2->Y, W2 != 0.f ? 1.f / W2 : 0.f, 0.f);
			Q.Verts[3].ScreenPosInvW = FVector4f(S3->X, S3->Y, W3 != 0.f ? 1.f / W3 : 0.f, 0.f);
			for (int32 v = 0; v < 4; ++v) { Q.Verts[v].Color = Col; Q.Verts[v].UV = FVector2f(0.f, 0.f); Q.Verts[v].LocalUV = FVector2f(0.f, 0.f); }
			OutQuads.Add(Q);
		}
	};

	// --- Splines: cubic bezier (P0..P3) → sampled polyline → thick quads (same projection as lines). ---
	auto EmitSplineArray = [&](const auto& SplineArray)
	{
		for (int32 i = 0; i < SplineArray.Num(); ++i)
		{
			const auto& Sp = SplineArray[i];
			const int32 N = 16;
			TArray<FVector2f> Pts; Pts.Reserve(N + 1);
			for (int32 k = 0; k <= N; ++k)
			{
				const float t = (float)k / (float)N, u = 1.f - t;
				// Bernstein cubic: u³P0 + 3u²t P1 + 3ut² P2 + t³P3
				const FVector2f B = Sp.P0 * (u*u*u) + Sp.P1 * (3.f*u*u*t) + Sp.P2 * (3.f*u*t*t) + Sp.P3 * (t*t*t);
				Pts.Add(B);
			}
			const FLinearColor Tint = Sp.GetTint();
			EmitThickPolyline(Pts, Sp.GetThickness(), Sp.GetRenderTransform(), FVector4f(Tint.R, Tint.G, Tint.B, Tint.A));
		}
	};
	EmitSplineArray(Elements.Get<(uint8)EElementType::ET_Spline>());

	// --- Gradients: between adjacent stops, a vertex-color-interpolated quad across the element rect. ---
	auto EmitGradientArray = [&](const auto& GradArray)
	{
		for (int32 i = 0; i < GradArray.Num(); ++i)
		{
			const auto& Gr = GradArray[i];
			if (Gr.GradientStops.Num() < 2) { continue; }
			const FSlateRenderTransform& RT = Gr.GetRenderTransform();
			const FVector2f LSize = (FVector2f)Gr.GetLocalSize();
			const bool bVert = (Gr.GradientType == Orient_Vertical);  // Orient_Vertical → varies along X (per engine)
			for (int32 s = 0; s + 1 < Gr.GradientStops.Num(); ++s)
			{
				const FSlateGradientStop& A = Gr.GradientStops[s];
				const FSlateGradientStop& Bs = Gr.GradientStops[s + 1];
				// Two corners of this band along gradient axis; full extent on the other axis.
				FVector2f Q0, Q1, Q2, Q3;  // 0,3 = A side; 1,2 = B side
				if (bVert)  // gradient runs along X
				{
					const float xA = A.Position.X, xB = Bs.Position.X;
					Q0 = FVector2f(xA, 0.f); Q3 = FVector2f(xA, LSize.Y);
					Q1 = FVector2f(xB, 0.f); Q2 = FVector2f(xB, LSize.Y);
				}
				else  // gradient runs along Y
				{
					const float yA = A.Position.Y, yB = Bs.Position.Y;
					Q0 = FVector2f(0.f, yA); Q3 = FVector2f(LSize.X, yA);
					Q1 = FVector2f(0.f, yB); Q2 = FVector2f(LSize.X, yB);
				}
				const FVector2f L0 = RT.TransformPoint(Q0), L1 = RT.TransformPoint(Q1), L2 = RT.TransformPoint(Q2), L3 = RT.TransformPoint(Q3);
				const TOptional<FVector2f> S0 = Proj.Project(L0), S1 = Proj.Project(L1), S2 = Proj.Project(L2), S3 = Proj.Project(L3);
				if (!(S0 && S1 && S2 && S3)) { continue; }
				const float W0 = Proj.ProjectToClip(L0).W, W1 = Proj.ProjectToClip(L1).W, W2 = Proj.ProjectToClip(L2).W, W3 = Proj.ProjectToClip(L3).W;
				const FVector4f CA(A.Color.R, A.Color.G, A.Color.B, A.Color.A);
				const FVector4f CB(Bs.Color.R, Bs.Color.G, Bs.Color.B, Bs.Color.A);
				FIm3DQuad Q; Q.Kind = EIm3DQuadKind::Textured; Q.bUseTexture = false;
				Q.Verts[0].ScreenPosInvW = FVector4f(S0->X, S0->Y, W0 != 0.f ? 1.f / W0 : 0.f, 0.f); Q.Verts[0].Color = CA;
				Q.Verts[1].ScreenPosInvW = FVector4f(S1->X, S1->Y, W1 != 0.f ? 1.f / W1 : 0.f, 0.f); Q.Verts[1].Color = CB;
				Q.Verts[2].ScreenPosInvW = FVector4f(S2->X, S2->Y, W2 != 0.f ? 1.f / W2 : 0.f, 0.f); Q.Verts[2].Color = CB;
				Q.Verts[3].ScreenPosInvW = FVector4f(S3->X, S3->Y, W3 != 0.f ? 1.f / W3 : 0.f, 0.f); Q.Verts[3].Color = CA;
				for (int32 v = 0; v < 4; ++v) { Q.Verts[v].UV = FVector2f(0.f, 0.f); Q.Verts[v].LocalUV = FVector2f(0.f, 0.f); }
				OutQuads.Add(Q);
			}
		}
	};
	EmitGradientArray(Elements.Get<(uint8)EElementType::ET_Gradient>());

	// --- CustomVerts: each index-triple → a triangle (degenerate quad), vertex pos/UV/color projected. ---
	auto EmitCustomVertsArray = [&](const auto& CVArray)
	{
		for (int32 i = 0; i < CVArray.Num(); ++i)
		{
			const auto& CV = CVArray[i];
			const TArray<FSlateVertex>& Vs = CV.Vertices;
			const TArray<SlateIndex>& Idx = CV.Indices;
			FTextureRHIRef TexRHI;
			if (CV.ResourceProxy && CV.ResourceProxy->Resource && CV.ResourceProxy->Resource->GetType() == ESlateShaderResource::NativeTexture)
			{
				TexRHI = static_cast<TSlateTexture<FTextureRHIRef>*>(CV.ResourceProxy->Resource)->GetTypedResource();
			}
			for (int32 t = 0; t + 2 < Idx.Num(); t += 3)
			{
				const FSlateVertex& VA = Vs[Idx[t]];
				const FSlateVertex& VB = Vs[Idx[t + 1]];
				const FSlateVertex& VC = Vs[Idx[t + 2]];
				// FSlateVertex.Position is already in render space (no extra RT); project each.
				auto ToL = [](const FSlateVertex& V) { return FVector2f(V.Position.X, V.Position.Y); };
				const FVector2f L0 = ToL(VA), L1 = ToL(VB), L2 = ToL(VC);
				const TOptional<FVector2f> S0 = Proj.Project(L0), S1 = Proj.Project(L1), S2 = Proj.Project(L2);
				if (!(S0 && S1 && S2)) { continue; }
				const float W0 = Proj.ProjectToClip(L0).W, W1 = Proj.ProjectToClip(L1).W, W2 = Proj.ProjectToClip(L2).W;
				FIm3DQuad Q; Q.Kind = EIm3DQuadKind::Textured; Q.bUseTexture = TexRHI.IsValid(); Q.Texture = TexRHI;
				auto Set = [&](int idx, const FVector2f& S, float Wv, const FSlateVertex& V)
				{
					Q.Verts[idx].ScreenPosInvW = FVector4f(S.X, S.Y, Wv != 0.f ? 1.f / Wv : 0.f, 0.f);
					Q.Verts[idx].UV = FVector2f(V.TexCoords[0], V.TexCoords[1]);
					Q.Verts[idx].Color = FVector4f(V.Color.R / 255.f, V.Color.G / 255.f, V.Color.B / 255.f, V.Color.A / 255.f);
					Q.Verts[idx].LocalUV = FVector2f(0.f, 0.f);
				};
				Set(0, *S0, W0, VA); Set(1, *S1, W1, VB); Set(2, *S2, W2, VC); Set(3, *S2, W2, VC);  // tri → degenerate quad
				OutQuads.Add(Q);
			}
		}
	};
	EmitCustomVertsArray(Elements.Get<(uint8)EElementType::ET_CustomVerts>());

	// --- Unsupported element types on a 3D plane: warn ONCE (don't silently drop). ---
#if !UE_BUILD_SHIPPING
	{
		static bool bWarnedUnsupported = false;
		if (!bWarnedUnsupported)
		{
			const int32 NViewport = Elements.Get<(uint8)EElementType::ET_Viewport>().Num();
			const int32 NCustom   = Elements.Get<(uint8)EElementType::ET_Custom>().Num();
			const int32 NPostProc = Elements.Get<(uint8)EElementType::ET_PostProcessPass>().Num();
			if (NViewport || NCustom || NPostProc)
			{
				bWarnedUnsupported = true;
				UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D] unsupported element types on 3D plane not projected: Viewport=%d Custom=%d PostProcess=%d"),
					NViewport, NCustom, NPostProc);
			}
		}
	}
#endif
}

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
