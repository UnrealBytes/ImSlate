// Copyright Epic Games, Inc. All Rights Reserved.
#include "SImSlate3DGizmoHud.h"

#if defined(IMSLATE3D_API)

#include "ImSlate3DGizmo.h"            // FImSlate3DGizmo / FImGizmoDragState / EImDragKind
#include "ImSlate3DProjection.h"       // ImBuildProjector / AppendWorldSegmentQuad3D
#include "ImSlate3DShaderElement.h"    // FIm3DQuad / FIm3DShaderElement
#include "ImSlate3DHitManager.h"       // (coordinate parity notes)
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Fonts/SlateFontInfo.h"

namespace ImSlate
{
void SImSlate3DGizmoHud::Construct(const FArguments& InArgs, const TSharedRef<FImSlate3DGizmo>& InGizmo)
{
	Gizmo = InGizmo;
	SetVisibility(EVisibility::HitTestInvisible);  // pure overlay: draws on top, never claims input
	SetCanTick(false);
}

// One faint world axis line through Anchor along Dir (± so it reads as a full axis), as a screen ribbon.
static void GhostAxis(const FImProjector& Proj, const FVector& Anchor, const FVector& Dir, double WorldLen,
	const FVector4f& Col, TArray<FIm3DQuad>& Out)
{
	AppendWorldSegmentQuad3D(Proj, Anchor - Dir * WorldLen, Anchor + Dir * WorldLen, /*thicknessPx*/2.f, Col, Out);
}

// A ruler along the drag axis from the START anchor: the axis line + a tick every TickWorld units, with a bold
// zero mark at the origin. Ticks are short segments ⊥ the axis (in the screen plane via a camera-facing perp).
static void RulerAlongAxis(const FImProjector& Proj, const FVector& StartLoc, const FVector& Axis,
	double WorldPerPx, const FVector4f& Col, TArray<FIm3DQuad>& Out)
{
	const double Len = WorldPerPx * 140.0;            // ruler half-length on screen (~140px)
	// Choose a "nice" world tick (…1,2,5,10,20,50…) so ~8 ticks span the ruler.
	const double Target = Len / 8.0;
	const double Pows[3] = { 1.0, 2.0, 5.0 };
	double Best = 10.0, BestErr = TNumericLimits<double>::Max();
	for (int32 e = -1; e <= 4; ++e) { for (double m : Pows) { const double S = m * FMath::Pow(10.0, (double)e);
		const double Err = FMath::Abs(S - Target); if (Err < BestErr) { BestErr = Err; Best = S; } } }
	const double Step = FMath::Max(Best, 1.0);
	AppendWorldSegmentQuad3D(Proj, StartLoc - Axis * Len, StartLoc + Axis * Len, 2.f, Col, Out);
	// Perp for ticks: axis × view dir, normalised, scaled to a few px.
	const FVector ViewDir = (StartLoc - Proj.GetCamPos()).GetSafeNormal();
	FVector Perp = FVector::CrossProduct(Axis, ViewDir).GetSafeNormal();
	if (Perp.IsNearlyZero()) { Perp = FVector::CrossProduct(Axis, FVector::UpVector).GetSafeNormal(); }
	for (double d = -Len; d <= Len + 1e-3; d += Step)
	{
		const FVector C = StartLoc + Axis * d;
		const bool bZero = FMath::Abs(d) < Step * 0.5;
		const double TickPx = (bZero ? 9.0 : 5.0) * WorldPerPx;
		AppendWorldSegmentQuad3D(Proj, C - Perp * TickPx, C + Perp * TickPx, bZero ? 3.f : 1.5f, Col, Out);
	}
}

// A rectangle outline in the world plane (U,V) centred at Centre, half-extent HalfWorld, as 4 ribbons.
static void PlaneOutline(const FImProjector& Proj, const FVector& Centre, const FVector& U, const FVector& V,
	double HalfWorld, float Thick, const FVector4f& Col, TArray<FIm3DQuad>& Out)
{
	const FVector A = Centre - U * HalfWorld - V * HalfWorld;
	const FVector B = Centre + U * HalfWorld - V * HalfWorld;
	const FVector C = Centre + U * HalfWorld + V * HalfWorld;
	const FVector D = Centre - U * HalfWorld + V * HalfWorld;
	AppendWorldSegmentQuad3D(Proj, A, B, Thick, Col, Out);
	AppendWorldSegmentQuad3D(Proj, B, C, Thick, Col, Out);
	AppendWorldSegmentQuad3D(Proj, C, D, Thick, Col, Out);
	AppendWorldSegmentQuad3D(Proj, D, A, Thick, Col, Out);
}


int32 SImSlate3DGizmoHud::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	TSharedPtr<FImSlate3DGizmo> G = Gizmo.Pin();
	if (!G.IsValid()) { return LayerId; }
	const FImGizmoDragState DS = G->QueryDragState();
	if (!DS.bDragging) { KeepAliveElem.Reset(); return LayerId; }

