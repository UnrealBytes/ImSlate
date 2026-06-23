// Copyright Epic Games, Inc. All Rights Reserved.
#include "ImSlate3DGizmo.h"

#if defined(IMSLATE3D_API)

#include "SImSlate3DArrow.h"
#include "SImSlate3DConstraintHandle.h"
#include "SImSlate3DGizmoHud.h"
#include "ImSlate3DHandleShape.h"
#include "ImSlate3DGizmoConstraint.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

namespace ImSlate
{
static const FLinearColor GAxisColors[3] = {
	FLinearColor(0.95f, 0.20f, 0.20f, 1.f),  // X red
	FLinearColor(0.25f, 0.85f, 0.30f, 1.f),  // Y green
	FLinearColor(0.25f, 0.45f, 0.95f, 1.f),  // Z blue
};
static const FVector GWorldAxes[3] = { FVector(1,0,0), FVector(0,1,0), FVector(0,0,1) };

FImSlate3DGizmo::FImSlate3DGizmo() = default;

FTransform FImSlate3DGizmo::GetTransform() const
{
	return QueryTransform.IsBound() ? QueryTransform.Execute() : InternalXform;
}

FTransform FImSlate3DGizmo::RenderTransformForHandles() const
{
	FTransform X = GetTransform();
	if (AxisSpace == EImAxisSpace::World) { X.SetRotation(FQuat::Identity); }  // axis-aligned handles
	if (IsImSlate3DRotLog())
	{
		const FRotator GR = GetTransform().GetRotation().Rotator();
		const FRotator HR = X.GetRotation().Rotator();
		static int32 N = 0;
		if ((N++ % 30) == 0)  // throttle: once per ~30 frames
		{
			UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][space] mode=%s gizmoRot=(%.1f,%.1f,%.1f) handleRot=(%.1f,%.1f,%.1f)"),
				AxisSpace == EImAxisSpace::World ? TEXT("World") : TEXT("Local"),
				GR.Pitch, GR.Yaw, GR.Roll, HR.Pitch, HR.Yaw, HR.Roll);
		}
	}
	return X;
}

void FImSlate3DGizmo::SetAxisLengths(float ShaftLen, float ShaftWid, float HeadLen, float HeadWid)
{
	AxisShaftLength = ShaftLen; AxisShaftWidth = ShaftWid; AxisHeadLength = HeadLen; AxisHeadWidth = HeadWid;
}

void FImSlate3DGizmo::BuildAxes()
{
	Arrows.Reset();
	const FVector Anchor = GetTransform().GetLocation();
	const FVector WorldAxes[3] = { FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1) };
	const FLinearColor Colors[3] = {
		FLinearColor(0.95f, 0.20f, 0.20f, 1.f),  // X red
		FLinearColor(0.25f, 0.85f, 0.30f, 1.f),  // Y green
		FLinearColor(0.25f, 0.45f, 0.95f, 1.f),  // Z blue
	};
	// Self-binding the drag write-back to a member is safe: the gizmo (TSharedFromThis) outlives the arrows it
	// owns. Capture a weak self so a stray callback after destruction is a no-op.
	TWeakPtr<FImSlate3DGizmo> WeakSelf = AsShared();
	for (int32 i = 0; i < 3; ++i)
	{
		TSharedRef<SImSlate3DArrow> Arrow = SNew(SImSlate3DArrow)
			.SelfProjector(true)
			.AnchorWorld(Anchor)
			.WorldAxis(WorldAxes[i])
			.WorldUnitScale(0.f)              // screen-constant size
			.ReverseWhenBackface(true)        // camera-facing axis draws its visible near-half
			.Color(Colors[i])
			.OverlapOrder(i)
			.ShaftLength(AxisShaftLength * UniformScale).ShaftWidth(AxisShaftWidth * UniformScale)
			.HeadLength(AxisHeadLength * UniformScale).HeadWidth(AxisHeadWidth * UniformScale)
			.OnPositionChanged_Lambda([WeakSelf](const FVector& NewLoc)
			{
				if (TSharedPtr<FImSlate3DGizmo> Self = WeakSelf.Pin()) { Self->OnAxisMoved(NewLoc); }
			});
		Arrows.Add(Arrow);
	}
}

void FImSlate3DGizmo::OnAxisMoved(const FVector& NewLoc)
{
	// Build the transform the drag WANTS (keep current rotation/scale, set the new location), let the owner
	// clamp it (EditTransform), and store the actual. Refresh() re-anchors all axes there next frame.
	FTransform Wanted = GetTransform();
	Wanted.SetLocation(NewLoc);
	const FTransform Actual = EditTransform.IsBound() ? EditTransform.Execute(Wanted) : Wanted;
	if (!QueryTransform.IsBound()) { InternalXform = Actual; }  // no owner store → keep it ourselves
}

