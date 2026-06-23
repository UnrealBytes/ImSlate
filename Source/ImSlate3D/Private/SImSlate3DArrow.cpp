// Copyright Epic Games, Inc. All Rights Reserved.
#include "SImSlate3DArrow.h"

#if defined(IMSLATE3D_API)

#include "SImSlate3DTransformBox.h"
#include "ImSlate3DShaderElement.h"   // FIm3DShaderElement / FIm3DQuad
#include "ImSlate3DProjection.h"      // ImBuildProjector / AppendWorldSegmentQuad3D (standalone projector)
#include "ImSlate3DShapedHit.h"       // shared PointInTri (per-pixel shape hit)
#include "ImSlate3DHost.h"            // FImSlate3DHost — bUseMouseForTouch hook (no direct Engine dep)
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Layout/Clipping.h"           // FSlateClippingZone (per-arrow AABB hit-clip)
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarImSlate3DGizmoStyle(
	TEXT("imslate.3d.GizmoStyle"), 0,
	TEXT("Gizmo arrow style: 0=Flat (flat arrow on a camera-facing sheet; default), "
	     "1=Volumetric3D (3D cylinder+cone standing out along the world axis). Applies next paint."));

static TAutoConsoleVariable<int32> CVarImSlate3DArrowVolumeHint(
	TEXT("imslate.3d.ArrowVolumeHint"), 1,
	TEXT("Flat arrow 3D hint: 1=draw cylinder/cone base ellipses under the flat arrow (the camera-facing "
	     "half shows → looks volumetric), 0=plain flat arrow. Ellipse flatness follows the axis tilt."));

namespace ImSlate
{
EImArrowStyle GetIm3DArrowStyle()
{
	return CVarImSlate3DGizmoStyle.GetValueOnGameThread() == 1 ? EImArrowStyle::Volumetric3D : EImArrowStyle::Flat;
}

// ---- one solid screen-space quad (TL,TR,BR,BL screen px) → FIm3DQuad (white texture, flat colour) ----
static void AppendScreenQuad(const FVector2f& P0, const FVector2f& P1, const FVector2f& P2, const FVector2f& P3,
	const FVector4f& Col, TArray<FIm3DQuad>& Out)
{
	FIm3DQuad Q;
	Q.Kind = EIm3DQuadKind::Textured;
	Q.bUseTexture = false;
	const FVector2f Pts[4] = { P0, P1, P2, P3 };
	for (int32 v = 0; v < 4; ++v)
	{
		Q.Verts[v].ScreenPosInvW = FVector4f(Pts[v].X, Pts[v].Y, 0.f, 0.f);  // InvW=0: flat colour
		Q.Verts[v].Color = Col;
		Q.Verts[v].UV = FVector2f(0.f, 0.f);
		Q.Verts[v].LocalUV = FVector2f(0.f, 0.f);
	}
	Out.Add(Q);
}

static void AppendWorldSegmentQuad(const FImProjector& Proj, const FVector& A, const FVector& B,
	float ScreenThickness, const FVector4f& Col, TArray<FIm3DQuad>& Out)
{
	const TOptional<FVector2f> SA = Proj.ProjectWorld(A);
	const TOptional<FVector2f> SB = Proj.ProjectWorld(B);
	if (!SA || !SB) { return; }
	FVector2f Dir = *SB - *SA;
	if (Dir.SizeSquared() <= UE_SMALL_NUMBER) { return; }
	Dir.Normalize();
	const FVector2f Perp(-Dir.Y * ScreenThickness * 0.5f, Dir.X * ScreenThickness * 0.5f);
	AppendScreenQuad(*SA + Perp, *SB + Perp, *SB - Perp, *SA - Perp, Col, Out);
}

// Fill a disc on the ARROW's billboard frame and project it → an ELLIPSE that gives the flat arrow a 3D
// hint. The disc basis is the SAME as the billboard arrow's: BWidth (= V, the in-screen ⊥A width dir) and
// BThick (= normalize(A × V), the billboard NORMAL pointing toward the camera). So the disc is the cross-
// section of an imaginary cylinder whose flat side faces the camera exactly like the arrow does → it tracks
// the billboard as the camera turns (no world-fixed ⊥Axis disc that detaches from the billboard). The
// Draw the cylinder/cone END-CAP ellipse ON THE BILLBOARD plane span(BWidth, A): the base LINE runs along
// BWidth (= the shaft width), and the half-ellipse bulges along the AXIS direction A toward the axis end
// (the arc of the end-cap seen edge-on). AxialBulge = the world reach along A of the bulge = RadiusW × tilt,
// where tilt = |A·viewUnit| (axis facing camera → end-cap faces us → full bulge; axis side-on → ~0 → the
// ellipse flattens to the base line = no cap visible). bTowardTip: bulge toward +A (cone base, toward tip)
// or −A (cylinder base, toward anchor). Only the OUTER half (the bulge) is drawn; it sits on the same
// billboard plane as the arrow so it tracks the camera consistently.
// End-cap as a SCREEN-SPACE ellipse (a circle squashed by the axis tilt — exactly what a world circle
// projects to, but computed directly in 2D so it's simple and the size is controllable):
//   center  = ProjectWorld(end center)
//   majorDir/a = the shaft-WIDTH screen direction & half-length (so the ellipse edge meets the shaft seamlessly)
//   minorDir   = the AXIS screen direction (⊥major in practice); minor半轴 b = a × Tilt
//   Tilt = |A·viewUnit|: 1 (axis facing camera) → b=a → full circle (end-cap fully seen);
//                        0 (axis side-on)       → b=0 → ellipse flattens to a line (no cap).
// The WHOLE ellipse is filled; the flat shaft/head drawn AFTER covers the inner half → only the outer
// (axis-end) half shows. Color is darker (cap shadow) for a depth cue.
static void AppendEndCapEllipse(const FImProjector& Proj, const FVector& CenterW, double RadiusW,
	const FVector& BWidth, const FVector& A, double Tilt, const FVector4f& Col, TArray<FIm3DQuad>& Out)
{
	if (BWidth.IsNearlyZero() || RadiusW <= 0.0 || Tilt < 0.1) { return; }  // side-on (tilt~0) → cap invisible, skip
	const TOptional<FVector2f> SC    = Proj.ProjectWorld(CenterW);
	const TOptional<FVector2f> SWide = Proj.ProjectWorld(CenterW + BWidth * RadiusW);  // width edge → major axis
	const TOptional<FVector2f> SAxis = Proj.ProjectWorld(CenterW + A * RadiusW);       // axis dir → minor axis
	if (!SC || !SWide || !SAxis) { return; }

	const FVector2f Major = *SWide - *SC;        // screen major axis vector (half), = shaft half-width on screen
	FVector2f MinorDir = *SAxis - *SC;
	if (MinorDir.SizeSquared() < 1.f) { MinorDir = FVector2f(-Major.Y, Major.X); }  // fallback ⊥major
	MinorDir = MinorDir.GetSafeNormal();
	const float MinorLen = Major.Size() * (float)Tilt;  // squash by tilt → ellipse
	const FVector2f Minor = MinorDir * MinorLen;
#if !UE_BUILD_SHIPPING
	if (IsImSlate3DDebugVerbose())
	{
		static int32 EN = 0;
		if (EN++ % 40 == 0)
			UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][cap] SC=(%.0f,%.0f) majorLen=%.1f minorLen=%.1f Tilt=%.3f R=%.1f"),
				SC->X, SC->Y, Major.Size(), MinorLen, Tilt, RadiusW);
	}