	// Self-build a projector anchored at the GHOST origin (the start location) — same scene camera the handles use.
	FImWorldPlacement P;
	P.WorldLocation = DS.Start.GetLocation();
	P.WorldScale = 0.f;                 // screen-constant
	FImProjector Proj;
	if (!ImBuildProjector(FVector2f(1.f, 1.f), P, Proj)) { KeepAliveElem.Reset(); return LayerId; }

	const FVector StartLoc = DS.Start.GetLocation();
	const double WorldPerPx = Proj.WorldUnitsPerScreenPx(StartLoc);
	const FVector4f Ghost(0.75f, 0.75f, 0.78f, 0.5f);   // faint grey ghost
	const FVector4f Accent(1.0f, 0.85f, 0.35f, 0.85f);  // amber: ruler / outline / arc

	TArray<FIm3DQuad> Quads;

	// Always: a faint 3-axis ghost at the START location, along the WORLD axes — consistent with the constraint,
	// the World-mode handle render, and the plane outline (all world-axis). Earlier this used Start.Rotation, which
	// made the ghost axes tilt while the outline stayed world-aligned → "start plane doesn't match". (R: hud axis-space)
	GhostAxis(Proj, StartLoc, FVector(1, 0, 0), WorldPerPx * 90.0, Ghost, Quads);
	GhostAxis(Proj, StartLoc, FVector(0, 1, 0), WorldPerPx * 90.0, Ghost, Quads);
	GhostAxis(Proj, StartLoc, FVector(0, 0, 1), WorldPerPx * 90.0, Ghost, Quads);

	// Per-kind reference overlay (user's ask: ruler for axis, start outline for plane, swept arc for rotate).
	switch (DS.Kind)
	{
	case EImDragKind::Move:
	case EImDragKind::Scale:
		RulerAlongAxis(Proj, StartLoc, DS.AxisU.GetSafeNormal(), WorldPerPx, Accent, Quads);
		break;
	case EImDragKind::Plane:
	{
		// Start outline (where the square WAS) + current outline (where it moved to) → the offset reads at a glance.
		// Edge axes = the WORLD axes (DS.AxisU/V), matching the constraint and the World-mode handle render. Do NOT
		// pre-multiply Start.Rotation (that desynced the outline from the actual square once the object had rotation).
		const double Half = WorldPerPx * 30.0;
		const FVector EU = DS.AxisU.GetSafeNormal();
		const FVector EV = DS.AxisV.GetSafeNormal();
		PlaneOutline(Proj, StartLoc,            EU, EV, Half, 1.5f, Ghost,  Quads);
		PlaneOutline(Proj, DS.Now.GetLocation(), EU, EV, Half, 2.0f, Accent, Quads);
		break;
	}
	case EImDragKind::Rotate:
	{
		// Rotation axis = the SAME axis the constraint and the ring handle use: the WORLD axis (DS.AxisU). Do NOT
		// pre-multiply by Start.Rotation — the ring handle renders in the gizmo's axis-space (World mode strips the
		// rotation), so multiplying here desynced the HUD wedge plane from the actual ring (the "arc not on the ring"
		// bug). The constraint's FImRotateConstraint also turns about this world axis, so wedge ≡ applied angle.
		const FVector WAxis = DS.AxisU.GetSafeNormal();
		const double R = (double)110.f * WorldPerPx;

		// Cursor ray ∩ rotation plane → in-plane direction from the anchor (unit). bOk=false if edge-on (ray misses).
		auto GrabDir = [&](FVector2f CursorVP, FVector& OutDir) -> bool
		{
			FVector RayO, RayD;
			if (!Proj.DeprojectScreenToWorldRay(CursorVP, RayO, RayD)) { return false; }
			const double Denom = FVector::DotProduct(RayD, WAxis);
			if (FMath::Abs(Denom) < 1e-4) { return false; }
			const double T = FVector::DotProduct(StartLoc - RayO, WAxis) / Denom;
			if (T <= 0.0) { return false; }
			FVector Dir = (RayO + RayD * T) - StartLoc;
			Dir -= WAxis * FVector::DotProduct(Dir, WAxis);   // into the plane
			if (Dir.IsNearlyZero()) { return false; }
			OutDir = Dir.GetSafeNormal();
			return true;
		};

		FVector DirS, DirN;
		const bool bS = GrabDir(DS.CursorStartVP, DirS);
		const bool bN = GrabDir(DS.CursorNowVP,   DirN);
		if (bS && bN)
		{
			// Arc from DirS to DirN the signed-short way about WAxis (matches the constraint's atan2 angle).
			const double Sweep = FMath::Atan2(FVector::DotProduct(FVector::CrossProduct(DirS, DirN), WAxis),
			                                  FVector::DotProduct(DirS, DirN));
			auto Pt = [&](const FVector& Dir, double Rad)
			{
				const FVector Perp = FVector::CrossProduct(WAxis, Dir);   // 90° ahead in-plane
				return StartLoc + (Dir * FMath::Cos(Rad) + Perp * FMath::Sin(Rad)) * R;
			};
			AppendWorldSegmentQuad3D(Proj, StartLoc, StartLoc + DirS * R, 1.5f, Ghost,  Quads);  // grabbed radius
			AppendWorldSegmentQuad3D(Proj, StartLoc, StartLoc + DirN * R, 2.0f, Accent, Quads);  // current radius
			const int32 Seg = FMath::Clamp((int32)(FMath::Abs(Sweep) / (PI / 24.0)) + 1, 1, 64);
			FVector Prev = StartLoc + DirS * R;
			for (int32 k = 1; k <= Seg; ++k)
			{
				const FVector Cur = Pt(DirS, Sweep * ((double)k / (double)Seg));
				AppendWorldSegmentQuad3D(Proj, Prev, Cur, 2.0f, Accent, Quads);
				Prev = Cur;
			}
		}
		break;
	}
	default: break;
	}

