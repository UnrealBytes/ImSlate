// Copyright Epic Games, Inc. All Rights Reserved.
#include "SImSlate3DConstraintHandle.h"

#if defined(IMSLATE3D_API)

#include "ImSlate3DHandleShape.h"      // IImHandleShape
#include "ImSlate3DGizmoConstraint.h"  // IImGizmoConstraint
#include "ImSlate3DProjection.h"       // ImBuildProjector
#include "ImSlate3DShaderElement.h"    // FIm3DQuad / FIm3DShaderElement
#include "Rendering/DrawElements.h"

namespace ImSlate
{
void SImSlate3DConstraintHandle::Construct(const FArguments& InArgs, const TSharedRef<IImHandleShape>& InShape,
	const TSharedRef<IImGizmoConstraint>& InConstraint)
{
	Shape = InShape;
	Constraint = InConstraint;
	Color = InArgs._Color;
	OverlapOrder = InArgs._OverlapOrder;
	WorldUnitScale = InArgs._WorldUnitScale;
	bHighPrecision = InArgs._HighPrecision;
	OnQueryStartTransform = InArgs._OnQueryStartTransform;
	OnTransformEdited = InArgs._OnTransformEdited;
	DragKind = InArgs._DragKind;
	AxisU = InArgs._AxisU.GetSafeNormal();
	AxisV = InArgs._AxisV.GetSafeNormal();
	// Visible: like SImSlate3DArrow, this widget is a 2D entry point — its SWidget::OnMouse* forward to the
	// shared FImSlate3DHitManager (which walks ALL units). It only claims the event when the cursor is over its
	// own shape (per-pixel); otherwise returns Unhandled so the click passes through (no fullscreen blocking).
	SetVisibility(EVisibility::Visible);
}

SImSlate3DConstraintHandle::~SImSlate3DConstraintHandle()
{
	FImSlate3DHitManager::Get().Unregister(this);
}

bool SImSlate3DConstraintHandle::ResolveProjector(const FImProjector*& OutProj) const
{
	FImWorldPlacement P;
	P.WorldLocation = GizmoTransform.GetLocation();
	P.WorldScale = WorldUnitScale;
	bSelfProjectorValid = ImBuildProjector(FVector2f(1.f, 1.f), P, SelfProjector);
	OutProj = &SelfProjector;
	return bSelfProjectorValid;
}

double SImSlate3DConstraintHandle::HandleWorldScale(const FImProjector& Proj) const
{
	return (WorldUnitScale == 0.f) ? Proj.WorldUnitsPerScreenPx(GizmoTransform.GetLocation())
	                               : FMath::Max((double)WorldUnitScale, (double)UE_SMALL_NUMBER);
}

FVector2f SImSlate3DConstraintHandle::ScreenToVP(const FVector2f& ScreenPx) const
{
	// Desktop/screen px → projector viewport px (the ProjView space ProjectWorld outputs in). The handle is a
	// FULLSCREEN leaf, so its own cached geometry is the fullscreen reference: AbsoluteToLocal kills the window
	// offset, ×Scale undoes DPI (screen is hi-DPI physical px, ProjView is logical), +ViewRect.Min adds the
	// view origin. SAME chain as SImSlate3DTransformBox::MapCursorToViewport — identity was wrong (it ignored
	// the DPI ratio, e.g. OutTex 2582 vs ProjView 1954 → hit cursor was 1.32× off the drawn shape). (R: hit DPI)
	const FImProjector* Proj = nullptr;
	const FIntRect VR = (ResolveProjector(Proj) && Proj) ? Proj->GetViewRect() : FIntRect(0, 0, 0, 0);
	return MapScreenToViewportPx(GetCachedGeometry(), VR, ScreenPx);
}

FVector2f SImSlate3DConstraintHandle::RawDeltaToVP(const FPointerEvent& Event) const
{
	return (FVector2f)Event.GetCursorDelta();  // standalone: raw delta already in screen/viewport px (no panel scale)
}

int32 SImSlate3DConstraintHandle::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FImProjector* Proj = nullptr;
	if (!ResolveProjector(Proj) || !Proj)
	{
		if (IsImSlate3DDebug()) { UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][chandle] OnPaint handle=%p projector INVALID → no draw"), this); }
		return LayerId;
	}
	const bool bHovered = FImSlate3DHitManager::Get().IsWidgetHovered(this) || Input.IsDragging();
	// While THIS handle drags, SetTransform is frozen (it edits off DragStartTransform), so GizmoTransform's LOCATION
	// is stale → the dragged square would sit still while the rest of the gizmo follows. Take the live LOCATION only
	// (from the owner, updated by OnTransformEdited this frame) and keep GizmoTransform's rotation/scale — those were
	// already resolved for the current axis-space (World clears rotation). Using the raw live transform instead made
	// the square JUMP on mouse-down (live carries the object rotation that GizmoTransform had stripped). (R: drag jump)
	FTransform RenderXform = GizmoTransform;
	if (Input.IsDragging() && OnQueryStartTransform.IsBound())
	{
		RenderXform.SetLocation(OnQueryStartTransform.Execute().GetLocation());
	}
	TArray<FIm3DQuad> Quads;
	Shape->BuildQuads(*Proj, RenderXform, HandleWorldScale(*Proj), Color, bHovered, Quads);
	if (IsImSlate3DDebug())
	{
		const FVector L = GizmoTransform.GetLocation();
		UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][chandle] OnPaint handle=%p anchor=(%.0f,%.0f,%.0f) wscale=%.3f quads=%d hov=%d"),
			this, L.X, L.Y, L.Z, HandleWorldScale(*Proj), Quads.Num(), bHovered ? 1 : 0);
	}
	if (Quads.Num() > 0)
	{
		// Store on the widget (mutable) so the strong ref outlives OnPaint → the draw element's TWeakPtr stays
		// valid until Draw_RenderThread. A local would null out before the render thread runs (R: invisible handles).
		KeepAliveElem = MakeShared<FIm3DShaderElement, ESPMode::ThreadSafe>(MoveTemp(Quads), Proj->GetViewRect());
		FSlateDrawElement::MakeCustom(OutDrawElements, LayerId, KeepAliveElem);
	}
	else
	{
		KeepAliveElem.Reset();
	}
	return LayerId;
}