#endif

	const int32 N = 20;  // full ellipse
	FVector2f PPrev = *SC + Major;  // θ=0
	for (int32 k = 1; k <= N; ++k)
	{
		const double Ang = 2.0 * PI * (double)k / (double)N;
		const FVector2f Pk = *SC + Major * (float)FMath::Cos(Ang) + Minor * (float)FMath::Sin(Ang);
		AppendScreenQuad(*SC, PPrev, Pk, Pk, Col, Out);  // triangle fan
		PPrev = Pk;
	}
}

// PointInTri now shared via ImSlate3DShapedHit.h (used by both the viewport-top arrow and in-window gizmo).

void SImSlate3DArrow::Construct(const FArguments& InArgs)
{
	Target = InArgs._Target;
	WorldAxis = InArgs._WorldAxis.GetSafeNormal();
	Color = InArgs._Color;
	OverlapOrder = InArgs._OverlapOrder;
	ShaftLength = InArgs._ShaftLength;
	ShaftWidth = InArgs._ShaftWidth;
	HeadLength = InArgs._HeadLength;
	HeadWidth = InArgs._HeadWidth;
	bVolumeHint = InArgs._VolumeHint;
	VolumeStrength = FMath::Clamp(InArgs._VolumeStrength, 0.f, 1.f);
	bSelfProjector = InArgs._SelfProjector;
	AnchorWorld = InArgs._AnchorWorld;
	WorldUnitScale = InArgs._WorldUnitScale > 0.f ? InArgs._WorldUnitScale : 1.f;
	bReverseWhenBackface = InArgs._ReverseWhenBackface;
	OnPositionChanged = InArgs._OnPositionChanged;
	SetVisibility(EVisibility::Visible);
}

FVector SImSlate3DArrow::EffectiveRenderAxis(const FImProjector& Proj, const FVector& AnchorW) const
{
	// When the axis points toward the camera (facing > 0), draw/hit along -WorldAxis so we render the visible
	// near half. Sign-agnostic for drag (ComputeAxisT uses the raw WorldAxis line). Disabled → raw WorldAxis.
	if (!bReverseWhenBackface) { return WorldAxis; }
	const double Facing = FVector::DotProduct(WorldAxis, (Proj.GetCamPos() - AnchorW).GetSafeNormal());
	return (Facing > 0.0) ? -WorldAxis : WorldAxis;
}

// Standalone callers update the anchored world point each frame (it may also have been moved by code
// between frames). No-op while dragging (the drag owns the position).
void SImSlate3DArrow::SetAnchorWorld(const FVector& InAnchorWorld)
{
	if (!bDragging) { AnchorWorld = InAnchorWorld; }
}

SImSlate3DArrow::~SImSlate3DArrow()
{
	FImSlate3DHitManager::Get().Unregister(this);
}

bool SImSlate3DArrow::GetProjector(const FImProjector*& OutProj, TSharedPtr<SImSlate3DTransformBox>& OutTarget) const
{
	OutTarget = Target.Pin();
	if (OutTarget.IsValid())
	{
		OutProj = &OutTarget->GetCachedProjector();
		return OutTarget->HasValidProjection();
	}
	// Standalone: self-build a projector from the scene camera. A tiny sheet anchored at AnchorWorld is
	// enough to give the projector a camera + view rect; the arrow geometry is built in world space from
	// AnchorWorld directly (GetWorldSegment / BuildArrowScreenShape), so the sheet size/placement only
	// needs to be valid, not meaningful. Billboard placement keeps it always camera-facing.
	if (bSelfProjector)
	{
		FImWorldPlacement P;
		P.WorldLocation = AnchorWorld;
		P.WorldScale = WorldUnitScale;
		// Default Opacity=1 (never culled) — a gizmo is always shown; no backface field needed.
		bSelfProjectorValid = ImBuildProjector(FVector2f(1.f, 1.f), P, SelfProjector);
		OutProj = &SelfProjector;
		return bSelfProjectorValid;
	}
	return false;
}

void SImSlate3DArrow::GetWorldSegment(const FImProjector& Proj, FVector& OutAnchorW, FVector& OutTipW) const
{
	TSharedPtr<SImSlate3DTransformBox> T = Target.Pin();
	float WScale;
	if (T.IsValid())
	{
		OutAnchorW = Proj.LocalToWorld(T->GetGizmoAnchorLocal());
		WScale = FMath::Max(T->GetWorldScale(), UE_SMALL_NUMBER);
	}
	else
	{
		OutAnchorW = AnchorWorld;  // standalone: pivot directly about the caller's world point
		// WorldUnitScale == 0 → screen-constant size: world units per arrow px = projector's per-screen-px
		// at the anchor distance (the arrow stays a constant on-screen size regardless of distance).
		WScale = (WorldUnitScale == 0.f) ? (float)Proj.WorldUnitsPerScreenPx(OutAnchorW)
		                                 : FMath::Max(WorldUnitScale, UE_SMALL_NUMBER);
	}
	// Tip along the effective render axis (flips when back-facing, if enabled) so the Volumetric3D path matches
	// the Flat path's direction. Drag (ComputeAxisT) uses the raw WorldAxis separately.
	OutTipW = OutAnchorW + EffectiveRenderAxis(Proj, OutAnchorW) * ((double)(ShaftLength + HeadLength) * WScale);
}