void FImSlate3DGizmo::Refresh()
{
	const FTransform Xform = RenderTransformForHandles();   // World mode: identity rotation; Local: the live rotation
	const FVector Anchor = Xform.GetLocation();
	const FQuat Rot = Xform.GetRotation();
	for (int32 i = 0; i < Arrows.Num(); ++i)
	{
		if (Arrows[i].IsValid())
		{
			Arrows[i]->SetAnchorWorld(Anchor);              // ignored on the axis currently dragging (it owns it)
			Arrows[i]->SetWorldAxis(Rot.RotateVector(GWorldAxes[i]));  // axes splay with the gizmo (Local mode)
		}
	}
	RefreshConstraintHandles();
}

// ---------------- constraint-driven handles (scale / plane / rotate) ----------------

void FImSlate3DGizmo::OnConstraintEdited(const FTransform& Wanted)
{
	// One clamp point for every constraint handle (mirrors OnAxisMoved but takes a full transform).
	const FTransform Actual = EditTransform.IsBound() ? EditTransform.Execute(Wanted) : Wanted;
	if (!QueryTransform.IsBound()) { InternalXform = Actual; }
	if (IsImSlate3DDragLog() || IsImSlate3DRotLog())  // pipeline step ④: edit/clamp Wanted → Actual → commit GXform
	{
		const FRotator WR = Wanted.GetRotation().Rotator(), AR = Actual.GetRotation().Rotator();
		UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][edit] wantLoc=(%.1f,%.1f,%.1f) wantRot=(%.1f,%.1f,%.1f) wantScale=(%.2f,%.2f,%.2f) "
			"→ actLoc=(%.1f,%.1f,%.1f) actRot=(%.1f,%.1f,%.1f) actScale=(%.2f,%.2f,%.2f)"),
			Wanted.GetLocation().X, Wanted.GetLocation().Y, Wanted.GetLocation().Z, WR.Pitch, WR.Yaw, WR.Roll,
			Wanted.GetScale3D().X, Wanted.GetScale3D().Y, Wanted.GetScale3D().Z,
			Actual.GetLocation().X, Actual.GetLocation().Y, Actual.GetLocation().Z, AR.Pitch, AR.Yaw, AR.Roll,
			Actual.GetScale3D().X, Actual.GetScale3D().Y, Actual.GetScale3D().Z);
	}
}

void FImSlate3DGizmo::AddConstraintHandle(const TSharedRef<IImHandleShape>& Shape,
	const TSharedRef<IImGizmoConstraint>& Constraint, const FLinearColor& Color, int32 Order,
	EImDragKind Kind, const FVector& AxisU, const FVector& AxisV)
{
	TWeakPtr<FImSlate3DGizmo> WeakSelf = AsShared();
	TSharedRef<SImSlate3DConstraintHandle> H = SNew(SImSlate3DConstraintHandle, Shape, Constraint)
		.Color(Color)
		.OverlapOrder(Order)
		.WorldUnitScale(0.f)      // screen-constant size
		.HighPrecision(false)     // absolute cursor (scale/plane/rotate don't need infinite scrub)
		.DragKind(Kind)
		.AxisU(AxisU)
		.AxisV(AxisV)
		.OnQueryStartTransform_Lambda([WeakSelf]() -> FTransform
		{
			TSharedPtr<FImSlate3DGizmo> S = WeakSelf.Pin();
			return S.IsValid() ? S->GetTransform() : FTransform::Identity;
		})
		.OnTransformEdited_Lambda([WeakSelf](const FTransform& Wanted)
		{
			if (TSharedPtr<FImSlate3DGizmo> S = WeakSelf.Pin()) { S->OnConstraintEdited(Wanted); }
		});
	H->SetTransform(RenderTransformForHandles());
	ConstraintHandles.Add(H);
}

void FImSlate3DGizmo::BuildScaleHandles()
{
	for (int32 i = 0; i < 3; ++i)
	{
		// Round cap at the axis tip, facing ALONG the axis (disc normal = axis) → clearly a scale grip, distinct from
		// the flat plane squares. FIXED offset (bScaleByAxis=false): letting it slide with Scale flung the disc far
		// off the gizmo once Scale grew (the stray green discs), and it read as a second disc. Feedback is the drag
		// itself + the HUD Δscale readout, not a sliding disc.
		TSharedRef<IImHandleShape> Shape = MakeShared<FImDiscShape>(GWorldAxes[i], 140.f * UniformScale, 13.f * UniformScale, /*bScaleByAxis*/false);
		TSharedRef<IImGizmoConstraint> C = MakeShared<FImScaleConstraint>(GWorldAxes[i]);
		AddConstraintHandle(Shape, C, GAxisColors[i], /*order*/10 + i, EImDragKind::Scale, GWorldAxes[i], FVector::ZeroVector);
	}
}