// 2D entry points — forward to the shared dispatch (same pattern as SImSlate3DArrow). The handle's standalone
// viewport-px mapping is the identity-ish ScreenToVP; the manager walks all units and per-pixel-resolves.
FReply SImSlate3DConstraintHandle::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2f VP = ScreenToVP((FVector2f)MouseEvent.GetScreenSpacePosition());
	return FImSlate3DHitManager::Get().HandleEvent(MouseEvent, VP,
		[&MouseEvent](IImSlate3DHittable& H, FVector2f InVP) { return H.OnHitMouseButtonDown(MouseEvent, InVP); });
}

FReply SImSlate3DConstraintHandle::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2f VP = ScreenToVP((FVector2f)MouseEvent.GetScreenSpacePosition());
	return FImSlate3DHitManager::Get().HandleEvent(MouseEvent, VP,
		[&MouseEvent](IImSlate3DHittable& H, FVector2f InVP) { return H.OnHitMouseButtonUp(MouseEvent, InVP); });
}

FReply SImSlate3DConstraintHandle::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2f VP = ScreenToVP((FVector2f)MouseEvent.GetScreenSpacePosition());
	return FImSlate3DHitManager::Get().HandleEvent(MouseEvent, VP,
		[&MouseEvent](IImSlate3DHittable& H, FVector2f InVP) { return H.OnHitMouseMove(MouseEvent, InVP); });
}

FBox2D SImSlate3DConstraintHandle::GetScreenBoundsVP() const
{
	const FImProjector* Proj = nullptr;
	if (!ResolveProjector(Proj) || !Proj) { return FBox2D(ForceInit); }
	return Shape->GetScreenBoundsVP(*Proj, GizmoTransform, HandleWorldScale(*Proj));
}

bool SImSlate3DConstraintHandle::IsPixelHovered(const FPointerEvent& Event, FVector2f CursorVP) const
{
	const FImProjector* Proj = nullptr;
	if (!ResolveProjector(Proj) || !Proj) { return false; }
	return Shape->HitTestVP(*Proj, GizmoTransform, HandleWorldScale(*Proj), CursorVP);
}

void SImSlate3DConstraintHandle::HandleHitEvent(FReply& OutReply, const FPointerEvent& Event, FVector2f CursorVP,
	const TFunctionRef<FReply(IImSlate3DHittable&, FVector2f)>& Callback)
{
	if (OutReply.IsEventHandled()) { return; }
	if (Input.IsDragging() || IsPixelHovered(Event, CursorVP))
	{
		OutReply = Callback(*this, CursorVP);
	}
}

FReply SImSlate3DConstraintHandle::OnHitMouseButtonDown(const FPointerEvent& Event, FVector2f CursorVP)
{
	const FImProjector* Proj = nullptr;
	if (!ResolveProjector(Proj) || !Proj) { return FReply::Unhandled(); }
	// Snapshot the transform at mouse-down (drag basis). Then freeze the projector + record cursor start.
	DragStartTransform = OnQueryStartTransform.IsBound() ? OnQueryStartTransform.Execute() : GizmoTransform;
	if (IsImSlate3DDebug()) { UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][chandle] OnHitMouseButtonDown handle=%p → begin drag"), this); }
	Input.Begin(Event, *Proj, bHighPrecision, [this](const FVector2f& S) { return ScreenToVP(S); });
	// Lock the replay baseline ONCE at press (the frozen projector is constant for this drag) → offline recompute.
	if (IsImSlate3DDragLog() || IsImSlate3DRotLog()) { ImDumpProjReplayBaseline(Input.GetFrozenProjector()); }
	FReply R = FReply::Handled().CaptureMouse(AsShared());
	if (bHighPrecision) { R.UseHighPrecisionMouseMovement(AsShared()); }
	return R;
}