bool SImSlate3DArrow::BuildArrowScreenShape(const FImProjector& Proj, TArray<FVector2f>& OutPoly,
	FVector* OutShaftBaseW, FVector* OutShaftEndW) const
{
	TSharedPtr<SImSlate3DTransformBox> T = Target.Pin();
	if (!T.IsValid() && !bSelfProjector) { return false; }

	// ImSlate-3D base principle: a 2D arrow drawn on a WORLD plane, then ProjectWorld → screen (perspective +
	// equal width come for free from projecting a fixed 2D figure). ALL THREE axes use the SAME rule (no
	// dependence on the main UI sheet): the arrow's plane = span(A, V) where A = the axis (length dir) and
	// V = the in-plane ⊥A direction whose plane normal faces the camera most (billboard-around-axis). That is
	// V = normalize(A × ViewDir): it's ⊥A and lies in the screen plane, so the arrow face is as edge-on to the
	// view ray as possible → maximally visible & never collapses, for every axis incl. the camera-facing one.
	const FVector AnchorW = T.IsValid() ? Proj.LocalToWorld(T->GetGizmoAnchorLocal()) : AnchorWorld;
	const double  WScale  = T.IsValid() ? FMath::Max((double)T->GetWorldScale(), (double)UE_SMALL_NUMBER)
	                      : (WorldUnitScale == 0.f ? Proj.WorldUnitsPerScreenPx(AnchorW)
	                                               : FMath::Max((double)WorldUnitScale, (double)UE_SMALL_NUMBER));
	// Position side = the effective render axis (flips to -WorldAxis when back-facing, so the arrow is drawn on the
	// camera-near half and stays visible). But the ARROWHEAD must ALWAYS point along +WorldAxis (the true positive
	// direction). When flipped, that means the head sits at the anchor (centre) end pointing back toward +WorldAxis,
	// and the shaft extends outward — i.e. head and tail swap, the head still indicates the positive axis.
	const FVector PosDir = EffectiveRenderAxis(Proj, AnchorW);          // which half to draw on (visibility)
	const bool bFlipped  = FVector::DotProduct(PosDir, WorldAxis) < 0.0; // drawn on the -WorldAxis half

	const FVector ViewDir = Proj.GetCamPos() - AnchorW;
	FVector V = FVector::CrossProduct(PosDir, ViewDir);  // ⊥axis, in screen plane (billboard-around-axis width dir)
	if (V.SizeSquared() < 1e-6 * FMath::Max(ViewDir.SizeSquared(), 1.0))
	{
		V = FVector::CrossProduct(PosDir, FVector::UpVector);
		if (V.IsNearlyZero()) { V = FVector::CrossProduct(PosDir, FVector::ForwardVector); }
	}
	V = V.GetSafeNormal();
	if (V.IsNearlyZero()) { return false; }

	const double ShaftLenW  = (double)ShaftLength * WScale;
	const double TipLenW    = (double)(ShaftLength + HeadLength) * WScale;
	const double ShaftHalfW = (double)ShaftWidth * 0.5 * WScale;
	const double HeadHalfW  = (double)HeadWidth  * 0.5 * WScale;
	// `along` is measured on the POSITION side (PosDir). Not flipped: head at the far end (tip points +WorldAxis,
	// which == PosDir). Flipped: head at the NEAR/centre end (along 0..HeadLen) pointing toward +WorldAxis (= -PosDir),
	// shaft from the head outward to TipLenW. Both keep the arrowhead indicating +WorldAxis.
	auto W = [&](double along, double across) -> FVector { return AnchorW + PosDir * along + V * across; };
	FVector Wpts[7];
	if (!bFlipped)
	{
		Wpts[0] = W(0.0,       +ShaftHalfW);  // shaft base L
		Wpts[1] = W(ShaftLenW, +ShaftHalfW);  // shaft top L
		Wpts[2] = W(ShaftLenW, -ShaftHalfW);  // shaft top R
		Wpts[3] = W(0.0,       -ShaftHalfW);  // shaft base R
		Wpts[4] = W(ShaftLenW, +HeadHalfW);   // head base L
		Wpts[5] = W(TipLenW,    0.0);         // tip (points +WorldAxis == +PosDir, far end)
		Wpts[6] = W(ShaftLenW, -HeadHalfW);   // head base R
	}
	else
	{
		// Mirror: head at the centre end pointing toward +WorldAxis (= toward the anchor, i.e. −PosDir).
		const double HeadLenW = (double)HeadLength * WScale;
		Wpts[0] = W(TipLenW,   +ShaftHalfW);  // shaft base L (far/outer end)
		Wpts[1] = W(HeadLenW,  +ShaftHalfW);  // shaft top L (toward head)
		Wpts[2] = W(HeadLenW,  -ShaftHalfW);  // shaft top R
		Wpts[3] = W(TipLenW,   -ShaftHalfW);  // shaft base R
		Wpts[4] = W(HeadLenW,  +HeadHalfW);   // head base L
		Wpts[5] = W(0.0,        0.0);         // tip at the anchor (points −PosDir == +WorldAxis)
		Wpts[6] = W(HeadLenW,  -HeadHalfW);   // head base R
	}
	// Shaft centre-line end points (world), accounting for flip — the volume caps reuse these (single source).
	if (OutShaftBaseW || OutShaftEndW)
	{
		const double HeadLenW2 = (double)HeadLength * WScale;
		const double BaseAlong = bFlipped ? TipLenW  : 0.0;        // anchor/tail end of the shaft
		const double EndAlong  = bFlipped ? HeadLenW2 : ShaftLenW;  // shaft/head junction
		if (OutShaftBaseW) { *OutShaftBaseW = W(BaseAlong, 0.0); }
		if (OutShaftEndW)  { *OutShaftEndW  = W(EndAlong,  0.0); }
	}
	OutPoly.Reset();
	OutPoly.AddUninitialized(7);
	for (int32 i = 0; i < 7; ++i)
	{
		const TOptional<FVector2f> S = Proj.ProjectWorld(Wpts[i]);
		if (!S) { return false; }
		OutPoly[i] = *S;
	}
	return true;
}

FVector2f SImSlate3DArrow::ScreenToViewportPx(const FVector2f& ScreenSpacePos) const
{
	// Desktop px → viewport physical px. Use the TARGET PANEL's mapping (its geometry), NOT our own widget
	// geometry — they may have different logical↔physical ratios, which would add a per-distance scale drift
	// (the "drags further → off more" symptom). The panel's chain is the one the projector agrees with.
	TSharedPtr<SImSlate3DTransformBox> T = Target.Pin();
	if (T.IsValid())
	{
		return T->MapDesktopCursorToViewportPx(ScreenSpacePos);
	}
	return ScreenSpacePos;
}

bool SImSlate3DArrow::HitTestArrowShapeScreen(const FVector2f& ScreenSpacePos) const
{
	return HitTestArrowShapeVP(ScreenToViewportPx(ScreenSpacePos));
}

bool SImSlate3DArrow::HitTestArrowShapeVP(const FVector2f& CursorVP) const
{
	const FImProjector* Proj = nullptr;
	TSharedPtr<SImSlate3DTransformBox> T;
	if (!GetProjector(Proj, T)) { return false; }
	TArray<FVector2f> P;
	if (!BuildArrowScreenShape(*Proj, P) || P.Num() != 7) { return false; }
	// CursorVP already in viewport px (computed by the gateway/box via MapScreenToViewportPx).
	if (PointInTri(CursorVP, P[0], P[1], P[2]) || PointInTri(CursorVP, P[0], P[2], P[3])) { return true; }
	if (PointInTri(CursorVP, P[4], P[5], P[6])) { return true; }
	return false;
}

