// Copyright Epic Games, Inc. All Rights Reserved.
#include "ImSlate3DHitManager.h"

#if defined(IMSLATE3D_API)

namespace ImSlate
{
FImSlate3DHitManager& FImSlate3DHitManager::Get()
{
	static FImSlate3DHitManager Instance;  // game-thread singleton
	return Instance;
}

void FImSlate3DHitManager::Register(const TWeakPtr<IImSlate3DHittable>& Hittable)
{
	TSharedPtr<IImSlate3DHittable> Pinned = Hittable.Pin();
	if (!Pinned.IsValid())
	{
		return;
	}
	Compact();
	// Avoid duplicate registration.
	for (const TWeakPtr<IImSlate3DHittable>& It : Registered)
	{
		if (It.Pin().Get() == Pinned.Get())
		{
			return;
		}
	}
	// Order is resolved at dispatch time (EnsureSortedForDispatch), not here — a unit's overlap order can
	// change after registration (e.g. a 3D window raised to front), so append and let the sort handle it.
	Registered.Add(Hittable);
	if (IsImSlate3DDebug())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][register] unit=%p order=%d → total=%d"),
			Pinned.Get(), Pinned->GetOverlapOrder(), Registered.Num());
	}
}

void FImSlate3DHitManager::EnsureSortedForDispatch() const
{
	// DESCENDING overlap order (higher = on top = checked first). Stable so equal-order units keep their
	// registration order. Done per-dispatch (unit count is tiny — a few 3D windows) so a runtime order change
	// takes effect immediately without callers having to poke the manager.
	Registered.StableSort([](const TWeakPtr<IImSlate3DHittable>& A, const TWeakPtr<IImSlate3DHittable>& B)
	{
		TSharedPtr<IImSlate3DHittable> PA = A.Pin();
		TSharedPtr<IImSlate3DHittable> PB = B.Pin();
		if (!PA.IsValid()) { return false; }
		if (!PB.IsValid()) { return true; }
		return PA->GetOverlapOrder() > PB->GetOverlapOrder();
	});
}

void FImSlate3DHitManager::Unregister(const IImSlate3DHittable* Hittable)
{
	Registered.RemoveAll([Hittable](const TWeakPtr<IImSlate3DHittable>& It)
	{
		TSharedPtr<IImSlate3DHittable> Pinned = It.Pin();
		return !Pinned.IsValid() || Pinned.Get() == Hittable;
	});
}

void FImSlate3DHitManager::Compact()
{
	Registered.RemoveAll([](const TWeakPtr<IImSlate3DHittable>& It) { return !It.IsValid(); });
}

// Broad-phase AABB reject: true if the cursor (CursorVP, viewport px) is OUTSIDE this unit's screen bounds
// (→ skip per-pixel). A unit that is actively dragging is never rejected (it must keep handling even
// off-bounds). An invalid (empty) box means "no broad phase" → never reject.
static bool BroadPhaseReject(IImSlate3DHittable& Unit, FVector2f CursorVP)
{
	if (Unit.IsActivelyDragging())
	{
		return false;
	}
	const FBox2D Bounds = Unit.GetScreenBoundsVP();
	if (!Bounds.bIsValid)
	{
		return false;
	}
	return !Bounds.IsInside(FVector2D(CursorVP.X, CursorVP.Y));
}

FReply FImSlate3DHitManager::HandleEvent(const FPointerEvent& Event, FVector2f CursorVP, const TFunctionRef<FReply(IImSlate3DHittable&, FVector2f)>& Callback)
{
	// Refresh the single global hover state from the live cursor on EVERY event (move/down/up). Doing it here,
	// in the one place all entries funnel through, is why hover can't desync or leave residue.
	UpdateGlobalHover(Event, CursorVP);

	FReply FinalReply = FReply::Unhandled();
	EnsureSortedForDispatch();
	const bool bDbg = IsImSlate3DDebug();
	int32 NumUnits = 0, NumRejected = 0, NumHandled = 0, Idx = 0;
	// Descending overlap order: first pixel-hit wins; later (lower) ones see OutReply handled and bow out.
	for (const TWeakPtr<IImSlate3DHittable>& It : Registered)
	{
		if (TSharedPtr<IImSlate3DHittable> Pinned = It.Pin())
		{
			++NumUnits;
			const bool bReject = !FinalReply.IsEventHandled() && BroadPhaseReject(*Pinned, CursorVP);
			if (bReject) { ++NumRejected; ++Idx; continue; }
			const bool bWasHandled = FinalReply.IsEventHandled();
			Pinned->HandleHitEvent(FinalReply, Event, CursorVP, Callback);
			if (!bWasHandled && FinalReply.IsEventHandled()) { ++NumHandled; }
			++Idx;
		}
	}
	if (bDbg)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][dispatch] units=%d rejected=%d handled=%d finalHandled=%d cursorVP=(%.1f,%.1f)"),
			NumUnits, NumRejected, NumHandled, FinalReply.IsEventHandled() ? 1 : 0, CursorVP.X, CursorVP.Y);
	}
	return FinalReply;
}

// Walk units in overlap order; the front-most pixel-hit one fills OutPath with its hovered widget chain.
// Returns true if any unit hit. Shared by hover recompute + wheel routing.
bool FImSlate3DHitManager::BuildFrontmostHoverPath(FVector2f CursorVP, TArray<FArrangedWidget>& OutPath) const
{
	EnsureSortedForDispatch();
	for (const TWeakPtr<IImSlate3DHittable>& It : Registered)
	{
		if (TSharedPtr<IImSlate3DHittable> Pinned = It.Pin())
		{
			if (!BroadPhaseReject(*Pinned, CursorVP) && Pinned->BuildHoverPath(CursorVP, OutPath))
			{
				return true;  // front-most hit owns it
			}
			OutPath.Reset();  // a non-hitting unit may have appended then returned false — discard
		}
	}
	return false;
}

