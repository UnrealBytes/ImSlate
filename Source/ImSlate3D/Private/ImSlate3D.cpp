// Copyright Epic Games, Inc. All Rights Reserved.
#include "ImSlate3D.h"

#if defined(IMSLATE3D_API)

#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarImSlate3DDebug(
	TEXT("imslate.3d.Debug"), 0,
	TEXT("Log ImSlate 3D projector axes + projected corners each Build (1=on)."));

static TAutoConsoleVariable<int32> CVarImSlate3DRotLog(
	TEXT("imslate.3d.rotlog"), 0,
	TEXT("Log gizmo ROTATE-drag sign data (axis/facing/screen-cross/deltaRad) on each rotate move only — no "
	     "per-frame noise. For diagnosing rotation direction. (1=on)"));

static TAutoConsoleVariable<int32> CVarImSlate3DDragLog(
	TEXT("imslate.3d.draglog"), 0,
	TEXT("Log gizmo constraint-drag math (plane/scale axis params + resulting delta) on each move only — no "
	     "per-frame noise. For diagnosing plane/scale translation. (1=on)"));

namespace ImSlate
{
bool IsImSlate3DDebug()
{
	return CVarImSlate3DDebug.GetValueOnAnyThread() != 0;
}
bool IsImSlate3DDebugVerbose()
{
	return CVarImSlate3DDebug.GetValueOnAnyThread() >= 2;
}
bool IsImSlate3DRotLog()
{
	return CVarImSlate3DRotLog.GetValueOnAnyThread() != 0;
}
bool IsImSlate3DDragLog()
{
	return CVarImSlate3DDragLog.GetValueOnAnyThread() != 0;
}

// The widget is a flat sheet in the world. We build its world-space basis from the
// placement, then project each local point through the REAL scene camera using the
// exact same math as FSceneView::ProjectWorldToScreen / DeprojectScreenToWorld so
// results line up pixel-for-pixel with the engine's own projection.

void FImProjector::Build(FVector2f InSize, const FImWorldPlacement& Placement,
                         const FMatrix& ViewProj, const FIntRect& ViewRect)
{
	Size = InSize;
	ViewProjMtx = ViewProj;
	InvViewProjMtx = ViewProj.Inverse();
	Rect = ViewRect;

	// Camera world position first (billboard needs it). Recover by deprojecting a near-plane point
	// (z=1 is near in UE's convention).
	const FVector4 NearProj(0.f, 0.f, 1.f, 1.f);
	const FVector4 HG = InvViewProjMtx.TransformFVector4(NearProj);
	CamPos = (HG.W != 0.0) ? FVector(HG.X, HG.Y, HG.Z) / HG.W : FVector(HG.X, HG.Y, HG.Z);  // cached member

	// Sheet basis — three ways:
	//   (a) explicit world axes (no FRotator round-trip);
	//   (b) BILLBOARD: WorldRotation is zero (and no explicit axes) → face the camera every frame
	//       (normal points at the camera; right/down derived from world up). The handy
	//       SetNextWindow3DTransform(loc) overload leaves WorldRotation = ZeroRotator → this path.
	//   (c) fixed orientation from WorldRotation: local X(right)->+Y, Y(down)->-Z, normal(front)->+X.
	FVector WorldRight, WorldDown;
	const bool bBillboard = !Placement.bUseExplicitAxes && Placement.WorldRotation.IsNearlyZero();
	if (Placement.bUseExplicitAxes)
	{
		WorldRight = Placement.ExplicitRight.GetSafeNormal();
		WorldDown  = Placement.ExplicitDown.GetSafeNormal();
		Normal     = Placement.ExplicitNormal.GetSafeNormal();
	}
	else if (bBillboard)
	{
		// Face the camera. Normal = sheet→camera. The basis must match the explicit-axis branch (which feeds
		// CamRight and renders correctly): local +X → screen-right, local +Y → screen-down (= world -Z-ish).
		//   Right = normal × worldUp  (with cam at -X, normal=(-1,0,0): gives +Y = CamRight → no mirror)
		//   Down  = normal × right    (gives -Z → rows grow downward, no upside-down)
		// The earlier `Up × normal` gave -Y → left-right MIRRORED; `right × normal` for down then gave +Z →
		// upside-down. Both axes are fixed here together so the sheet reads exactly like the 2D / explicit case.
		Normal = (CamPos - Placement.WorldLocation).GetSafeNormal();
		if (Normal.IsNearlyZero()) { Normal = FVector(1, 0, 0); }
		FVector Up = FVector(0, 0, 1);
		WorldRight = FVector::CrossProduct(Normal, Up).GetSafeNormal();
		if (WorldRight.IsNearlyZero())  // normal ∥ world up → pick any perpendicular
		{
			WorldRight = FVector::CrossProduct(Normal, FVector(0, 1, 0)).GetSafeNormal();
		}
		WorldDown = FVector::CrossProduct(Normal, WorldRight).GetSafeNormal();
	}
	else
	{
		const FRotationMatrix Rot(Placement.WorldRotation);
		WorldRight = Rot.TransformVector(FVector(0, 1, 0));   // +Y
		WorldDown  = Rot.TransformVector(FVector(0, 0, -1));  // -Z
		Normal     = Rot.TransformVector(FVector(1, 0, 0));   // +X (front)
	}

	// World units per widget px. SPECIAL CASE WorldScale == 0 → SCREEN-SPACE: size the sheet so 1 widget px
	// projects to exactly 1 screen px at the anchor's distance, at any distance (constant on-screen size).
	// Derive world-units-per-screen-px at distance d from the projection: UE perspective M[0][0] = 1/tan(halfFovX),
	// so screen-px-per-world-unit (horizontal) = M[0][0] * (ViewRectW/2) / d. Invert → world units per screen px
	// = d / (M[0][0] * ViewRectW/2). Same factor for both axes (square pixels; M[1][1] mirrors via aspect).
	float EffScale = Placement.WorldScale;
	if (Placement.WorldScale == 0.f)
	{
		// Screen-space: 1 widget px = 1 screen px at the anchor distance (constant on-screen size, any distance).
		EffScale = (float)WorldUnitsPerScreenPx(Placement.WorldLocation);
	}

	RightAxis = WorldRight * EffScale;
	DownAxis  = WorldDown * EffScale;

	Opacity = Placement.Opacity;

	// Facing is decided by the geometric normal (before any back-correction flip, which is render-only).
	// Use the anchor (= pivot's world point = WorldLocation) so it's independent of the pivot offset below.
	bFrontFacing = FVector::DotProduct(Normal, CamPos - Placement.WorldLocation) > 0.0;

	// Back-correction: when facing away, flip RightAxis so local +X still maps to the viewer's right → the
	// content reads correctly from the back (un-mirrored), sheet staying in place. DownAxis/Normal unchanged
	// (only the horizontal axis mirrors). Hit-test + projection both go through RightAxis, so they stay
	// consistent with what's drawn automatically.
	const bool bBackCorrected = (!bFrontFacing && Placement.bCorrectBackface);
	if (bBackCorrected)
	{
		RightAxis = -RightAxis;
	}
	// Content reads right-way-up (→ hittable) when front-facing, or a back that was un-mirrored by correct.
	// A mirrored back (Show) is display-only. Drives IsHittable.
	bContentReadable = bFrontFacing || bBackCorrected;

	// WorldLocation pins the pivot point; back out the world position of local (0,0) using the FINAL RightAxis.
	const FVector PivotOffset =
		RightAxis * (Placement.Pivot.X * Size.X) + DownAxis * (Placement.Pivot.Y * Size.Y);
	Origin = Placement.WorldLocation - PivotOffset;

#if !UE_BUILD_SHIPPING
	// >=3 only: per-frame projector dump (VERY spammy — every Build/paint). Moved above Debug 2 so the
	// event-driven gizmo logs ([arrowDrag]/[arrowHit]/[probe]) at Debug 2 aren't drowned out.
	if (CVarImSlate3DDebug.GetValueOnAnyThread() >= 3)
	{
		auto Px = [this](float lx, float ly) {
			TOptional<FVector2f> s = Project(FVector2f(lx, ly));
			return s.IsSet() ? FString::Printf(TEXT("(%.0f,%.0f)"), s->X, s->Y) : FString(TEXT("<behind>"));
		};
		UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D] Size=%s Origin=%s Right=%s Down=%s Normal=%s Front=%d"),
			*Size.ToString(), *Origin.ToString(), *RightAxis.ToString(), *DownAxis.ToString(), *Normal.ToString(), bFrontFacing ? 1 : 0);
		UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D] screen corners TL%s TR%s BR%s BL%s"),
			*Px(0.f, 0.f), *Px(Size.X, 0.f), *Px(Size.X, Size.Y), *Px(0.f, Size.Y));
	}
