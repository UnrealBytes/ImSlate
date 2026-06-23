// Copyright Epic Games, Inc. All Rights Reserved.
#include "ImSlate3DGizmoConstraint.h"

#if defined(IMSLATE3D_API)

#include "ImSlate3D.h"   // FImProjector

namespace ImSlate
{
// Replay baseline: dump the full projection inputs (ViewProj rows + ViewRect + cam) so the cursor→ray→world chain
// can be recomputed offline from raw cursor px. Called ONCE PER DRAG at mouse-down (the frozen projector is constant
// for the whole drag), so one dump per drag — no per-frame 16-float spam. (Exposed in the header for the handle.)
void ImDumpProjReplayBaseline(const FImProjector& Proj)
{
	const FMatrix& M = Proj.GetViewProjMatrix();
	const FIntRect VR = Proj.GetViewRect();
	const FVector Cam = Proj.GetCamPos();
	UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][replay] viewRect=(%d,%d,%d,%d) cam=(%.1f,%.1f,%.1f)"),
		VR.Min.X, VR.Min.Y, VR.Max.X, VR.Max.Y, Cam.X, Cam.Y, Cam.Z);
	UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][replay] VP r0=(%.5f,%.5f,%.5f,%.5f) r1=(%.5f,%.5f,%.5f,%.5f)"),
		M.M[0][0], M.M[0][1], M.M[0][2], M.M[0][3], M.M[1][0], M.M[1][1], M.M[1][2], M.M[1][3]);
	UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][replay] VP r2=(%.5f,%.5f,%.5f,%.5f) r3=(%.5f,%.5f,%.5f,%.5f)"),
		M.M[2][0], M.M[2][1], M.M[2][2], M.M[2][3], M.M[3][0], M.M[3][1], M.M[3][2], M.M[3][3]);
}

// --- Strict unproject helpers: every constraint maps the cursor through DeprojectScreenToWorldRay and intersects
// a world plane (the project/unproject pair), so the grabbed world point stays under the cursor. No screen-space
// approximations. ---

// Cursor view ray ∩ plane(PlanePoint, PlaneNormal) → world hit. False if ray too grazing (|cos| < MinFacing) or
// hit behind camera. MinFacing guards against the grazing-angle blow-up: when the ray is nearly parallel to the
// plane, a tiny denominator sends the intersection thousands of units away (the gizmo "flies off"). A larger
// MinFacing means "this plane is too edge-on to drag right now" → caller holds instead of jumping.
static bool RayPlane(const FImProjector& Proj, FVector2f CursorVP, const FVector& PlanePoint,
                     const FVector& PlaneNormal, FVector& OutHit, double MinFacing = 1e-4)
{
	FVector RayO, RayD;
	if (!Proj.DeprojectScreenToWorldRay(CursorVP, RayO, RayD)) { return false; }
	const double Denom = FVector::DotProduct(RayD, PlaneNormal);
	if (FMath::Abs(Denom) < MinFacing) { return false; }
	const double T = FVector::DotProduct(PlanePoint - RayO, PlaneNormal) / Denom;
	if (T <= 0.0) { return false; }
	OutHit = RayO + RayD * T;
	return true;
}

// Strict world param along WorldAxis for a cursor: intersect the cursor ray with the plane that CONTAINS the axis
// and faces the camera the most (normal = Axis × (Axis × ViewDir) = the in-axis-plane normal ⊥ axis toward view),
// then project (hit − anchor) onto the axis. This keeps the grabbed point on the axis under the cursor (the move
// gizmo's true behaviour). Returns false only when the axis points almost straight at the camera (no stable plane).
static bool RayAxisParam(const FImProjector& Proj, const FVector& AnchorW, const FVector& WorldAxis,
                         FVector2f CursorVP, double& OutT)
{
	const FVector ViewDir = (AnchorW - Proj.GetCamPos()).GetSafeNormal();
	// Only the truly degenerate case (axis almost exactly along the view → projects to a point) is rejected; the
	// near-edge-on band is kept usable and CLAMPED by the caller (Apply), not held. (|a·v|>0.999 ≈ within ~2.5°.)
	if (FMath::Abs(FVector::DotProduct(WorldAxis, ViewDir)) > 0.999) { return false; }
	// Plane normal = component of ViewDir ⊥ axis (so the plane contains the axis and most faces the camera).
	FVector N = ViewDir - WorldAxis * FVector::DotProduct(ViewDir, WorldAxis);
	if (N.IsNearlyZero()) { return false; }  // axis ∥ view → axis projects to a point → unusable
	N.Normalize();
	FVector Hit;
	if (!RayPlane(Proj, CursorVP, AnchorW, N, Hit)) { return false; }
	OutT = FVector::DotProduct(Hit - AnchorW, WorldAxis);  // signed distance along the axis
	if (IsImSlate3DDragLog())
	{
		const FVector Cam = Proj.GetCamPos();
		UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][axis] axis=(%.2f,%.2f,%.2f) a·v=%.3f anchor=(%.0f,%.0f,%.0f) "
			"cam=(%.0f,%.0f,%.0f) mouseVP=(%.1f,%.1f) T=%.1f hit=(%.0f,%.0f,%.0f)"),
			WorldAxis.X, WorldAxis.Y, WorldAxis.Z, FVector::DotProduct(WorldAxis, ViewDir),
			AnchorW.X, AnchorW.Y, AnchorW.Z, Cam.X, Cam.Y, Cam.Z, CursorVP.X, CursorVP.Y, OutT, Hit.X, Hit.Y, Hit.Z);
	}
	return true;
}