	if (Quads.Num() > 0)
	{
		// Keep alive past OnPaint: the draw element holds the element as a TWeakPtr (R: invisible handles pattern).
		KeepAliveElem = MakeShared<FIm3DShaderElement, ESPMode::ThreadSafe>(MoveTemp(Quads), Proj.GetViewRect());
		FSlateDrawElement::MakeCustom(OutDrawElements, LayerId, KeepAliveElem);
	}
	else
	{
		KeepAliveElem.Reset();
	}

	// Numeric readout next to the CURSOR (follows the mouse). The cursor is in ProjView px (CursorNowVP); a handle
	// drag fills it, an axis-arrow drag leaves it zero → fall back to the live anchor's projection (the cursor sits
	// right on the axis there anyway). ProjView px → widget-local is the inverse of the shared hit map
	// (RefGeom fullscreen → AbsoluteToLocal*Scale == ProjView px): local = (ProjViewPx - ViewRect.Min) / Geom.Scale.
	FVector2f AnchorVP = DS.CursorNowVP;
	if (AnchorVP.IsNearlyZero())
	{
		const TOptional<FVector2f> NowS = Proj.ProjectWorld(DS.Now.GetLocation());
		if (NowS.IsSet()) { AnchorVP = *NowS; }
	}
	if (!AnchorVP.IsNearlyZero())
	{
		const FIntRect VR = Proj.GetViewRect();
		const float Sc = FMath::Max(AllottedGeometry.Scale, UE_SMALL_NUMBER);
		const FVector2f Local((AnchorVP.X - (float)VR.Min.X) / Sc + 16.f, (AnchorVP.Y - (float)VR.Min.Y) / Sc + 14.f);

		const FVector DLoc = DS.Now.GetLocation() - DS.Start.GetLocation();
		const FVector DScale = DS.Now.GetScale3D() - DS.Start.GetScale3D();
		const FQuat DQuat = DS.Now.GetRotation() * DS.Start.GetRotation().Inverse();
		FVector RotAxis; float RotAngRad = 0.f;
		DQuat.ToAxisAndAngle(RotAxis, RotAngRad);

		TArray<FString> Lines;
		if (!DLoc.IsNearlyZero(0.01))   { Lines.Add(FString::Printf(TEXT("Δloc  %.1f, %.1f, %.1f"), DLoc.X, DLoc.Y, DLoc.Z)); }
		if (!DScale.IsNearlyZero(0.001)) { Lines.Add(FString::Printf(TEXT("Δscale %.2f, %.2f, %.2f"), DScale.X, DScale.Y, DScale.Z)); }
		if (FMath::Abs(RotAngRad) > 0.001f) { Lines.Add(FString::Printf(TEXT("Δangle %.1f°"), FMath::RadiansToDegrees(RotAngRad))); }

		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 12);
		const FLinearColor TextCol(1.f, 0.95f, 0.6f, 1.f);
		for (int32 i = 0; i < Lines.Num(); ++i)
		{
			const FPaintGeometry PG = AllottedGeometry.ToPaintGeometry(
				FVector2D(220.0, 16.0), FSlateLayoutTransform(FVector2D(Local.X, Local.Y + i * 16.f)));
			FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1, PG, Lines[i], Font, ESlateDrawEffect::None, TextCol);
		}
	}

	return LayerId + 1;
}

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
