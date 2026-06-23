// Copyright Epic Games, Inc. All Rights Reserved.
#include "SImSlate3DPlaneArrow.h"

#if defined(IMSLATE3D_API)

#include "Rendering/DrawElements.h"
#include "SImSlate3DTransformBox.h"   // owner box: projector / cursor map / world loc write-back
#include "ImSlate3D.h"                // FImProjector
#include "ImSlate3DHitManager.h"      // FImSlate3DHitManager::IsWidgetHovered (single hover source)

namespace ImSlate
{
void SImSlate3DPlaneArrow::Construct(const FArguments& InArgs)
{
	WorldAxis   = InArgs._WorldAxis;
	Color       = InArgs._Color;
	ShaftLength = InArgs._ShaftLength;
	ShaftWidth  = InArgs._ShaftWidth;
	HeadLength  = InArgs._HeadLength;
	HeadWidth   = InArgs._HeadWidth;
	CapBulge    = InArgs._CapBulge;
	OnPositionChanged = InArgs._OnPositionChanged;
	// Visible (hit-test): a real hit leaf. The box's BuildLocalPath reaches it, runs HitTestPlaneLocal, and
	// RouteSelf forwards mouse events here — same unified path as any window control.
	SetVisibility(EVisibility::Visible);
	AddMetadata(MakeShared<FImPlaneShapedMeta>(static_cast<IImSlate3DPlaneShaped*>(this)));
}

bool SImSlate3DPlaneArrow::ResolveFromOwner() const
{
	TSharedPtr<SImSlate3DTransformBox> Box = OwnerBox.Pin();
	if (!Box.IsValid() || !Box->HasValidProjection()) { return false; }
	// Anchor = panel CENTRE, in widget-local px. The arrow extends from here along the in-plane dir; with the
	// window/box sized to ~2*reach (ComputeDesiredSize) the arrow stays inside [0..Size] for ANY direction, so
	// bInsideRect-gated hit-testing works. Centre also aligns the arrow root with the window's centre pivot →
	// it sits exactly on the anchored world position. Sibling arrows share this origin (a move gizmo's axes meet).
	const FVector2f Size = Box->GetCachedWidgetSize();
	AnchorLocal = Size * 0.5f;
	// In-plane unit dir = WorldAxis projected onto the plane basis (RightAxis/DownAxis), so the arrow lies ON
	// the sheet and its on-screen direction matches what dragging moves.
	const FImProjector& Proj = Box->GetCachedProjector();
	const FVector R = Proj.LocalToWorld(FVector2f(1.f, 0.f)) - Proj.LocalToWorld(FVector2f(0.f, 0.f));
	const FVector D = Proj.LocalToWorld(FVector2f(0.f, 1.f)) - Proj.LocalToWorld(FVector2f(0.f, 0.f));
	const FVector W = WorldAxis.GetSafeNormal();
	FVector2f D2((float)FVector::DotProduct(W, R.GetSafeNormal()), (float)FVector::DotProduct(W, D.GetSafeNormal()));
	Dir = (D2.SizeSquared() > UE_SMALL_NUMBER) ? D2.GetSafeNormal() : FVector2f(0.f, -1.f);
	return true;
}

bool SImSlate3DPlaneArrow::IsHovered2D() const
{
	// Single source of truth = the manager's GlobalHoveredPath. The arrow keeps NO hover flag.
	return FImSlate3DHitManager::Get().IsWidgetHovered(this);
}

FLinearColor SImSlate3DPlaneArrow::ResolveColor() const
{
	// Hover from the manager; drag highlight from bActive (the arrow's own concern). Orthogonal, can't conflict.
	if (!IsHovered2D() && !bActive)
	{
		return Color;
	}
	if (bHasHighlightColor)
	{
		return HighlightColor;
	}
	return FLinearColor(Color.R * HighlightScale, Color.G * HighlightScale, Color.B * HighlightScale, Color.A);
}

void SImSlate3DPlaneArrow::BuildArrowLocalPoly(FVector2f OutPoly[7]) const
{
	ResolveFromOwner();  // refresh AnchorLocal + Dir from the owner box's current projector before building
	const FVector2f Perp(-Dir.Y, Dir.X);
	const FVector2f ShaftEnd = AnchorLocal + Dir * ShaftLength;
	const FVector2f Tip      = ShaftEnd  + Dir * HeadLength;
	const float SHalf = ShaftWidth * 0.5f;
	const float HHalf = HeadWidth  * 0.5f;
	OutPoly[0] = AnchorLocal + Perp * SHalf;
	OutPoly[1] = ShaftEnd    + Perp * SHalf;
	OutPoly[2] = ShaftEnd    - Perp * SHalf;
	OutPoly[3] = AnchorLocal - Perp * SHalf;
	OutPoly[4] = ShaftEnd    + Perp * HHalf;
	OutPoly[5] = Tip;
	OutPoly[6] = ShaftEnd    - Perp * HHalf;
}

bool SImSlate3DPlaneArrow::GetCapEllipse(FVector2f& OutCenter, FVector2f& OutMajor, FVector2f& OutMinor) const
{
	if (CapBulge <= 0.f) { return false; }
	const FVector2f Perp(-Dir.Y, Dir.X);
	OutCenter = AnchorLocal + Dir * ShaftLength;            // head base
	OutMajor  = Perp * (HeadWidth * 0.5f);                 // width radius (along Perp)
	OutMinor  = Dir  * (HeadWidth * 0.5f * CapBulge);      // bulge along the axis dir
	return true;
}

bool SImSlate3DPlaneArrow::HitTestSelf(FVector2f WidgetLocalPt) const
{
	FVector2f P[7];
	BuildArrowLocalPoly(P);
	// shaft quad = tris (0,1,2)+(0,2,3); head tri = (4,5,6).
	if (PointInTri(WidgetLocalPt, P[0], P[1], P[2]) || PointInTri(WidgetLocalPt, P[0], P[2], P[3])
		|| PointInTri(WidgetLocalPt, P[4], P[5], P[6]))
	{
		return true;
	}
	// End-cap ellipse: (u/|Major|)²+(v/|Minor|)²≤1.
	FVector2f C, Maj, Min;
	if (GetCapEllipse(C, Maj, Min))
	{
		const FVector2f Rel = WidgetLocalPt - C;
		const float MajL2 = Maj.SizeSquared();
		const float MinL2 = Min.SizeSquared();
		if (MajL2 > UE_SMALL_NUMBER && MinL2 > UE_SMALL_NUMBER)
		{
			const float u = FVector2f::DotProduct(Rel, Maj) / MajL2;
			const float v = FVector2f::DotProduct(Rel, Min) / MinL2;
			if (u * u + v * v <= 1.f) { return true; }
		}
	}
	return false;
}

bool SImSlate3DPlaneArrow::GetPlaneLocalBounds(FVector2f& OutMin, FVector2f& OutMax) const
{
	FVector2f P[7];
	BuildArrowLocalPoly(P);
	OutMin = P[0];
	OutMax = P[0];
	for (int32 i = 1; i < 7; ++i)
	{
		OutMin.X = FMath::Min(OutMin.X, P[i].X); OutMin.Y = FMath::Min(OutMin.Y, P[i].Y);
		OutMax.X = FMath::Max(OutMax.X, P[i].X); OutMax.Y = FMath::Max(OutMax.Y, P[i].Y);
	}
	// End-cap ellipse can bulge past the head base along the axis dir — include its extremes.
	FVector2f C, Maj, Min;
	if (GetCapEllipse(C, Maj, Min))
	{
		for (const FVector2f& E : { C + Maj + Min, C + Maj - Min, C - Maj + Min, C - Maj - Min })
		{
			OutMin.X = FMath::Min(OutMin.X, E.X); OutMin.Y = FMath::Min(OutMin.Y, E.Y);
			OutMax.X = FMath::Max(OutMax.X, E.X); OutMax.Y = FMath::Max(OutMax.Y, E.Y);
		}
	}
	return true;
}

int32 SImSlate3DPlaneArrow::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FPaintGeometry PaintGeom = AllottedGeometry.ToPaintGeometry();
	const FLinearColor Col = ResolveColor();