int32 SImSlate3DArrow::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FImProjector* Proj = nullptr;
	TSharedPtr<SImSlate3DTransformBox> T;
	if (!GetProjector(Proj, T)) { return LayerId; }

	FLinearColor LC = Color;
	if (FImSlate3DHitManager::Get().IsWidgetHovered(this) || bDragging)
	{
		LC = FMath::Lerp(LC, FLinearColor(1.f, 1.f, 1.f, LC.A), 0.45f);
	}
	const FVector4f Col(LC.R, LC.G, LC.B, LC.A);

	TArray<FIm3DQuad> Quads;

	if (GetIm3DArrowStyle() == EImArrowStyle::Volumetric3D)
	{
		FVector AnchorW, TipW;
		GetWorldSegment(*Proj, AnchorW, TipW);
		const FVector Axis = EffectiveRenderAxis(*Proj, AnchorW);  // back-facing flip (render); drag uses raw axis
		const float WScale = T.IsValid() ? FMath::Max(T->GetWorldScale(), UE_SMALL_NUMBER)
		                   : (WorldUnitScale == 0.f ? (float)Proj->WorldUnitsPerScreenPx(AnchorW)
		                                            : FMath::Max(WorldUnitScale, UE_SMALL_NUMBER));
		FVector U = FVector::CrossProduct(Axis, FVector::UpVector);
		if (U.IsNearlyZero()) { U = FVector::CrossProduct(Axis, FVector::ForwardVector); }
		U = U.GetSafeNormal();
		const FVector V = FVector::CrossProduct(Axis, U).GetSafeNormal();
		const double ShaftRadW = (double)ShaftWidth * 0.5 * WScale;
		const double HeadRadW  = (double)HeadWidth * 0.5 * WScale;
		const FVector ShaftEndW = AnchorW + Axis * ((double)ShaftLength * WScale);
		const int32 NSide = 8;
		const float EdgePx = 2.f;
		auto Ring = [&](const FVector& C, double R, int32 k)
		{
			const double Ang = 2.0 * PI * (double)k / (double)NSide;
			return C + (U * FMath::Cos(Ang) + V * FMath::Sin(Ang)) * R;
		};
		for (int32 k = 0; k < NSide; ++k)
		{
			const FVector A0 = Ring(AnchorW, ShaftRadW, k), A1 = Ring(AnchorW, ShaftRadW, (k + 1) % NSide);
			const FVector S0 = Ring(ShaftEndW, ShaftRadW, k), H0 = Ring(ShaftEndW, HeadRadW, k);
			const FVector H1 = Ring(ShaftEndW, HeadRadW, (k + 1) % NSide);
			AppendWorldSegmentQuad(*Proj, A0, A1, EdgePx, Col, Quads);
			AppendWorldSegmentQuad(*Proj, A0, S0, EdgePx, Col, Quads);
			AppendWorldSegmentQuad(*Proj, S0, H0, EdgePx, Col, Quads);
			AppendWorldSegmentQuad(*Proj, H0, H1, EdgePx, Col, Quads);
			AppendWorldSegmentQuad(*Proj, H0, TipW, EdgePx, Col, Quads);
		}
	}
	else
	{
		// Volume hint: draw the cylinder-base + cone-base WORLD discs FIRST (projected to ellipses); the flat
		// shaft/head drawn after covers their inner half → only the camera-facing half-ellipse shows → 3D look.
		// Ellipse flatness is driven for free by the axis tilt to the view (side-on axis → flat, no hint).
		// Pseudo-3D end-caps: per-arrow VolumeHint flag AND a global CVar must both allow it; VolumeStrength
		// (0..1) scales BOTH the axial bulge and the cap opacity (0 = no hint, 1 = full).
		if (bVolumeHint && VolumeStrength > 0.f && CVarImSlate3DArrowVolumeHint.GetValueOnGameThread() != 0)
		{
			const FVector AnchorW   = T.IsValid() ? Proj->LocalToWorld(T->GetGizmoAnchorLocal()) : AnchorWorld;
			const double  WScale    = T.IsValid() ? FMath::Max((double)T->GetWorldScale(), (double)UE_SMALL_NUMBER)
			                        : (WorldUnitScale == 0.f ? Proj->WorldUnitsPerScreenPx(AnchorW)
			                                                 : FMath::Max((double)WorldUnitScale, (double)UE_SMALL_NUMBER));
			// End-cap CENTRES come straight from the arrow body (BuildArrowScreenShape OutShaftBase/EndW): one place
			// computes flip + scale + axis, so the caps can never drift from the drawn shaft (the "disc on the wrong
			// part of the shaft" bug from duplicated position logic). A = render axis for the ellipse bulge dir only.
			const FVector A = EffectiveRenderAxis(*Proj, AnchorW);
			TArray<FVector2f> PBody; FVector ShaftBaseW = AnchorW, ShaftEndW = AnchorW;
			BuildArrowScreenShape(*Proj, PBody, &ShaftBaseW, &ShaftEndW);
			// End-cap ellipses live ON the billboard plane span(A, BWidth) (same as the arrow → tracks camera).
			// BWidth = the shaft width dir = normalize(A × ViewDir). The cap bulges along the AXIS by RadiusW ×
			// tilt × VolumeStrength, tilt = |A·viewUnit| (axis toward camera → cap faces us → bulge; side-on → flat).
			const FVector ViewDir = (Proj->GetCamPos() - AnchorW);
			const FVector ViewUnit = ViewDir.GetSafeNormal();
			FVector BWidth = FVector::CrossProduct(A, ViewDir);
			if (BWidth.IsNearlyZero()) { BWidth = FVector::CrossProduct(A, FVector::UpVector); }
			BWidth = BWidth.GetSafeNormal();
			const double Tilt = FMath::Abs(FVector::DotProduct(A, ViewUnit)) * (double)VolumeStrength;  // 0=side-on/off .. 1=facing
			// Full solid cylinder+cone, painter's algorithm along the axis (far→near). Pieces:
			//   tail  @AnchorW   r=ShaftR  normal −A   (cylinder bottom)
			//   shaft (BuildArrowScreenShape P[0..3])
			//   cylTop@ShaftEndW r=ShaftR  normal +A   (cylinder top — only the rim peeking past the cone shows)
			//   coneBase@ShaftEndW r=HeadR normal −A   (cone bottom annulus, HeadR>ShaftR)
			//   head  (BuildArrowScreenShape P[4..6])
			// bTipToCam = the +A (tip) end is toward the camera. Draw order is the depth order along the axis:
			//   tip-to-cam  : tail → shaft → cylTop → coneBase → head        (anchor far, tip near)
			//   tip-from-cam: head → coneBase → cylTop → shaft → tail        (tip far, anchor near)
			// A cap whose OUTWARD normal faces the camera shows its DISC (DARK end-cap shadow); else SAME color.
			const bool bTipToCam = FVector::DotProduct(A, ViewUnit) > 0.0;
			const FVector4f Dark(LC.R * 0.6f, LC.G * 0.6f, LC.B * 0.6f, LC.A * VolumeStrength);
			const FVector4f Same(Col.X, Col.Y, Col.Z, Col.W * VolumeStrength);
			// tail normal −A: faces camera ⇔ !bTipToCam. cone-base normal −A: same. cyl-top normal +A: faces ⇔ bTipToCam.
			const FVector4f TailCol = !bTipToCam ? Dark : Same;
			const FVector4f ConeBaseCol = !bTipToCam ? Dark : Same;
			const FVector4f CylTopCol = bTipToCam ? Dark : Same;
#if !UE_BUILD_SHIPPING
			if (IsImSlate3DDebugVerbose())
			{
				static int32 VN = 0;
				if (VN++ % 30 == 0)
					UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][volHint] axis=%s Strength=%.2f Tilt=%.3f tipToCam=%d"),
						*WorldAxis.ToString(), VolumeStrength, Tilt, bTipToCam ? 1 : 0);
			}
