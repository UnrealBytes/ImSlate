// Copyright Epic Games, Inc. All Rights Reserved.
#include "ImSlate3DHandleShape.h"

#if defined(IMSLATE3D_API)

#include "ImSlate3D.h"               // FImProjector
#include "ImSlate3DProjection.h"     // AppendScreenQuad3D
#include "ImSlate3DShaderElement.h"  // FIm3DQuad
#include "ImSlate3DShapedHit.h"      // PointInTri

namespace ImSlate
{
static FVector4f TintCol(const FLinearColor& C, bool bHovered)
{
	const FLinearColor L = bHovered ? FMath::Lerp(C, FLinearColor(1, 1, 1, C.A), 0.45f) : C;
	return FVector4f(L.R, L.G, L.B, L.A);
}

// ---------------- FImBoxShape ----------------

void FImBoxShape::WorldCorners(const FTransform& Xform, double HandlePx, const FVector& CamPos, FVector OutC[4]) const
{
	// Axes rotated by the gizmo's rotation → the box swings as you rotate. Offsets optionally ×Xform.Scale →
	// the scale handle moves outward as you scale. HalfSize stays screen-constant (HandlePx), not ×Scale.
	const FQuat Q = Xform.GetRotation();
	FVector WU = Q.RotateVector(AxisU);
	FVector WV = Q.RotateVector(AxisV);
	// Offset toward the camera-VISIBLE side of each edge axis (same rule as the arrows' EffectiveRenderAxis): the
	// plane square then sits between the two arrows' VISIBLE halves, and follows them when an arrow flips to its
	// near side. (Half-extents flip with the offset dir so the square stays on that side.)
	const FVector ToCam = CamPos - Xform.GetLocation();
	if (FVector::DotProduct(WU, ToCam) < 0.0) { WU = -WU; }
	if (FVector::DotProduct(WV, ToCam) < 0.0) { WV = -WV; }
	const FVector Sc = Xform.GetScale3D();
	const double OffU = OffsetU * HandlePx * (bScaleByU ? FMath::Abs(FVector::DotProduct(Sc, AxisU.GetAbs())) : 1.0);
	const double OffV = OffsetV * HandlePx * (bScaleByV ? FMath::Abs(FVector::DotProduct(Sc, AxisV.GetAbs())) : 1.0);
	const FVector Centre = Xform.GetLocation() + WU * OffU + WV * OffV;
	const FVector HU = WU * (HalfSizePx * HandlePx);
	const FVector HV = WV * (HalfSizePx * HandlePx);
	OutC[0] = Centre - HU - HV;
	OutC[1] = Centre + HU - HV;
	OutC[2] = Centre + HU + HV;
	OutC[3] = Centre - HU + HV;
}

// Project the 4 world corners to screen; false if any behind camera.
static bool ProjectQuad(const FImProjector& Proj, const FVector C[4], FVector2f Out[4])
{
	for (int32 i = 0; i < 4; ++i)
	{
		const TOptional<FVector2f> S = Proj.ProjectWorld(C[i]);
		if (!S) { return false; }
		Out[i] = *S;
	}
	return true;
}

void FImBoxShape::BuildQuads(const FImProjector& Proj, const FTransform& Xform, double HandlePx,
	const FLinearColor& Color, bool bHovered, TArray<FIm3DQuad>& OutQuads) const
{
	FVector C[4]; WorldCorners(Xform, HandlePx, Proj.GetCamPos(), C);
	FVector2f S[4];
	if (!ProjectQuad(Proj, C, S)) { return; }
	AppendScreenQuad3D(S[0], S[1], S[2], S[3], TintCol(Color, bHovered), OutQuads);
}

bool FImBoxShape::HitTestVP(const FImProjector& Proj, const FTransform& Xform, double HandlePx,
	FVector2f CursorVP) const
{
	FVector C[4]; WorldCorners(Xform, HandlePx, Proj.GetCamPos(), C);
	FVector2f S[4];
	if (!ProjectQuad(Proj, C, S)) { return false; }
	return PointInTri(CursorVP, S[0], S[1], S[2]) || PointInTri(CursorVP, S[0], S[2], S[3]);
}

FBox2D FImBoxShape::GetScreenBoundsVP(const FImProjector& Proj, const FTransform& Xform, double HandlePx) const
{
	FVector C[4]; WorldCorners(Xform, HandlePx, Proj.GetCamPos(), C);
	FVector2f S[4];
	if (!ProjectQuad(Proj, C, S)) { return FBox2D(ForceInit); }
	FBox2D Box(ForceInit);
	for (int32 i = 0; i < 4; ++i) { Box += FVector2D(S[i].X, S[i].Y); }
	return Box.ExpandBy(2.0);
}

// ---------------- FImDiscShape ----------------

bool FImDiscShape::ProjectDisc(const FImProjector& Proj, const FTransform& Xform, double HandlePx,
	FVector2f& OutCentre, TArray<FVector2f>& OutRim) const
{
	const FVector WAxis = Xform.GetRotation().RotateVector(Axis).GetSafeNormal();
	const FVector Sc = Xform.GetScale3D();
	const double Off = OffsetPx * HandlePx * (bScaleByAxis ? FMath::Abs(FVector::DotProduct(Sc, Axis.GetAbs())) : 1.0);
	const FVector Centre = Xform.GetLocation() + WAxis * Off;
	const TOptional<FVector2f> SC = Proj.ProjectWorld(Centre);
	if (!SC) { return false; }
	OutCentre = *SC;
	// In-plane basis ⊥ WAxis for the rim (disc plane normal = WAxis → disc faces along the axis).
	FVector U = FVector::CrossProduct(WAxis, FVector::UpVector);
	if (U.IsNearlyZero()) { U = FVector::CrossProduct(WAxis, FVector::ForwardVector); }
	U = U.GetSafeNormal();
	const FVector V = FVector::CrossProduct(WAxis, U).GetSafeNormal();
	const double R = (double)RadiusPx * HandlePx;
	OutRim.Reset(); OutRim.Reserve(Segments);
	for (int32 k = 0; k < Segments; ++k)
	{
		const double Ang = 2.0 * PI * (double)k / (double)Segments;
		const TOptional<FVector2f> S = Proj.ProjectWorld(Centre + (U * FMath::Cos(Ang) + V * FMath::Sin(Ang)) * R);
		if (!S) { return false; }
		OutRim.Add(*S);
	}
	return true;
}

void FImDiscShape::BuildQuads(const FImProjector& Proj, const FTransform& Xform, double HandlePx,
	const FLinearColor& Color, bool bHovered, TArray<FIm3DQuad>& OutQuads) const
{
	FVector2f C; TArray<FVector2f> Rim;
	if (!ProjectDisc(Proj, Xform, HandlePx, C, Rim) || Rim.Num() < 3) { return; }
	const FVector4f Col = TintCol(Color, bHovered);
	// Triangle fan as degenerate quads (C, Rim[k], Rim[k+1], Rim[k+1]) → solid filled disc.
	for (int32 k = 0; k < Rim.Num(); ++k)
	{
		const FVector2f& A = Rim[k];
		const FVector2f& B = Rim[(k + 1) % Rim.Num()];
		AppendScreenQuad3D(C, A, B, B, Col, OutQuads);
	}
}

bool FImDiscShape::HitTestVP(const FImProjector& Proj, const FTransform& Xform, double HandlePx, FVector2f CursorVP) const
{
	FVector2f C; TArray<FVector2f> Rim;
	if (!ProjectDisc(Proj, Xform, HandlePx, C, Rim) || Rim.Num() < 3) { return false; }
	for (int32 k = 0; k < Rim.Num(); ++k)
	{
		if (PointInTri(CursorVP, C, Rim[k], Rim[(k + 1) % Rim.Num()])) { return true; }
	}
	return false;
}

FBox2D FImDiscShape::GetScreenBoundsVP(const FImProjector& Proj, const FTransform& Xform, double HandlePx) const
{
	FVector2f C; TArray<FVector2f> Rim;
	if (!ProjectDisc(Proj, Xform, HandlePx, C, Rim)) { return FBox2D(ForceInit); }
	FBox2D Box(ForceInit);
	Box += FVector2D(C.X, C.Y);
	for (const FVector2f& P : Rim) { Box += FVector2D(P.X, P.Y); }
	return Box.ExpandBy(2.0);
}

// ---------------- FImRingShape ----------------

bool FImRingShape::ProjectRing(const FImProjector& Proj, const FTransform& Xform, double HandlePx,
	TArray<FVector2f>& OutScreenPts) const
{
	// Rotate the local axis by the gizmo's rotation → the ring turns as you rotate. Two world dirs ⊥ that axis
	// span the circle. (Radius stays screen-constant; rotation only re-orients the disc, doesn't resize it.)
	const FVector WAxis = Xform.GetRotation().RotateVector(Axis).GetSafeNormal();
	FVector U = FVector::CrossProduct(WAxis, FVector::UpVector);
	if (U.IsNearlyZero()) { U = FVector::CrossProduct(WAxis, FVector::ForwardVector); }
	U = U.GetSafeNormal();
	const FVector V = FVector::CrossProduct(WAxis, U).GetSafeNormal();
	const FVector AnchorW = Xform.GetLocation();
	const double R = (double)RadiusPx * HandlePx;
	OutScreenPts.Reset();
	OutScreenPts.Reserve(Segments);
	for (int32 k = 0; k < Segments; ++k)
	{
		const double Ang = 2.0 * PI * (double)k / (double)Segments;
		const FVector P = AnchorW + (U * FMath::Cos(Ang) + V * FMath::Sin(Ang)) * R;
		const TOptional<FVector2f> S = Proj.ProjectWorld(P);
		if (!S) { return false; }  // any point behind camera → bail (ring partly off → don't draw/hit)
		OutScreenPts.Add(*S);
	}
	return true;
}

void FImRingShape::BuildQuads(const FImProjector& Proj, const FTransform& Xform, double HandlePx,
	const FLinearColor& Color, bool bHovered, TArray<FIm3DQuad>& OutQuads) const
{
	TArray<FVector2f> Pts;
	if (!ProjectRing(Proj, Xform, HandlePx, Pts) || Pts.Num() < 2) { return; }
	const FVector4f Col = TintCol(Color, bHovered);
	const float Half = ThicknessPx * 0.5f;
	for (int32 k = 0; k < Pts.Num(); ++k)
	{
		const FVector2f& A = Pts[k];
		const FVector2f& B = Pts[(k + 1) % Pts.Num()];
		FVector2f Dir = B - A;
		if (Dir.SizeSquared() <= UE_SMALL_NUMBER) { continue; }
		Dir.Normalize();
		const FVector2f Perp(-Dir.Y * Half, Dir.X * Half);
		AppendScreenQuad3D(A + Perp, B + Perp, B - Perp, A - Perp, Col, OutQuads);
	}
}

// Distance from point P to segment AB (2D).
static float DistToSeg(const FVector2f& P, const FVector2f& A, const FVector2f& B)
{
	const FVector2f AB = B - A;
	const float L2 = AB.SizeSquared();
	const float T = (L2 > UE_SMALL_NUMBER) ? FMath::Clamp(FVector2f::DotProduct(P - A, AB) / L2, 0.f, 1.f) : 0.f;
	return FVector2f::Distance(P, A + AB * T);
}

bool FImRingShape::HitTestVP(const FImProjector& Proj, const FTransform& Xform, double HandlePx,
	FVector2f CursorVP) const
{
	TArray<FVector2f> Pts;
	if (!ProjectRing(Proj, Xform, HandlePx, Pts) || Pts.Num() < 2) { return false; }
	const float Tol = ThicknessPx * 0.5f + 4.f;  // near-line threshold (band half + a small margin)
	for (int32 k = 0; k < Pts.Num(); ++k)
	{
		if (DistToSeg(CursorVP, Pts[k], Pts[(k + 1) % Pts.Num()]) <= Tol) { return true; }
	}
	return false;
}

FBox2D FImRingShape::GetScreenBoundsVP(const FImProjector& Proj, const FTransform& Xform, double HandlePx) const
{
	TArray<FVector2f> Pts;
	if (!ProjectRing(Proj, Xform, HandlePx, Pts)) { return FBox2D(ForceInit); }
	FBox2D Box(ForceInit);
	for (const FVector2f& P : Pts) { Box += FVector2D(P.X, P.Y); }
	return Box.ExpandBy(ThicknessPx * 0.5 + 4.0);
}

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