#endif
}

FVector FImProjector::LocalToWorld(FVector2f Local) const
{
	return Origin + RightAxis * (double)Local.X + DownAxis * (double)Local.Y;
}

double FImProjector::WorldUnitsPerScreenPx(const FVector& AtWorld) const
{
	// UE perspective: screen-px-per-world-unit (horizontal) at distance d = M[0][0] * (ViewRectW/2) / d.
	// Invert → world units per screen px. Distance = along the camera→point ray. Used for screen-constant size.
	const double Dist = FMath::Max((CamPos - AtWorld).Size(), 1.0);
	const double M00 = FMath::Abs(ViewProjMtx.M[0][0]);
	const double HalfW = FMath::Max((double)Rect.Width() * 0.5, 1.0);
	const double ScreenPxPerWorld = (M00 * HalfW) / Dist;
	return (ScreenPxPerWorld > UE_SMALL_NUMBER) ? (1.0 / ScreenPxPerWorld) : 1.0;
}

// local px -> homogeneous clip (no perspective divide). For the GPU shader path: the VS passes
// this straight to SV_POSITION, so the hardware does the divide + perspective-correct interp.
// ViewProjMtx maps world -> clip in UE's RHI clip convention, which is exactly what SV_POSITION
// expects, so no extra Y-flip / Z-remap is needed here.
FVector4f FImProjector::ProjectToClip(FVector2f Local) const
{
	const FVector World = LocalToWorld(Local);
	const FPlane Clip = ViewProjMtx.TransformFVector4(FVector4(World, 1.f));
	return FVector4f((float)Clip.X, (float)Clip.Y, (float)Clip.Z, (float)Clip.W);
}