FTransform FImAxisConstraint::Apply(const FTransform& Start, const FImProjector& FrozenProj,
                                    FVector2f CursorStartVP, FVector2f CursorNowVP, bool& bOutValid) const
{
	bOutValid = true;
	const FVector AnchorW = Start.GetLocation();
	double TStart = 0.0, TNow = 0.0;
	if (!RayAxisParam(FrozenProj, AnchorW, WorldAxis, CursorStartVP, TStart) ||
	    !RayAxisParam(FrozenProj, AnchorW, WorldAxis, CursorNowVP,   TNow))
	{
		bOutValid = false;  // axis ~dead-on to camera → caller holds (no snap-back to Start)
		return Start;
	}
	double Move = TNow - TStart;
	// ALWAYS clamp to a sane multiple of cursor screen travel × world-units-per-px (same as plane). Normal/tilted
	// drags stay under the cap; only the dead-on blow-up is pulled to the nearest extreme. Cap tightens as the axis
	// nears pointing-at-camera (|a·v|→1, less reliable), always finite — never flung, never held.
	const double Facing = FMath::Abs(FVector::DotProduct(WorldAxis, (AnchorW - FrozenProj.GetCamPos()).GetSafeNormal()));
	const double CursorPx = (double)(CursorNowVP - CursorStartVP).Size();
	const double Base = FMath::Max(CursorPx * FrozenProj.WorldUnitsPerScreenPx(AnchorW), 1.0);
	const double Headroom = FMath::Lerp(12.0, 2.0, FMath::Clamp((Facing - 0.7) / 0.3, 0.0, 1.0));  // open:12× .. dead-on:2×
	const double MaxMove = Base * Headroom;
	if (FMath::Abs(Move) > MaxMove) { Move = FMath::Sign(Move) * MaxMove; }
	FTransform Out = Start;
	Out.SetLocation(AnchorW + WorldAxis * Move);
	return Out;
}

