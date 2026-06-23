// Copyright Epic Games, Inc. All Rights Reserved.
#include "ImSlate3D.h"

#if defined(IMSLATE3D_API) && WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Math/PerspectiveMatrix.h"

// Automation tests for FImProjector (M1). Run with:
//   UnrealEditor-Win64-DebugGame-Cmd.exe <proj>/CH/Slash.uproject
//     -ExecCmds="Automation RunTests ImSlate.Projector;Quit"
//     -TestExit="Automation Test Queue Empty" -unattended -nopause -nullrhi -nosound -nosplash
//     -log=ImSlate3D.log
// Results in CH/Saved/Logs/ImSlate3D.log (grep "Result={Success}").

namespace ImSlate
{
// A simple, invertible view-projection looking down +X at the origin region.
// We don't need a physically exact engine camera here — round-trip consistency must
// hold for ANY invertible ViewProj, which is exactly what validates Project/Unproject.
// View: world +X(forward)->view +Z, +Y(right)->view +X, +Z(up)->view +Y, eye at (-EyeDist,0,0).
static FMatrix MakeTestViewProj(double EyeDist)
{
	// world -> view (axis remap + eye translation along forward)
	FMatrix View = FMatrix::Identity;
	View.SetAxis(0, FVector(0, 1, 0));   // world Y -> view X (right)
	View.SetAxis(1, FVector(0, 0, 1));   // world Z -> view Y (up)
	View.SetAxis(2, FVector(1, 0, 0));   // world X -> view Z (forward)
	// SetAxis sets rows (basis); translation: move world so eye is at origin, looking +Z.
	View = FTranslationMatrix(FVector(EyeDist, 0, 0)) * View;

	const FPerspectiveMatrix Proj(FMath::DegreesToRadians(45.f), 1280.f, 720.f, 1.f);
	return View * Proj;
}

static bool NearlyEqual(FVector2f A, FVector2f B, float Tol = 0.5f)
{
	return FMath::Abs(A.X - B.X) <= Tol && FMath::Abs(A.Y - B.Y) <= Tol;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImProjectorLocalToWorldTest, "ImSlate.Projector.LocalToWorld",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FImProjectorLocalToWorldTest::RunTest(const FString&)
{
	// Sheet at origin, identity rotation, pivot centre, scale 1: local(0,0)=top-left maps to
	// world (0, -W/2, +H/2) since local X->+Y, local Y->-Z, centred on the anchor.
	FImWorldPlacement Place;
	Place.WorldLocation = FVector::ZeroVector;
	Place.WorldRotation = FRotator::ZeroRotator;
	Place.Pivot = FVector2f(0.5f, 0.5f);
	Place.WorldScale = 1.0f;

	FImProjector P;
	P.Build(FVector2f(400.f, 300.f), Place, MakeTestViewProj(1000.0), FIntRect(0, 0, 1280, 720));

	const FVector Centre = P.LocalToWorld(FVector2f(200.f, 150.f));
	TestTrue(TEXT("centre maps to anchor"), Centre.Equals(FVector::ZeroVector, 0.01));

	const FVector TopLeft = P.LocalToWorld(FVector2f(0.f, 0.f));
	// local X->+Y so left is -Y; local Y->-Z so top is +Z.
	TestTrue(FString::Printf(TEXT("top-left world=%s"), *TopLeft.ToString()),
		TopLeft.Equals(FVector(0, -200, 150), 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImProjectorRoundTripTest, "ImSlate.Projector.RoundTrip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FImProjectorRoundTripTest::RunTest(const FString&)
{
	struct FCase { FRotator Rot; };
	const FCase Cases[] = {
		{ FRotator(0, 0, 0) }, { FRotator(20, 0, 0) }, { FRotator(0, 30, 0) },
		{ FRotator(0, 0, 25) }, { FRotator(15, 25, 10) }, { FRotator(-20, -35, 5) },
	};

	for (const FCase& C : Cases)
	{
		FImWorldPlacement Place;
		Place.WorldRotation = C.Rot;
		FImProjector P;
		P.Build(FVector2f(400.f, 300.f), Place, MakeTestViewProj(1000.0), FIntRect(0, 0, 1280, 720));
		if (!P.IsFrontFacing())
		{
			continue;
		}
		const FVector2f Pts[] = { {50,40}, {200,150}, {350,260}, {100,250}, {300,60} };
		for (const FVector2f& Pt : Pts)
		{
			const TOptional<FVector2f> Screen = P.Project(Pt);
			TestTrue(FString::Printf(TEXT("project %s yields screen"), *Pt.ToString()), Screen.IsSet());
			if (!Screen.IsSet())
			{
				continue;
			}
			const TOptional<FVector2f> Back = P.Unproject(*Screen);
			TestTrue(FString::Printf(TEXT("unproject recovers %s"), *Pt.ToString()), Back.IsSet());
			if (Back.IsSet())
			{
				TestTrue(FString::Printf(TEXT("roundtrip rot(%s) %s -> %s"), *C.Rot.ToString(), *Pt.ToString(), *Back->ToString()),
					NearlyEqual(*Back, Pt));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImProjectorPerspectiveTest, "ImSlate.Projector.Perspective",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FImProjectorPerspectiveTest::RunTest(const FString&)
{
	// Pitch the sheet so its top tilts away from the camera; the top edge must project
	// NARROWER than the bottom edge (true perspective foreshortening, not a parallelogram).
	FImWorldPlacement Place;
	Place.WorldRotation = FRotator(35.f, 0.f, 0.f);  // pitch about world Y
	FImProjector P;
	P.Build(FVector2f(400.f, 300.f), Place, MakeTestViewProj(800.0), FIntRect(0, 0, 1280, 720));

	const TOptional<FVector2f> TL = P.Project(FVector2f(0, 0));
	const TOptional<FVector2f> TR = P.Project(FVector2f(400, 0));
	const TOptional<FVector2f> BL = P.Project(FVector2f(0, 300));
	const TOptional<FVector2f> BR = P.Project(FVector2f(400, 300));
	if (!(TL && TR && BL && BR))
	{
		AddError(TEXT("perspective: a corner failed to project"));
		return false;
	}
	const float TopW = FMath::Abs(TR->X - TL->X);
	const float BotW = FMath::Abs(BR->X - BL->X);
	TestTrue(FString::Printf(TEXT("perspective: top width %g != bottom %g"), TopW, BotW),
		FMath::Abs(TopW - BotW) > 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImProjectorBackFaceTest, "ImSlate.Projector.BackFace",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FImProjectorBackFaceTest::RunTest(const FString&)
{
	// Yaw 180 turns the sheet's front away from the camera: hit-test must be suppressed.
	FImWorldPlacement Place;
	Place.WorldRotation = FRotator(0.f, 180.f, 0.f);
	FImProjector P;
	P.Build(FVector2f(400.f, 300.f), Place, MakeTestViewProj(1000.0), FIntRect(0, 0, 1280, 720));

	TestFalse(TEXT("yaw 180 is back-facing"), P.IsFrontFacing());
	TestFalse(TEXT("back-facing Unproject returns unset"), P.Unproject(FVector2f(640, 360)).IsSet());
	return true;
}

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API) && WITH_DEV_AUTOMATION_TESTS