#endif
			const double ShaftR = (double)ShaftWidth * 0.5 * WScale;
			const double HeadR  = (double)HeadWidth  * 0.5 * WScale;
			TArray<FVector2f> P;
			const bool bShape = BuildArrowScreenShape(*Proj, P) && P.Num() == 7;
			auto Tail     = [&]{ AppendEndCapEllipse(*Proj, ShaftBaseW, ShaftR, BWidth, A, Tilt, TailCol, Quads); };
			auto CylTop   = [&]{ AppendEndCapEllipse(*Proj, ShaftEndW,  ShaftR, BWidth, A, Tilt, CylTopCol, Quads); };
			auto ConeBase = [&]{ AppendEndCapEllipse(*Proj, ShaftEndW,  HeadR,  BWidth, A, Tilt, ConeBaseCol, Quads); };
			auto Shaft    = [&]{ if (bShape) AppendScreenQuad(P[0], P[1], P[2], P[3], Col, Quads); };
			auto Head     = [&]{ if (bShape) AppendScreenQuad(P[4], P[5], P[5], P[6], Col, Quads); };
			if (bTipToCam) { Tail(); Shaft(); CylTop(); ConeBase(); Head(); }   // anchor far → tip near
			else           { Head(); ConeBase(); CylTop(); Shaft(); Tail(); }   // tip far → anchor near
		}
		else
		{
			TArray<FVector2f> P;
			if (BuildArrowScreenShape(*Proj, P) && P.Num() == 7)
			{
				AppendScreenQuad(P[0], P[1], P[2], P[3], Col, Quads);   // shaft rect
				AppendScreenQuad(P[4], P[5], P[5], P[6], Col, Quads);   // head triangle (tip doubled)
			}
		}
	}

	if (Quads.Num() > 0)
	{
		KeepAliveShaderElement = MakeShared<FIm3DShaderElement, ESPMode::ThreadSafe>(MoveTemp(Quads), Proj->GetViewRect());
		FSlateDrawElement::MakeCustom(OutDrawElements, LayerId, KeepAliveShaderElement);
	}
	else
	{
		KeepAliveShaderElement.Reset();
	}
	return LayerId + 1;
}

// ---- Raw events: forward to the manager, which walks all units in overlap order ----

FReply SImSlate3DArrow::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2f VP = ScreenToViewportPx((FVector2f)MouseEvent.GetScreenSpacePosition());
	return FImSlate3DHitManager::Get().HandleEvent(MouseEvent, VP,
		[&MouseEvent](IImSlate3DHittable& H, FVector2f InVP) { return H.OnHitMouseButtonDown(MouseEvent, InVP); });
}

FReply SImSlate3DArrow::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2f VP = ScreenToViewportPx((FVector2f)MouseEvent.GetScreenSpacePosition());
	return FImSlate3DHitManager::Get().HandleEvent(MouseEvent, VP,
		[&MouseEvent](IImSlate3DHittable& H, FVector2f InVP) { return H.OnHitMouseButtonUp(MouseEvent, InVP); });
}

FReply SImSlate3DArrow::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2f VP = ScreenToViewportPx((FVector2f)MouseEvent.GetScreenSpacePosition());
	return FImSlate3DHitManager::Get().HandleEvent(MouseEvent, VP,
		[&MouseEvent](IImSlate3DHittable& H, FVector2f InVP) { return H.OnHitMouseMove(MouseEvent, InVP); });
}

void SImSlate3DArrow::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	// Hover is the manager's (GlobalHoveredPath); nothing to clear here. The manager recomputes on the move
	// that took the cursor off us. (No self hover flag anymore.)
	SLeafWidget::OnMouseLeave(MouseEvent);
}

void SImSlate3DArrow::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	if (bDragging) { EndDrag(); }
	SLeafWidget::OnMouseCaptureLost(CaptureLostEvent);
}

FCursorReply SImSlate3DArrow::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	if (bDragging && FSlateApplication::IsInitialized() && FSlateApplication::Get().IsUsingHighPrecisionMouseMovment())
	{
		return FCursorReply::Cursor(EMouseCursor::None);
	}
	return FCursorReply::Unhandled();
}

// ---- IImSlate3DHittable ----

FBox2D SImSlate3DArrow::GetScreenBoundsVP() const
{
	const FImProjector* Proj = nullptr;
	TSharedPtr<SImSlate3DTransformBox> T;
	if (!GetProjector(Proj, T)) { return FBox2D(ForceInit); }  // invalid → manager skips broad phase
	TArray<FVector2f> P;
	if (!BuildArrowScreenShape(*Proj, P) || P.Num() != 7) { return FBox2D(ForceInit); }
	FBox2D Box(ForceInit);
	for (const FVector2f& Pt : P) { Box += FVector2D(Pt.X, Pt.Y); }
	Box = Box.ExpandBy(2.0);  // small margin so edge pixels still pass to the per-pixel test
	return Box;
}

bool SImSlate3DArrow::IsPixelHovered(const FPointerEvent& Event, FVector2f CursorVP) const
{
	return HitTestArrowShapeVP(CursorVP);
}

void SImSlate3DArrow::HandleHitEvent(FReply& OutReply, const FPointerEvent& Event, FVector2f CursorVP,
	const TFunctionRef<FReply(IImSlate3DHittable&, FVector2f)>& Callback)
{
	// Click dispatch only — hover is owned by the manager (GlobalHoveredPath). If a higher unit already won,
	// bow out. Actively dragging keeps handling off-shape; otherwise route only when pixel-hit.
	if (OutReply.IsEventHandled())
	{
		return;
	}
	if (bDragging || IsPixelHovered(Event, CursorVP))
	{
		OutReply = Callback(*this, CursorVP);
	}
}

bool SImSlate3DArrow::BuildHoverPath(FVector2f CursorVP, TArray<FArrangedWidget>& OutPath) const
{
	// A-route arrow is a leaf unit with no sub-widgets — its hover path is just itself when pixel-hit.
	if (!HitTestArrowShapeVP(CursorVP)) { return false; }
	OutPath.Add(FArrangedWidget(ConstCastSharedRef<SWidget>(AsShared()), GetCachedGeometry()));
	return true;
}