void FImSlate3DHitManager::UpdateGlobalHover(const FPointerEvent& Event, FVector2f CursorVP)
{
	const uint32 PointerIndex = Event.GetPointerIndex();

	// Recompute this pointer's hovered chain from the live cursor (front-most pixel-hit unit; empty if none).
	TArray<FArrangedWidget> NewPath;
	BuildFrontmostHoverPath(CursorVP, NewPath);

	// Set-diff vs THIS pointer's stored path. Pin() guards destroyed widgets. A parent stays hovered while the
	// cursor is over its child (both in path). Each finger keeps an independent path → no cross-pointer residue.
	TArray<TWeakPtr<SWidget>>& OldPath = HoveredByPointer.FindOrAdd(PointerIndex);
	auto InNew = [&NewPath](const SWidget* W) -> bool
	{
		for (const FArrangedWidget& A : NewPath) { if (&A.Widget.Get() == W) { return true; } }
		return false;
	};
	int32 NumLeave = 0, NumEnter = 0;
	for (const TWeakPtr<SWidget>& OldW : OldPath)
	{
		TSharedPtr<SWidget> Old = OldW.Pin();
		if (Old.IsValid() && !InNew(Old.Get())) { Old->OnMouseLeave(Event); ++NumLeave; }
	}
	for (const FArrangedWidget& A : NewPath)
	{
		bool bWas = false;
		for (const TWeakPtr<SWidget>& OldW : OldPath)
		{
			if (OldW.Pin().Get() == &A.Widget.Get()) { bWas = true; break; }
		}
		if (!bWas) { A.Widget->OnMouseEnter(A.Geometry, Event); ++NumEnter; }
	}

	if (IsImSlate3DDebug() && (NumLeave > 0 || NumEnter > 0))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][hover] UpdateGlobalHover ptr=%u old=%d new=%d → leave=%d enter=%d"),
			PointerIndex, OldPath.Num(), NewPath.Num(), NumLeave, NumEnter);
	}

	OldPath.Reset();
	for (const FArrangedWidget& A : NewPath) { OldPath.Add(A.Widget); }
	if (OldPath.Num() == 0) { HoveredByPointer.Remove(PointerIndex); }  // empty bucket → drop (finger lifted / off)
}

bool FImSlate3DHitManager::IsWidgetHovered(const SWidget* Widget) const
{
	if (Widget == nullptr) { return false; }
	for (const TPair<uint32, TArray<TWeakPtr<SWidget>>>& Bucket : HoveredByPointer)
	{
		for (const TWeakPtr<SWidget>& W : Bucket.Value)
		{
			if (W.Pin().Get() == Widget) { return true; }
		}
	}
	return false;
}

void FImSlate3DHitManager::ClearHover(const FPointerEvent& Event)
{
	const uint32 PointerIndex = Event.GetPointerIndex();
	TArray<TWeakPtr<SWidget>>* OldPath = HoveredByPointer.Find(PointerIndex);
	if (!OldPath || OldPath->Num() == 0) { return; }
	for (const TWeakPtr<SWidget>& W : *OldPath)
	{
		if (TSharedPtr<SWidget> P = W.Pin()) { P->OnMouseLeave(Event); }
	}
	HoveredByPointer.Remove(PointerIndex);
	if (IsImSlate3DDebug())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][hover] ClearHover ptr=%u (pointer left 3D entry)"), PointerIndex);
	}
}

void FImSlate3DHitManager::ClearAllHover()
{
	for (const TPair<uint32, TArray<TWeakPtr<SWidget>>>& Bucket : HoveredByPointer)
	{
		for (const TWeakPtr<SWidget>& W : Bucket.Value)
		{
			if (TSharedPtr<SWidget> P = W.Pin()) { P->OnMouseLeave(FPointerEvent()); }
		}
	}
	HoveredByPointer.Reset();
}

FReply FImSlate3DHitManager::HandleWheel(const FPointerEvent& Event, FVector2f CursorVP)
{
	// Wheel = one-shot hit-and-dispatch (no hover, no capture). Walk units overlap-order; the front-most
	// pixel-hit one bubbles the wheel through its OWN hit chain (Slate parity — nested ScrollBox scrolls).
	EnsureSortedForDispatch();
	for (const TWeakPtr<IImSlate3DHittable>& It : Registered)
	{
		if (TSharedPtr<IImSlate3DHittable> Pinned = It.Pin())
		{
			if (BroadPhaseReject(*Pinned, CursorVP)) { continue; }
			if (!Pinned->IsPixelHovered(Event, CursorVP)) { continue; }
			FReply R = Pinned->OnHitMouseWheel(Event, CursorVP);
			if (R.IsEventHandled()) { return R; }
			// front-most unit was hit but didn't consume → still don't fall to LOWER 3D units (Slate stops at
			// the front-most hit widget tree); return Unhandled so it passes to 2D below, not to a back window.
			return FReply::Unhandled();
		}
	}
	return FReply::Unhandled();  // nothing under the wheel → fall through to lower 2D widgets
}

bool FImSlate3DHitManager::AnyPixelHovered(const FPointerEvent& Event, FVector2f CursorVP) const
{
	EnsureSortedForDispatch();
	for (const TWeakPtr<IImSlate3DHittable>& It : Registered)
	{
		if (TSharedPtr<IImSlate3DHittable> Pinned = It.Pin())
		{
			if (!BroadPhaseReject(*Pinned, CursorVP) && Pinned->IsPixelHovered(Event, CursorVP))
			{
				return true;
			}
		}
	}
	return false;
}

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