FReply SImSlate3DConstraintHandle::OnHitMouseMove(const FPointerEvent& Event, FVector2f CursorVP)
{
	if (!Input.IsDragging() || (int32)Event.GetPointerIndex() != Input.GetPointerIndex())
	{
		return FReply::Handled();
	}
	const FVector2f NowVP = Input.ComputeCursorNowVP(Event,
		[this](const FVector2f& S) { return ScreenToVP(S); },
		[this](const FPointerEvent& E) { return RawDeltaToVP(E); });
	LastCursorNowVP = NowVP;   // HUD rotate arc reads the live cursor sweep
	// Reverse transform: constraint maps (start transform, frozen proj, cursor start→now) → new transform. If the
	// frame is ill-conditioned (bValid=false: axis/plane edge-on, ray miss), SKIP committing — hold the last pose
	// instead of snapping the object back to the drag origin. (R: degenerate frame snap-back)
	bool bValid = true;
	const FTransform Wanted = Constraint->Apply(DragStartTransform, Input.GetFrozenProjector(),
		Input.GetCursorStartVP(), NowVP, bValid);
	if (!bValid) { return FReply::Handled(); }  // keep current GXform; no jump-back
	if (IsImSlate3DDebugVerbose())
	{
		const FRotator R = Wanted.GetRotation().Rotator();
		UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][chandle] drag handle=%p curNow=(%.1f,%.1f) start=(%.1f,%.1f) loc=(%.0f,%.0f,%.0f) scale=(%.2f,%.2f,%.2f) rot=(%.1f,%.1f,%.1f)"),
			this, NowVP.X, NowVP.Y, Input.GetCursorStartVP().X, Input.GetCursorStartVP().Y,
			Wanted.GetLocation().X, Wanted.GetLocation().Y, Wanted.GetLocation().Z,
			Wanted.GetScale3D().X, Wanted.GetScale3D().Y, Wanted.GetScale3D().Z, R.Pitch, R.Yaw, R.Roll);
	}
	OnTransformEdited.ExecuteIfBound(Wanted);   // gizmo clamps + commits + re-anchors all handles
	// Pipeline step ⑥ (render), ONE line PER MOVE EVENT (not per OnPaint frame): project the resulting transform's
	// anchor to screen so the full chain — cursor px → constraint Wanted → world→screen — is replayable on one row.
	if (IsImSlate3DDragLog() || IsImSlate3DRotLog())
	{
		const FImProjector* PP = nullptr;
		const TOptional<FVector2f> SAnchor = (ResolveProjector(PP) && PP) ? PP->ProjectWorld(Wanted.GetLocation()) : TOptional<FVector2f>();
		const FRotator WR = Wanted.GetRotation().Rotator();
		UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][paint] handle=%p kind=%d nowVP=(%.1f,%.1f) wantLoc=(%.0f,%.0f,%.0f) "
			"wantRot=(%.1f,%.1f,%.1f) → anchorScreen=(%.1f,%.1f)"),
			this, (int32)DragKind, NowVP.X, NowVP.Y, Wanted.GetLocation().X, Wanted.GetLocation().Y, Wanted.GetLocation().Z,
			WR.Pitch, WR.Yaw, WR.Roll, SAnchor.IsSet() ? SAnchor->X : -1.f, SAnchor.IsSet() ? SAnchor->Y : -1.f);
	}
	return FReply::Handled();
}

FReply SImSlate3DConstraintHandle::OnHitMouseButtonUp(const FPointerEvent& Event, FVector2f CursorVP)
{
	// End if WE are the dragging handle. Match the pointer when one was recorded, but if THIS handle is dragging
	// and the up arrives with a different/again-unset index (PIE fake-touch can renumber down→up), still end —
	// a stuck "dragging" handle would otherwise refuse every SetTransform and freeze in place. (R: stuck handle)
	if (!Input.IsDragging())
	{
		return FReply::Unhandled();
	}
	const bool bIndexMatch = (int32)Event.GetPointerIndex() == Input.GetPointerIndex();
	if (IsImSlate3DRotLog())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][chandle] Up handle=%p kind=%d ptrEvt=%d ptrDrag=%d match=%d → END"),
			this, (int32)DragKind, (int32)Event.GetPointerIndex(), Input.GetPointerIndex(), bIndexMatch ? 1 : 0);
	}
	Input.End();
	return FReply::Handled().ReleaseMouseCapture();
}

void SImSlate3DConstraintHandle::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	// Capture can vanish without an Up (Alt+Tab, focus loss, dragged out of the viewport). Without this the drag
	// stays "active" forever → every SetTransform is refused → the handle freezes at its drag-start pose while the
	// rest of the gizmo moves on (the stray floating square/ring). End the drag so the next Refresh re-anchors it.
	if (Input.IsDragging()) { Input.End(); }
	SLeafWidget::OnMouseCaptureLost(CaptureLostEvent);
}

bool SImSlate3DConstraintHandle::BuildHoverPath(FVector2f CursorVP, TArray<FArrangedWidget>& OutPath) const
{
	if (!IsPixelHovered(FPointerEvent(), CursorVP)) { return false; }
	OutPath.Add(FArrangedWidget(ConstCastSharedRef<SWidget>(AsShared()), GetCachedGeometry()));
	return true;
}

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