FReply SImSlate3DArrow::OnHitMouseButtonDown(const FPointerEvent& Event, FVector2f CursorVP)
{
	BeginDrag(Event);
	FReply R = FReply::Handled().CaptureMouse(AsShared());
	// Mouse path: high precision pins+hides the OS cursor; we accumulate raw deltas onto a virtual cursor for
	// infinite scrub. Touch path: no high precision (use the real cursor). Both feed the ray-closest-point drag.
	if (bDragHighPrecision)
	{
		R.UseHighPrecisionMouseMovement(AsShared());
	}
	return R;
}

FReply SImSlate3DArrow::OnHitMouseMove(const FPointerEvent& Event, FVector2f CursorVP)
{
	// Only the owning pointer drives the drag (multi-touch: another finger must not perturb it).
	if (bDragging && (int32)Event.GetPointerIndex() == DragPointerIndex) { UpdateDrag(Event); }
	return FReply::Handled();
}

FReply SImSlate3DArrow::OnHitMouseButtonUp(const FPointerEvent& Event, FVector2f CursorVP)
{
	if (!bDragging || (int32)Event.GetPointerIndex() != DragPointerIndex) { return FReply::Unhandled(); }
	if (bDragHighPrecision && FSlateApplication::IsInitialized() && FSlateApplication::Get().IsUsingHighPrecisionMouseMovment())
	{
		FSlateApplication::Get().SetCursorPos(FVector2D(DragAnchorScreenPos.X, DragAnchorScreenPos.Y));
	}
	EndDrag();
	return FReply::Handled().ReleaseMouseCapture();
}

// ---- drag actions ----

// Ray–axis closest point: deproject the cursor to a world ray, find the closest point on the axis line
// (P0=DragStartWorldLoc, dir=WorldAxis), return its axis parameter t (world units along the axis from P0).
// Robust for ALL axes including ones nearly facing the camera — only fails when the ray is ~parallel to the
// axis (then there's genuinely no screen handle to drag). This replaces the screen-projection method whose
// pixels-per-world degenerates (→ runaway) for camera-facing axes.
// Screen-space projection method (Our Machinery "Gizmo Repair" / tinygizmo). Replaces the naive 3D
// ray–axis closest-point, whose t-sensitivity is 1/sin²θ → blows up as the axis faces the camera
// (the blue/Normal axis "shrinks / jumps" on repeated drags) and scales with perspective distance
// ("drag a little, moves a lot"). Three steps:
//   1. Project the world axis (origin DragStartWorldLoc + dir WorldAxis) to a 2D screen line.
//   2. Project the cursor onto that 2D line → mouse_on_axis (screen point ON the axis line).
//   3. Deproject mouse_on_axis back to a world ray and intersect with the world axis line. Because the
//      ray now starts on the screen-projected axis line, it passes right through the world axis → the
//      closest-point is well-conditioned (no grazing-angle blowup).
// Returns false (caller holds position) when the axis projects to a near-degenerate screen line (axis
// almost dead-on to the camera) — there is genuinely no usable screen handle then.
bool SImSlate3DArrow::ComputeAxisT(FVector2f CursorVP, double& OutT) const
{
	TSharedPtr<SImSlate3DTransformBox> T;
	const FImProjector* Proj = nullptr;
	// During a drag use the FROZEN projector (constant calibration); otherwise the live one.
	if (bDragging && bDragFrozenProjectorValid)
	{
		Proj = &DragFrozenProjector;
		T = Target.Pin();
		if (!T.IsValid() && !bSelfProjector) { return false; }
	}
	else if (!GetProjector(Proj, T))
	{
		return false;
	}

	const FVector A = WorldAxis;  // unit world axis
	// Step 1: world axis → screen line. Two world points along the axis straddling the origin.
	const float WScale = T.IsValid() ? FMath::Max(T->GetWorldScale(), UE_SMALL_NUMBER)
	                   : (WorldUnitScale == 0.f ? (float)Proj->WorldUnitsPerScreenPx(DragStartAnchorW)
	                                            : FMath::Max(WorldUnitScale, UE_SMALL_NUMBER));
	const double Span = FMath::Max((double)(ShaftLength + HeadLength) * WScale, 1.0);  // world span for a stable screen dir
	// Axis line passes through the ARROW ANCHOR (where the arrow is actually drawn), not the panel pivot.
	const TOptional<FVector2f> SA = Proj->ProjectWorld(DragStartAnchorW - A * Span);
	const TOptional<FVector2f> SB = Proj->ProjectWorld(DragStartAnchorW + A * Span);
	if (!SA.IsSet() || !SB.IsSet()) { return false; }  // axis endpoint behind camera
	FVector2f ScreenDir = *SB - *SA;
	const float ScreenLen = ScreenDir.Size();
	if (ScreenLen < 3.f) { return false; }  // axis ~dead-on to camera → no usable screen handle → hold
	ScreenDir /= ScreenLen;

	// Step 2: project the cursor onto the 2D screen axis line → signed pixel position along it from SA.
	const FVector2f RelCursor = CursorVP - *SA;
	const float ScreenT = FVector2f::DotProduct(RelCursor, ScreenDir);  // px from SA along the screen axis

	// Step 3: map the screen-pixel position DIRECTLY to a world axis parameter via the known endpoint
	// calibration: world span [-Span..+Span] (= 2*Span world units) maps to [SA..SB] (= ScreenLen px). So
	//   worldPerPx = 2*Span / ScreenLen,   t = ScreenT * worldPerPx - Span  (SA corresponds to t = -Span).
	// This is the robust form: it NEVER divides by 1-cos²θ, so it does NOT blow up when the world axis points
	// near the camera (the earlier ray∩axis step still had 1/Denom → curT exploded to -25000 on the axis that
	// happened to face the camera). The screen line is the only thing we trust; ScreenLen<3px is already
	// rejected above. Linear in screen px ⇒ exact grab-point follow along the axis (perspective foreshortening
	// across the span is sub-pixel for a gizmo-sized handle).
	const double WorldPerPx = (2.0 * Span) / (double)ScreenLen;
	OutT = (double)ScreenT * WorldPerPx - Span;  // world units along A from DragStartWorldLoc
#if !UE_BUILD_SHIPPING
	// REPLAY [axisStep]: the screen-projection intermediates, so the axis math can be recomputed offline from
	// the [dragConst] matrix/basis and cross-checked against this engine output. SA/SB are the projected axis
	// endpoints (frozen), ScreenT the cursor's signed px along the screen axis, OutT the resulting world param.
	if (IsImSlate3DDebugVerbose())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][axisStep] axis=%s cursorVP=(%.2f,%.2f) SA=(%.2f,%.2f) SB=(%.2f,%.2f) screenLen=%.2f screenT=%.2f worldPerPx=%.4f OutT=%.3f"),
			*WorldAxis.ToString(), CursorVP.X, CursorVP.Y, SA->X, SA->Y, SB->X, SB->Y, ScreenLen, ScreenT, WorldPerPx, OutT);
	}
#endif
	return true;
}