// Mirrors FSceneView::ProjectWorldToScreen.
TOptional<FVector2f> FImProjector::WorldToScreen(const FVector& World) const
{
	const FPlane Result = ViewProjMtx.TransformFVector4(FVector4(World, 1.f));
	if (Result.W <= 0.0)
	{
		return TOptional<FVector2f>();  // behind the camera
	}
	const double RHW = 1.0 / Result.W;
	const double NormalizedX = (Result.X * RHW * 0.5) + 0.5;
	const double NormalizedY = 1.0 - (Result.Y * RHW * 0.5) - 0.5;

	const double ScreenX = NormalizedX * (double)Rect.Width() + (double)Rect.Min.X;
	const double ScreenY = NormalizedY * (double)Rect.Height() + (double)Rect.Min.Y;
	return FVector2f((float)ScreenX, (float)ScreenY);
}

TOptional<FVector2f> FImProjector::Project(FVector2f Local) const
{
	return WorldToScreen(LocalToWorld(Local));
}

TOptional<FVector2f> FImProjector::ProjectWorld(const FVector& World) const
{
	return WorldToScreen(World);  // arbitrary world point, no widget-plane clipping
}

bool FImProjector::DeprojectScreenToWorldRay(FVector2f Screen, FVector& OutOrigin, FVector& OutDir) const
{
	// Same deproject as Unproject's first half, but stop at the world ray (no plane intersection).
	const double NormalizedX = (Screen.X - Rect.Min.X) / FMath::Max((double)Rect.Width(), 1.0);
	const double NormalizedY = (Screen.Y - Rect.Min.Y) / FMath::Max((double)Rect.Height(), 1.0);
	const double ScreenSpaceX = (NormalizedX - 0.5) * 2.0;
	const double ScreenSpaceY = ((1.0 - NormalizedY) - 0.5) * 2.0;

	const FVector4 RayStartProj((float)ScreenSpaceX, (float)ScreenSpaceY, 1.0f, 1.0f);
	const FVector4 RayEndProj((float)ScreenSpaceX, (float)ScreenSpaceY, 0.01f, 1.0f);
	const FVector4 HGStart = InvViewProjMtx.TransformFVector4(RayStartProj);
	const FVector4 HGEnd   = InvViewProjMtx.TransformFVector4(RayEndProj);

	FVector RayStart(HGStart.X, HGStart.Y, HGStart.Z);
	FVector RayEnd(HGEnd.X, HGEnd.Y, HGEnd.Z);
	if (HGStart.W != 0.0) RayStart /= HGStart.W;
	if (HGEnd.W != 0.0)   RayEnd /= HGEnd.W;

	OutDir = (RayEnd - RayStart).GetSafeNormal();
	OutOrigin = RayStart;
	return !OutDir.IsNearlyZero();
}