	FVector2f P[7];
	BuildArrowLocalPoly(P);
	const FVector2f ShaftEnd = (P[1] + P[2]) * 0.5f;  // head base centre
	const FVector2f Tip = P[5];

	// Shaft = thick line (= filled bar of width ShaftWidth).
	{
		TArray<FVector2f> Bar; Bar.Add(AnchorLocal); Bar.Add(ShaftEnd);
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, PaintGeom, Bar, ESlateDrawEffect::None, Col,
			true, FMath::Max(ShaftWidth, 1.f));
	}
	// Head triangle = hatch-fill (line pipeline has no filled primitive).
	{
		const FVector2f BaseL = P[4];
		const FVector2f BaseR = P[6];
		const int32 Fill = FMath::Clamp(FMath::CeilToInt(HeadWidth * 0.5f), 3, 64);
		for (int32 k = 0; k <= Fill; ++k)
		{
			const FVector2f BasePt = FMath::Lerp(BaseL, BaseR, (float)k / (float)Fill);
			TArray<FVector2f> Spoke; Spoke.Add(Tip); Spoke.Add(BasePt);
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, PaintGeom, Spoke, ESlateDrawEffect::None, Col, true, 2.f);
		}
	}
	// Pseudo-3D end-cap ellipse (hatch-fill across major), darker for a depth cue. Same ellipse HitTestSelf uses.
	FVector2f EC, EMaj, EMin;
	if (GetCapEllipse(EC, EMaj, EMin))
	{
		const FLinearColor CapCol(Col.R * 0.7f, Col.G * 0.7f, Col.B * 0.7f, Col.A);
		const int32 N = FMath::Clamp(FMath::CeilToInt(EMaj.Size()), 6, 48);
		for (int32 k = -N; k <= N; ++k)
		{
			const float u = (float)k / (float)N;
			const float h = FMath::Sqrt(FMath::Max(0.f, 1.f - u * u));
			const FVector2f Mid = EC + EMaj * u;
			TArray<FVector2f> Chord; Chord.Add(Mid + EMin * h); Chord.Add(Mid - EMin * h);
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, PaintGeom, Chord, ESlateDrawEffect::None, CapCol, true, 2.f);
		}
	}
	return LayerId;
}