// Desktop-px raw cursor delta → viewport-px delta, via the TARGET PANEL's mapping (consistent space). The
// viewport-px offset (ViewRect.Min) cancels on a delta, leaving the pure logical→physical scale.
FVector2f SImSlate3DArrow::RawDeltaToViewportPx(const FPointerEvent& Event) const
{
	TSharedPtr<SImSlate3DTransformBox> T = Target.Pin();
	if (!T.IsValid()) { return (FVector2f)Event.GetCursorDelta(); }
	const FVector2f ScreenPos = (FVector2f)Event.GetScreenSpacePosition();
	const FVector2f AbsDelta = (FVector2f)Event.GetCursorDelta();
	return T->MapDesktopCursorToViewportPx(ScreenPos) - T->MapDesktopCursorToViewportPx(ScreenPos - AbsDelta);
}

void SImSlate3DArrow::BeginDrag(const FPointerEvent& Event)
{
	TSharedPtr<SImSlate3DTransformBox> T = Target.Pin();
	if (!T.IsValid() && !bSelfProjector) { return; }
	bDragging = true;
	DragPointerIndex = (int32)Event.GetPointerIndex();  // which pointer/finger owns this drag (multi-touch)
	DragStartWorldLoc = T.IsValid() ? T->GetWorldLocation() : AnchorWorld;  // standalone: drag the caller's point
	DragAnchorScreenPos = (FVector2f)Event.GetScreenSpacePosition();
	// Freeze the projector for the whole drag: UpdatePlacementLocation moves the panel each frame, which would
	// otherwise rebuild the live projector and shift the screen-axis calibration mid-drag (step jumps). With a
	// frozen projector the px→world mapping is constant → step tracks ONLY the cursor.
	{
		const FImProjector* LiveProj = nullptr;
		TSharedPtr<SImSlate3DTransformBox> PT;
		if (GetProjector(LiveProj, PT) && LiveProj)
		{
			DragFrozenProjector = *LiveProj;
			bDragFrozenProjectorValid = true;
			// Arrow anchor world pos at mouse-down = the axis-line origin (NOT panel WorldLocation/pivot).
			// Standalone: the anchor IS the caller's world point.
			DragStartAnchorW = T.IsValid() ? DragFrozenProjector.LocalToWorld(T->GetGizmoAnchorLocal()) : AnchorWorld;
		}
		else
		{
			bDragFrozenProjectorValid = false;
			DragStartAnchorW = DragStartWorldLoc;  // fallback
		}
	}
#if !UE_BUILD_SHIPPING
	// REPLAY [dragConst]: everything constant over this drag, enough to recompute the entire screen↔world chain
	// offline. ViewProjMtx (row-major 4x4) + ViewRect + plane basis (Origin/Right/Down/Normal) + axis/Span +
	// the panel geometry's S1→S3 scale (DPI) & ViewRect.Min via a probe: MapDesktopCursorToViewportPx of the
	// down cursor (so the desktop→viewport transform is reconstructible from this one sample pair).
	if (IsImSlate3DDebugVerbose() && bDragFrozenProjectorValid && T.IsValid())  // panel-specific replay dump
	{
		const FImProjector& P = DragFrozenProjector;
		const FMatrix& M = P.GetViewProjMatrix();
		const FIntRect VR = P.GetViewRect();
		const float WScale = FMath::Max(T->GetWorldScale(), UE_SMALL_NUMBER);
		const double Span = FMath::Max((double)(ShaftLength + HeadLength) * WScale, 1.0);
		const FVector2f DownScreen = (FVector2f)Event.GetScreenSpacePosition();
		const FVector2f DownVP = T->MapDesktopCursorToViewportPx(DownScreen);
		UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][dragConst] axis=%s startWorld=(%.3f,%.3f,%.3f) Span=%.3f WScale=%.4f VR=(%d,%d,%d,%d) downScreen=(%.2f,%.2f) downVP=(%.2f,%.2f)"),
			*WorldAxis.ToString(), DragStartWorldLoc.X, DragStartWorldLoc.Y, DragStartWorldLoc.Z, Span, WScale,
			VR.Min.X, VR.Min.Y, VR.Max.X, VR.Max.Y, DownScreen.X, DownScreen.Y, DownVP.X, DownVP.Y);
		UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][dragConst] basis Origin=(%.3f,%.3f,%.3f) Right=(%.3f,%.3f,%.3f) Down=(%.3f,%.3f,%.3f) Normal=(%.3f,%.3f,%.3f)"),
			P.GetPlaneOrigin().X, P.GetPlaneOrigin().Y, P.GetPlaneOrigin().Z,
			P.GetPlaneRight().X, P.GetPlaneRight().Y, P.GetPlaneRight().Z,
			P.GetPlaneDown().X, P.GetPlaneDown().Y, P.GetPlaneDown().Z,
			P.GetPlaneNormal().X, P.GetPlaneNormal().Y, P.GetPlaneNormal().Z);
		UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][dragConst] VP M[0]=(%.6f,%.6f,%.6f,%.6f) M[1]=(%.6f,%.6f,%.6f,%.6f)"),
			M.M[0][0], M.M[0][1], M.M[0][2], M.M[0][3], M.M[1][0], M.M[1][1], M.M[1][2], M.M[1][3]);
		UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][dragConst] VP M[2]=(%.6f,%.6f,%.6f,%.6f) M[3]=(%.6f,%.6f,%.6f,%.6f)"),
			M.M[2][0], M.M[2][1], M.M[2][2], M.M[2][3], M.M[3][0], M.M[3][1], M.M[3][2], M.M[3][3]);
	}
#endif

	// Two independent input paths:
	//   • Mouse  (real desktop mouse): high-precision + hidden cursor + INFINITE scrub. We accumulate raw
	//     deltas onto a virtual cursor (can leave the screen), deproject THAT to the drag ray.
	//   • Touch / fake-touch: no high precision (touch has no raw delta); use the REAL absolute cursor.
	const FImSlate3DHost& Host = FImSlate3DHost::Get();
	const bool bFakeTouch = Host.IsMouseForTouch ? Host.IsMouseForTouch() : false;
	bDragHighPrecision = !bFakeTouch;  // mouse path = high precision; touch path = absolute

	VirtualCursorVP = ScreenToViewportPx((FVector2f)Event.GetScreenSpacePosition());
	// Defer the startT anchor to the FIRST UpdateDrag frame (fake-touch jumps down→first-move; anchoring here
	// makes frame-1 step jump). Mark valid so UpdateDrag runs; it captures startT on its first call.
	bDragStartAxisTValid = true;
	bDragStartTPending = true;
	DragStartAxisT = 0.0;
	LastLoggedStep = 0.0;
	if (IsImSlate3DDebug())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][arrow] drag begin axis=%s path=%s (startT deferred to frame 1)"),
			*WorldAxis.ToString(), bDragHighPrecision ? TEXT("mouse/hiprec") : TEXT("touch/absolute"));
	}
}