FImPlaneHit FImProjector::MapScreenToPlane(FVector2f Screen) const
{
	FImPlaneHit Out;
	if (!IsHittable())
	{
		return Out;  // culled (Opacity 0) or back-facing → not hit-testable (back is display-only)
	}

	// Screen → world ray (single source of the deproject math; ∩-plane below builds on it).
	FVector RayStart, RayDir;
	if (!DeprojectScreenToWorldRay(Screen, RayStart, RayDir))
	{
		return Out;
	}

	// Intersect ray with the sheet plane (point=Origin, normal=Normal): t = (Origin-Start)·N / (Dir·N)
	const double DdotN = FVector::DotProduct(RayDir, Normal);
	if (FMath::Abs(DdotN) < UE_DOUBLE_KINDA_SMALL_NUMBER)
	{
		return Out;  // ray parallel to sheet
	}
	const double T = FVector::DotProduct(Origin - RayStart, Normal) / DdotN;
	if (T <= 0.0)
	{
		return Out;  // plane behind the ray origin (camera)
	}
	const FVector Hit = RayStart + RayDir * T;

	// Solve Hit = Origin + u*RightAxis + v*DownAxis for (u,v) by projecting onto each axis. Exact when
	// Right ⊥ Down (the projector requires an orthogonal sheet basis; see Build()).
	const FVector Rel = Hit - Origin;
	const double Rr = FVector::DotProduct(RightAxis, RightAxis);
	const double Dd = FVector::DotProduct(DownAxis, DownAxis);
	if (Rr < UE_DOUBLE_SMALL_NUMBER || Dd < UE_DOUBLE_SMALL_NUMBER)
	{
		return Out;
	}
	const double U = FVector::DotProduct(Rel, RightAxis) / Rr;  // = local X (px)
	const double V = FVector::DotProduct(Rel, DownAxis) / Dd;   // = local Y (px)

	Out.bValid      = true;
	Out.World       = Hit;
	Out.Local       = FVector2f((float)U, (float)V);
	// Inside the widget's projected (irregular) quad ⇔ local within [0..Size]. Hit-tests require this; the
	// world/local mapping itself stays valid outside (bValid true, bInsideRect false).
	Out.bInsideRect = (U >= 0.0 && U <= (double)Size.X && V >= 0.0 && V <= (double)Size.Y);
	return Out;
}

TOptional<FVector2f> FImProjector::Unproject(FVector2f Screen) const
{
	// Thin wrapper over the unified mapping: hit-testing returns local only when inside the projected quad.
	const FImPlaneHit Hit = MapScreenToPlane(Screen);
	if (!Hit.bValid || !Hit.bInsideRect)
	{
		return TOptional<FVector2f>();
	}
	return Hit.Local;
}

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