// --- Interaction (window-internal single-axis arrow; box RouteSelf forwards these) ----------------------

static bool CursorToPlaneLocal(const TSharedPtr<SImSlate3DTransformBox>& Box, const FPointerEvent& E, FVector2f& Out)
{
	if (!Box.IsValid() || !Box->HasValidProjection()) { return false; }
	const FVector2f CursorVP = Box->MapDesktopCursorToViewportPx(E.GetScreenSpacePosition());
	// MapScreenToPlane (not Unproject): bValid = ray hit the sheet; Local may be outside [0..Size] (arrow
	// extends past the panel). Must NOT gate on bInsideRect.
	const FImPlaneHit Hit = Box->GetCachedProjector().MapScreenToPlane(CursorVP);
	if (!Hit.bValid) { return false; }
	Out = Hit.Local;
	return true;
}

FReply SImSlate3DPlaneArrow::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	TSharedPtr<SImSlate3DTransformBox> Box = OwnerBox.Pin();
	// Only the pointer that started this drag drives it (multi-touch: another finger's move must not perturb us).
	if (bActive && (int32)MouseEvent.GetPointerIndex() == DragPointerIndex)
	{
		const FVector2f CursorVP = Box.IsValid() ? Box->MapDesktopCursorToViewportPx(MouseEvent.GetScreenSpacePosition())
		                                         : FVector2f::ZeroVector;
		double CurT;
		if (Box.IsValid() && ComputeDragT(CursorVP, CurT))
		{
			if (bDragStartTPending) { DragStartT = CurT; bDragStartTPending = false; }
			const double Step = CurT - DragStartT;
			// Write the new world position back through the delegate — the box (or a shared FVector) owns the
			// actual position. The box no longer knows about us; we don't call box->UpdatePlacementLocation.
			const FVector NewPos = DragStartWorldLoc + DragWorldAxis * Step;
			OnPositionChanged.ExecuteIfBound(NewPos);
			if (IsImSlate3DDebugVerbose())
			{
				// Proves the captured arrow is receiving move directly each drag frame (capture is intact).
				UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][drag] arrow=%p axis=%s step=%.2f newPos=(%.1f,%.1f,%.1f)"),
					this, *WorldAxis.ToString(), Step, NewPos.X, NewPos.Y, NewPos.Z);
			}
		}
		return FReply::Handled();
	}
	// NOT dragging: nothing to do. Hover is the manager's job (UpdateGlobalHover); the arrow keeps no hover
	// flag and reads HitManager::IsWidgetHovered in OnPaint. Nothing here can desync.
	return FReply::Unhandled();  // hover doesn't consume
}