void SImSlate3DArrow::UpdateDrag(const FPointerEvent& Event)
{
	TSharedPtr<SImSlate3DTransformBox> T = Target.Pin();
	if ((!T.IsValid() && !bSelfProjector) || !bDragStartAxisTValid) { return; }

	FVector2f CursorVP;
	if (bDragHighPrecision)
	{
		// Mouse path: high precision freezes the OS cursor, so accumulate the raw delta onto our virtual
		// cursor — it can run past the screen edge → infinite scrub.
		VirtualCursorVP += RawDeltaToViewportPx(Event);
		CursorVP = VirtualCursorVP;
	}
	else
	{
		// Touch path: use the real absolute cursor position.
		CursorVP = ScreenToViewportPx((FVector2f)Event.GetScreenSpacePosition());
	}

	double CurT;
	if (!ComputeAxisT(CursorVP, CurT)) { return; }   // no usable screen handle this frame → hold position
	// First valid frame defines the grab anchor: startT = this frame's t → step starts at 0, grab point stays
	// under the cursor (avoids the fake-touch down→first-move jump).
	if (bDragStartTPending)
	{
		DragStartAxisT = CurT;
		bDragStartTPending = false;
	}
	const double StepDelta = CurT - DragStartAxisT;  // world units moved along the axis since the grab anchor
	const FVector NewWorldLoc = DragStartWorldLoc + WorldAxis * StepDelta;
	if (T.IsValid())
	{
		T->UpdatePlacementLocation(NewWorldLoc);  // embedded: move the panel
	}
	else
	{
		AnchorWorld = NewWorldLoc;                 // standalone: move our own anchor + notify the caller
		OnPositionChanged.ExecuteIfBound(NewWorldLoc);
	}
#if !UE_BUILD_SHIPPING
	// REPLAY [dragFrame]: one line per move event, enough to recompute the whole chain offline against the
	// [dragConst] block. Fields:
	//   screen = raw desktop cursor (S1)           — chain input
	//   vp     = CursorVP (S3) fed to ComputeAxisT — verify S1→S3 (vs dragConst down sample)
	//   curT/startT/step                            — verify the screen-projection axis math
	//   grabReproj = ProjectWorld(NewWorldLoc + the world grab point) back to screen — THE follow test:
	//     the grab point is the world point on the axis we should keep under the cursor. We reproject the
	//     anchor world point AFTER moving; grabReproj should equal `vp` if it tracks. Δ = grabReproj - vp.
	if (IsImSlate3DDebugVerbose())
	{
		const FVector2f Screen = (FVector2f)Event.GetScreenSpacePosition();
		// Grab world point = where startT sits on the (now-moved) axis. After the move, the point at axis param
		// startT relative to DragStartWorldLoc has world pos DragStartWorldLoc + A*startT, and the panel moved by
		// A*step, so the grabbed material point is now at DragStartWorldLoc + A*startT + A*step = + A*CurT.
		const FVector GrabWorldNow = DragStartAnchorW + WorldAxis * CurT;
		FVector2f GrabReproj(0, 0); bool bGRValid = false;
		const FImProjector* RP = (bDragging && bDragFrozenProjectorValid) ? &DragFrozenProjector : nullptr;
		if (RP) { const TOptional<FVector2f> S = RP->ProjectWorld(GrabWorldNow); if (S.IsSet()) { GrabReproj = *S; bGRValid = true; } }
		UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][dragFrame] axis=%s path=%s screen=(%.2f,%.2f) vp=(%.2f,%.2f) curT=%.3f startT=%.3f step=%.3f newWorld=(%.2f,%.2f,%.2f) grabReproj=%s(%.2f,%.2f) dGrab=(%.2f,%.2f)"),
			*WorldAxis.ToString(), bDragHighPrecision ? TEXT("M") : TEXT("T"),
			Screen.X, Screen.Y, CursorVP.X, CursorVP.Y, CurT, DragStartAxisT, StepDelta,
			NewWorldLoc.X, NewWorldLoc.Y, NewWorldLoc.Z,
			bGRValid ? TEXT("") : TEXT("INV"), GrabReproj.X, GrabReproj.Y,
			bGRValid ? (GrabReproj.X - CursorVP.X) : 0.f, bGRValid ? (GrabReproj.Y - CursorVP.Y) : 0.f);
	}
#endif
}

void SImSlate3DArrow::EndDrag()
{
	bDragging = false;
	DragPointerIndex = INDEX_NONE;
	bDragFrozenProjectorValid = false;  // release the frozen projector; resume using the live one
}

// ---------------- SImSlate3DArrowHitClip: per-arrow AABB hit-clip wrapper ----------------

void SImSlate3DArrowHitClip::Construct(const FArguments& InArgs)
{
	Arrow = InArgs._Arrow;
	SetVisibility(EVisibility::SelfHitTestInvisible);  // wrapper itself never grabs events; only pushes the clip
	if (Arrow.IsValid())
	{
		ChildSlot[ Arrow.ToSharedRef() ];
	}
}

int32 SImSlate3DArrowHitClip::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	bool bPushed = false;
	if (Arrow.IsValid())
	{
		const FBox2D BoundsVP = Arrow->GetScreenBoundsForClip();  // viewport physical px
		const TSharedPtr<SImSlate3DTransformBox> T = Arrow->GetTarget();
		if (BoundsVP.bIsValid && T.IsValid() && T->HasValidProjection())
		{
			const FVector2f LocalSize = (FVector2f)AllottedGeometry.GetLocalSize();
			const FIntRect VR = T->GetCachedProjector().GetViewRect();
			if (VR.Width() > 0 && VR.Height() > 0 && LocalSize.X > 0.f && LocalSize.Y > 0.f)
			{
				const FVector2f S(LocalSize.X / (float)VR.Width(), LocalSize.Y / (float)VR.Height());
				const FVector2f Min((float)VR.Min.X, (float)VR.Min.Y);
				// viewport physical px → this widget's LOCAL → ABSOLUTE/window space (PushClip stores verbatim).
				auto ToWindow = [&](FVector2f P)
				{
					const FVector2f Local((P.X - Min.X) * S.X, (P.Y - Min.Y) * S.Y);
					return (FVector2f)AllottedGeometry.LocalToAbsolute(Local);
				};
				const FVector2f TL((float)BoundsVP.Min.X, (float)BoundsVP.Min.Y);
				const FVector2f TR((float)BoundsVP.Max.X, (float)BoundsVP.Min.Y);
				const FVector2f BL((float)BoundsVP.Min.X, (float)BoundsVP.Max.Y);
				const FVector2f BR((float)BoundsVP.Max.X, (float)BoundsVP.Max.Y);
				FSlateClippingZone Zone(ToWindow(TL), ToWindow(TR), ToWindow(BL), ToWindow(BR));
				OutDrawElements.PushClip(Zone);
				bPushed = true;
			}
		}
	}

	const int32 Result = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	if (bPushed)
	{
		OutDrawElements.PopClip();
	}
	return Result;
}

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