FTransform FImPlaneConstraint::Apply(const FTransform& Start, const FImProjector& FrozenProj,
                                     FVector2f CursorStartVP, FVector2f CursorNowVP, bool& bOutValid) const
{
	bOutValid = true;
	// The handle lies in the world plane through the anchor with normal = AxisU×AxisV. The cursor's view ray hits
	// that plane at a world point; the move is simply (now-hit − start-hit). This is the direct, correct screen→
	// plane mapping — both DOF, no axis decomposition. (Plane edge-on → ray ∥ plane → no hit → hold; that plane is
	// genuinely undraggable at that angle, which is expected, not a bug to work around.)
	const FVector PlanePoint  = Start.GetLocation();
	const FVector PlaneNormal = FVector::CrossProduct(AxisU, AxisV).GetSafeNormal();
	if (PlaneNormal.IsNearlyZero()) { bOutValid = false; return Start; }

	// Keep even fairly edge-on planes usable (MinFacing 0.02); only when truly parallel to the view does RayPlane
	// fail → hold. The near-edge-on blow-up is CLAMPED by angle below (not held, not by raw size).
	FVector HitStart, HitNow;
	if (!RayPlane(FrozenProj, CursorStartVP, PlanePoint, PlaneNormal, HitStart, 0.02) ||
	    !RayPlane(FrozenProj, CursorNowVP,   PlanePoint, PlaneNormal, HitNow,   0.02)) { bOutValid = false; return Start; }
	FVector Delta = HitNow - HitStart;   // already lies in the plane (both hits do)
	// ALWAYS clamp the move to a sane multiple of the cursor's screen travel × world-units-per-px. Normal & even
	// steeply-tilted drags stay well under the cap (a few ×); only the grazing blow-up (Δ in the thousands for a few
	// px) is pulled to the nearest in-direction extreme — never flung away, never held. Cap tightens as the plane
	// nears edge-on (less reliable), but is always finite. (R: plane drag flies to Δloc=4521)
	const FVector ViewUnit = (PlanePoint - FrozenProj.GetCamPos()).GetSafeNormal();
	const double Facing = FMath::Abs(FVector::DotProduct(ViewUnit, PlaneNormal));  // 1 head-on, 0 edge-on
	const double CursorPx = (double)(CursorNowVP - CursorStartVP).Size();
	const double Base = FMath::Max(CursorPx * FrozenProj.WorldUnitsPerScreenPx(PlanePoint), 1.0);
	const double Headroom = FMath::Lerp(2.0, 12.0, FMath::Clamp(Facing / 0.30, 0.0, 1.0));  // edge-on:2× .. open:12×
	const double MaxMove = Base * Headroom;
	if (Delta.SizeSquared() > MaxMove * MaxMove) { Delta = Delta.GetSafeNormal() * MaxMove; }
	FTransform Out = Start;
	Out.SetLocation(PlanePoint + Delta);
	if (IsImSlate3DDragLog())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][plane] N=(%.2f,%.2f,%.2f) mouseStart=(%.1f,%.1f) mouseNow=(%.1f,%.1f) "
			"hitStart=(%.0f,%.0f,%.0f) hitNow=(%.0f,%.0f,%.0f) delta=(%.1f,%.1f,%.1f)"),
			PlaneNormal.X, PlaneNormal.Y, PlaneNormal.Z, CursorStartVP.X, CursorStartVP.Y, CursorNowVP.X, CursorNowVP.Y,
			HitStart.X, HitStart.Y, HitStart.Z, HitNow.X, HitNow.Y, HitNow.Z, Delta.X, Delta.Y, Delta.Z);
	}
	return Out;
}

FTransform FImScaleConstraint::Apply(const FTransform& Start, const FImProjector& FrozenProj,
                                     FVector2f CursorStartVP, FVector2f CursorNowVP, bool& bOutValid) const
{
	bOutValid = true;
	const FVector AnchorW = Start.GetLocation();
	double TStart = 0.0, TNow = 0.0;
	if (!RayAxisParam(FrozenProj, AnchorW, WorldAxis, CursorStartVP, TStart) ||
	    !RayAxisParam(FrozenProj, AnchorW, WorldAxis, CursorNowVP,   TNow))
	{
		bOutValid = false;  // axis ~dead-on to camera → caller holds
		return Start;
	}
	// Screen-axis drag delta → scale factor. (Ref + Δ)/Ref: Δ>0 (drag outward) grows, Δ<0 shrinks; clamped
	// positive so the object never flips/zeroes. Reverse transform = same axis-projection math as move, but the
	// world delta becomes a ratio instead of a translation.
	const double Delta = TNow - TStart;
	const double Factor = FMath::Max((RefLengthWorld + Delta) / RefLengthWorld, 0.01);

	// Apply the factor to the Start scale along WorldAxis only (component-wise on the axis the handle represents).
	const FVector AbsAxis(FMath::Abs(WorldAxis.X), FMath::Abs(WorldAxis.Y), FMath::Abs(WorldAxis.Z));
	const FVector S = Start.GetScale3D();
	const FVector NewScale(
		S.X * FMath::Lerp(1.0, Factor, (double)AbsAxis.X),
		S.Y * FMath::Lerp(1.0, Factor, (double)AbsAxis.Y),
		S.Z * FMath::Lerp(1.0, Factor, (double)AbsAxis.Z));
	FTransform Out = Start;
	Out.SetScale3D(NewScale);
	return Out;
}