FReply SImSlate3DPlaneArrow::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	TSharedPtr<SImSlate3DTransformBox> Box = OwnerBox.Pin();
	FVector2f PlaneLocal;
	if (!CursorToPlaneLocal(Box, MouseEvent, PlaneLocal) || !HitTestSelf(PlaneLocal))
	{
		if (IsImSlate3DDebug())
		{
			UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][hover] arrow=%p axis=%s OnMouseButtonDown MISS shape → Unhandled"), this, *WorldAxis.ToString());
		}
		return FReply::Unhandled();
	}
	if (IsImSlate3DDebug())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][hover] arrow=%p axis=%s OnMouseButtonDown HIT → begin drag + CaptureMouse"), this, *WorldAxis.ToString());
	}

	bActive = true;
	DragPointerIndex = (int32)MouseEvent.GetPointerIndex();   // remember which finger/pointer owns this drag
	delete DragProj;
	DragProj = new FImProjector(Box->GetCachedProjector());     // freeze for the whole drag
	DragStartWorldLoc = Box->GetWorldLocation();
	ResolveFromOwner();  // ensure AnchorLocal is current for the world-anchor below
	DragAnchorWorld   = DragProj->LocalToWorld(AnchorLocal);
	DragWorldAxis     = WorldAxis.GetSafeNormal();
	bDragStartTPending = true;
	DragStartT = 0.0;
	return FReply::Handled().CaptureMouse(AsShared());
}

FReply SImSlate3DPlaneArrow::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Only the owning pointer ends the drag (another finger's up over us must not).
	if (!bActive || (int32)MouseEvent.GetPointerIndex() != DragPointerIndex) { return FReply::Unhandled(); }
	if (IsImSlate3DDebug())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][hover] arrow=%p axis=%s OnMouseButtonUp → EndDrag + ReleaseCapture"), this, *WorldAxis.ToString());
	}
	EndDrag();
	return FReply::Handled().ReleaseMouseCapture();
}

void SImSlate3DPlaneArrow::EndDrag()
{
	// Only clear the drag state. Hover is the manager's (IsWidgetHovered) — after the drag, OnPaint reflects
	// whether the cursor is still over us this frame. Nothing to reset, nothing that can stick.
	bActive = false;
	DragPointerIndex = INDEX_NONE;
	delete DragProj;
	DragProj = nullptr;
}

bool SImSlate3DPlaneArrow::ComputeDragT(FVector2f CursorVP, double& OutT) const
{
	if (!DragProj) { return false; }
	TSharedPtr<SImSlate3DTransformBox> Box = OwnerBox.Pin();
	const float WScale = Box.IsValid() ? FMath::Max(Box->GetWorldScale(), UE_SMALL_NUMBER) : 1.f;
	const double Span = FMath::Max(120.0 * (double)WScale, 1.0);  // stable world span for the screen dir
	const TOptional<FVector2f> SA = DragProj->ProjectWorld(DragAnchorWorld - DragWorldAxis * Span);
	const TOptional<FVector2f> SB = DragProj->ProjectWorld(DragAnchorWorld + DragWorldAxis * Span);
	if (!SA.IsSet() || !SB.IsSet()) { return false; }
	FVector2f ScreenDir = *SB - *SA;
	const float Len = ScreenDir.Size();
	if (Len < 3.f) { return false; }  // axis ~dead-on to camera
	ScreenDir /= Len;
	const float ScreenT = FVector2f::DotProduct(CursorVP - *SA, ScreenDir);
	OutT = (double)ScreenT * (2.0 * Span / (double)Len) - Span;
	return true;
}

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