void FImSlate3DGizmo::BuildPlaneHandles()
{
	// One plane handle PER AXIS, coloured by that axis. The square FACES ALONG its axis (square normal = the axis,
	// i.e. ⊥ the same-colour arrow) and you drag within the plane ⊥ that axis. So the red handle is the square
	// standing across the red arrow, moving in the plane perpendicular to red, etc. Square edges = the OTHER two
	// world axes (they span that perpendicular plane); offset along both so it sits in the quadrant, not on the axis.
	const int32 UAx[3] = { 1, 2, 0 };  // edge axis U (the two axes ⊥ the handle's own axis)
	const int32 VAx[3] = { 2, 0, 1 };  // edge axis V
	for (int32 i = 0; i < 3; ++i)
	{
		const FVector N = GWorldAxes[i];        // the handle's axis = its square normal = same-colour arrow
		const FVector U = GWorldAxes[UAx[i]];
		const FVector V = GWorldAxes[VAx[i]];
		// Square in the quadrant between the two edge axes (original placement: offset 45 along each, half 16).
		TSharedRef<IImHandleShape> Shape = MakeShared<FImBoxShape>(U, V, 45.f * UniformScale, 45.f * UniformScale, 16.f * UniformScale);
		TSharedRef<IImGizmoConstraint> C = MakeShared<FImPlaneConstraint>(U, V);  // drag in the plane ⊥ N
		(void)N;  // N == U×V (the same-colour axis / square normal); the HUD outline derives it from U,V.
		// HUD descriptor: AxisU/AxisV = the square's two EDGE axes → the outline lies in the same plane, normal = N.
		// LOWEST priority: the plane square is a big target that overlaps the arrows/rings/discs; give it the
		// smallest overlap order so those more specific handles win the click, and the plane only catches a click
		// when nothing else is under the cursor. (Dispatch is descending order → smaller = checked last.)
		AddConstraintHandle(Shape, C, GAxisColors[i], /*order*/-10 + i, EImDragKind::Plane, U, V);
	}
}

void FImSlate3DGizmo::BuildRotateHandles()
{
	for (int32 i = 0; i < 3; ++i)
	{
		TSharedRef<IImHandleShape> Shape = MakeShared<FImRingShape>(GWorldAxes[i], 110.f * UniformScale, 4.f * FMath::Max(UniformScale, 1.f));
		TSharedRef<IImGizmoConstraint> C = MakeShared<FImRotateConstraint>(GWorldAxes[i]);
		AddConstraintHandle(Shape, C, GAxisColors[i], /*order*/i, EImDragKind::Rotate, GWorldAxes[i], FVector::ZeroVector);  // rings below tips
	}
}

void FImSlate3DGizmo::RefreshConstraintHandles()
{
	const FTransform Xform = RenderTransformForHandles();
	for (const TSharedPtr<SImSlate3DConstraintHandle>& H : ConstraintHandles)
	{
		if (H.IsValid()) { H->SetTransform(Xform); }
	}
}

// ---------------- drag HUD (ghost + delta) ----------------

void FImSlate3DGizmo::BuildHud()
{
	Hud = SNew(SImSlate3DGizmoHud, AsShared());
}

FImGizmoDragState FImSlate3DGizmo::QueryDragState() const
{
	FImGizmoDragState Out;
	// A constraint handle (scale/plane/rotate) carries the full start transform — use it directly.
	for (const TSharedPtr<SImSlate3DConstraintHandle>& H : ConstraintHandles)
	{
		if (H.IsValid() && H->IsActivelyDragging())
		{
			Out.bDragging = true;
			Out.Start = H->GetDragStartTransform();
			Out.Now = GetTransform();
			Out.Kind = H->GetDragKind();
			Out.AxisU = H->GetAxisU();
			Out.AxisV = H->GetAxisV();
			Out.CursorStartVP = H->GetCursorStartVP();
			Out.CursorNowVP = H->GetCursorNowVP();
			return Out;
		}
	}
	// An axis arrow only moves location; its start transform = current rotation/scale with the start location.
	for (const TSharedPtr<SImSlate3DArrow>& A : Arrows)
	{
		if (A.IsValid() && A->IsActivelyDragging())
		{
			const FTransform Now = GetTransform();
			Out.bDragging = true;
			Out.Now = Now;
			Out.Start = Now;
			Out.Start.SetLocation(A->GetDragStartWorldLoc());
			Out.Kind = EImDragKind::Move;
			Out.AxisU = A->GetWorldAxis();
			return Out;
		}
	}
	return Out;  // bDragging == false
}

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