double FImRotateConstraint::SignedDeltaAngle(const FImProjector& FrozenProj, const FVector& Anchor,
                                             FVector2f CursorStartVP, FVector2f CursorNowVP) const
{
	// HYBRID. The rotation plane = through the anchor, normal = the axis.
	//  • Plane FACES the camera enough → unproject both cursor rays onto it; the signed world angle between the two
	//    hit directions about the axis is exact and inherently correctly-signed (right-hand). Best when usable.
	//  • Plane EDGE-ON (axis ⊥ view → plane ∥ view ray → rays miss it, bHit=0) → fall back to the SCREEN-tangent
	//    method: the cursor's screen angle about the projected anchor. Sign mapped to a right-hand rotation by the
	//    axis-facing direction (screen Y is down). This is the only method that still works when the ring is a line.
	const FVector ViewDir = (Anchor - FrozenProj.GetCamPos()).GetSafeNormal();
	const double AxisDotView = FVector::DotProduct(WorldAxis, ViewDir);

	FVector HitS, HitN;
	const bool bHitS = RayPlane(FrozenProj, CursorStartVP, Anchor, WorldAxis, HitS);
	const bool bHitN = RayPlane(FrozenProj, CursorNowVP,   Anchor, WorldAxis, HitN);
	const bool bPlaneUsable = bHitS && bHitN && FMath::Abs(AxisDotView) > 0.20;  // not too edge-on

	double Result = 0.0;
	const TCHAR* Method = TEXT("none");
	if (bPlaneUsable)
	{
		FVector Vs = HitS - Anchor;  Vs -= WorldAxis * FVector::DotProduct(Vs, WorldAxis);
		FVector Vn = HitN - Anchor;  Vn -= WorldAxis * FVector::DotProduct(Vn, WorldAxis);
		if (!Vs.IsNearlyZero() && !Vn.IsNearlyZero())
		{
			Vs.Normalize();  Vn.Normalize();
			const double SinT = FVector::DotProduct(FVector::CrossProduct(Vs, Vn), WorldAxis);
			const double CosT = FVector::DotProduct(Vs, Vn);
			Result = FMath::Atan2(SinT, CosT);
			Method = TEXT("plane");
		}
	}
	else
	{
		// Screen-tangent fallback (edge-on). Cursor screen angle about the projected anchor.
		const TOptional<FVector2f> SC = FrozenProj.ProjectWorld(Anchor);
		if (SC.IsSet())
		{
			const FVector2f A = CursorStartVP - *SC, B = CursorNowVP - *SC;
			if (A.SizeSquared() >= 1.f && B.SizeSquared() >= 1.f)
			{
				// Screen Y-down: atan2(cross, dot) is screen-clockwise-positive. A right-hand rotation about an axis
				// pointing TOWARD the camera (AxisDotView<0) appears screen-CCW, so screen-CW(+) ↔ −θ → negate; axis
				// pointing AWAY → +θ. So Sign = (AxisDotView < 0) ? -1 : +1.
				const double ScreenDelta = FMath::Atan2((double)(A.X * B.Y - A.Y * B.X), (double)(A.X * B.X + A.Y * B.Y));
				Result = ScreenDelta * ((AxisDotView < 0.0) ? -1.0 : 1.0);
				Method = TEXT("screen");
			}
		}
	}

	if (IsImSlate3DRotLog())
	{
		// Full replay set: axis, anchor, camPos, both cursor px, both world hits → the whole cursor→ray→plane→angle
		// chain can be recomputed offline. (hitS/hitN valid only when bHitS/bHitN=1; screen method uses cursor px.)
		const FVector Cam = FrozenProj.GetCamPos();
		UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][rot] axis=(%.2f,%.2f,%.2f) a·v=%.3f bHit=%d%d method=%s "
			"anchor=(%.0f,%.0f,%.0f) cam=(%.0f,%.0f,%.0f) mouseS=(%.1f,%.1f) mouseN=(%.1f,%.1f) "
			"hitS=(%.0f,%.0f,%.0f) hitN=(%.0f,%.0f,%.0f) deltaDeg=%.2f"),
			WorldAxis.X, WorldAxis.Y, WorldAxis.Z, AxisDotView, bHitS ? 1 : 0, bHitN ? 1 : 0, Method,
			Anchor.X, Anchor.Y, Anchor.Z, Cam.X, Cam.Y, Cam.Z,
			CursorStartVP.X, CursorStartVP.Y, CursorNowVP.X, CursorNowVP.Y,
			HitS.X, HitS.Y, HitS.Z, HitN.X, HitN.Y, HitN.Z, FMath::RadiansToDegrees(Result));
	}
	return Result;
}

FTransform FImRotateConstraint::Apply(const FTransform& Start, const FImProjector& FrozenProj,
                                      FVector2f CursorStartVP, FVector2f CursorNowVP, bool& bOutValid) const
{
	bOutValid = true;
	const double DeltaRad = SignedDeltaAngle(FrozenProj, Start.GetLocation(), CursorStartVP, CursorNowVP);
	if (FMath::IsNearlyZero(DeltaRad)) { bOutValid = false; return Start; }
	FTransform Out = Start;
	const FQuat DeltaQ(WorldAxis, DeltaRad);
	Out.SetRotation((DeltaQ * Start.GetRotation()).GetNormalized());  // pre-multiply: rotate about the world axis
	return Out;
}

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
