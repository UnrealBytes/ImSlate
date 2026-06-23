// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once
#include "Slate.h"

#include "Layout/Children.h"
#include "UnrealCompatibility.h"
#include "PrivateFieldAccessor.h"

namespace ImSlate
{
namespace ImSlateInternal
{
	GS_PRIVATEACCESS_MEMBER(FChildren, Owner, SWidget*)
}  // namespace ImSlateInternal

void ResetSlotBase(FSlotBase* InSlot);

template<typename SlotType>
class TChildrenLayout : public FChildren
{
public:
	virtual const FSlotBase& GetSlotAt(int32 ChildIndex) const override { return Slots[ChildIndex]; }
	TChildrenLayout(SWidget* InOwner)
		: FChildren(InOwner)
		, bEmptying(false)
	{
	}
	TChildrenLayout& operator=(TChildrenLayout&& Other)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		GetOwnerPtr() = Other.GetOwnerPtr();
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		bEmptying = Other.bEmptying;
		Slots = MoveTemp(Other.Slots);
		return *this;
	}

	virtual int32 Num() const override { return Slots.Num(); }

	virtual TSharedRef<const SWidget> GetChildAt(int32 Index) const override { return Slots[Index].GetWidget(); }
	virtual TSharedRef<SWidget> GetChildAt(int32 Index) override { return Slots[Index].GetWidget(); }
#if ENGINE_MAJOR_VERSION >= 5
	virtual FWidgetRef GetChildRefAt(int32 Index) override { return FWidgetRef(ReferenceConstruct, GetChildAt(Index).Get()); }
	virtual FConstWidgetRef GetChildRefAt(int32 Index) const override { return FConstWidgetRef(ReferenceConstruct, GetChildAt(Index).Get()); }
#endif

	int32 Insert(SlotType* Slot, int32 Index = 0)
	{
		if (bEmptying)
		{
			return INDEX_NONE;
		}

		Slots.Insert(Slot, Index);

		// Don't do parent manipulation if this panel has no owner.
		if (GetOwnerPtr())
		{
#if UE_5_00_OR_LATER
			Slot->SetOwner(*this);
#else
			Slot->AttachWidgetParent(GetOwnerPtr());
#endif
		}
		return Index;
	}

	int32 Add(SlotType* Slot) { return Insert(Slot, Slots.Num()); }

	void RemoveAt(int32 Index)
	{
		if (!bEmptying && ensure(Slots.IsValidIndex(Index)))
		{
			Slots.RemoveAt(Index);
		}
	}
	bool RemoveFirst(int32 InCnt = 1)
	{
		bool bRet = false;
		if (!bEmptying && ensure(InCnt > 0))
		{
			if (Slots.Num() <= InCnt)
			{
				Empty();
			}
			else
			{
				Slots.RemoveAt(0, InCnt);
			}
			bRet = true;
		}
		return bRet;
	}

	bool RemoveLast(int32 InCnt = 1)
	{
		bool bRet = false;
		if (!bEmptying && ensure(InCnt > 0))
		{
			if (Slots.Num() <= InCnt)
			{
				Empty();
			}
			else
			{
				Slots.RemoveAt(Slots.Num() - InCnt, InCnt);
			}
			bRet = true;
		}
		return bRet;
	}

	void Empty()
	{
		if (!bEmptying)
		{
			TGuardValue<bool> GuardEmptying(bEmptying, true);
			TIndirectArray<SlotType> ChildrenCopy = MoveTemp(Slots);
			Slots.Empty();
		}
	}
	void ResetSlots(bool bResetOwner = false)
	{
		if (!bEmptying)
		{
			TGuardValue<bool> GuardEmptying(bEmptying, true);
			TIndirectArray<SlotType> ChildrenCopy = MoveTemp(Slots);
			for (auto& Child : ChildrenCopy)
			{
				ResetSlotBase(&Child);
			}
			Slots.Empty();
		}
		if (bResetOwner)
			GetOwnerPtr() = nullptr;
	}

	void Move(int32 IndexToMove, int32 IndexToDestination)
	{
		check(IndexToMove != IndexToDestination && Slots.IsValidIndex(IndexToMove) && Slots.IsValidIndex(IndexToDestination));

		auto Delta = IndexToMove < IndexToDestination ? 1 : -1;
		auto Left = Delta * (IndexToDestination - IndexToMove);
		for (auto i = IndexToMove; Left > 0; i += Delta)
		{
			--Left;
			Slots.Swap(i, i + Delta);
		}

		if (GetOwnerPtr())
		{
			GetOwnerPtr()->Invalidate(EInvalidateWidget::ChildOrder);
		}
	}

	void Reserve(int32 NumToReserve) { Slots.Reserve(NumToReserve); }

	bool IsValidIndex(int32 Index) const { return Slots.IsValidIndex(Index); }

	const SlotType& operator[](int32 Index) const { return Slots[Index]; }
	SlotType& operator[](int32 Index) { return Slots[Index]; }
	const SlotType& Last() const { return Slots.Last(); }
	SlotType& Last() { return Slots.Last(); }
	const SlotType& First() const { return Slots[0]; }
	SlotType& First() { return Slots[0]; }

	void Swap(int32 IndexA, int32 IndexB)
	{
		Slots.Swap(IndexA, IndexB);
		if (GetOwnerPtr())
		{
			GetOwnerPtr()->Invalidate(EInvalidateWidget::ChildOrder);
		}
	}

	TIndirectArray<SlotType> Slots;

private:
	bool bEmptying;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	SWidget*& GetOwnerPtr() { return ImSlateInternal::PrivateAccess::Owner(*this); }
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
};

template<typename ChildType>
class TImSlateChildren : public FChildren
{
private:
	virtual const FSlotBase& GetSlotAt(int32 ChildIndex) const override
	{
		// @todo slate : slotless children should be removed altogether; for now they return a fake slot.
		static FSlotBase NullSlot;
		return NullSlot;
	}

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	SWidget*& GetOwnerPtr() { return ImSlateInternal::PrivateAccess::Owner(*this); }
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	friend class SImSlateViewport;

public:
	TImSlateChildren(SWidget* InOwner, bool InbChangesInvalidatePrepass = true)
		: FChildren(InOwner)
		, bChangesInvalidatePrepass(InbChangesInvalidatePrepass)
	{
	}

	virtual int32 Num() const override { return Widgets.Num(); }

	virtual TSharedRef<SWidget> GetChildAt(int32 Index) override { return Widgets[Index]; }
	virtual TSharedRef<const SWidget> GetChildAt(int32 Index) const override { return Widgets[Index]; }
#if ENGINE_MAJOR_VERSION >= 5
	virtual FWidgetRef GetChildRefAt(int32 Index) override { return FWidgetRef(ReferenceConstruct, GetChildAt(Index).Get()); }
	virtual FConstWidgetRef GetChildRefAt(int32 Index) const override { return FConstWidgetRef(ReferenceConstruct, GetChildAt(Index).Get()); }
#endif
	int32 Add(const TSharedRef<ChildType>& Child)
	{
		if (GetOwnerPtr() && bChangesInvalidatePrepass)
		{
			GetOwnerPtr()->Invalidate(EInvalidateWidget::ChildOrder);
		}

		int32 Index = Widgets.Add(Child);

		if (GetOwnerPtr())
		{
			if (Child != SNullWidget::NullWidget)
			{
				Child->AssignParentWidget(GetOwnerPtr()->AsShared());
			}
		}

		return Index;
	}

	void Reset(int32 NewSize = 0)
	{
		for (int ChildIndex = 0; ChildIndex < Widgets.Num(); ChildIndex++)
		{
			TSharedRef<SWidget> Child = GetChildAt(ChildIndex);
			if (Child != SNullWidget::NullWidget)
			{
				Child->ConditionallyDetatchParentWidget(GetOwnerPtr());
			}
		}

		Widgets.Reset(NewSize);
	}

	void Empty()
	{
		for (int ChildIndex = 0; ChildIndex < Widgets.Num(); ChildIndex++)
		{
			TSharedRef<SWidget> Child = GetChildAt(ChildIndex);
			if (Child != SNullWidget::NullWidget)
			{
				Child->ConditionallyDetatchParentWidget(GetOwnerPtr());
			}
		}

		Widgets.Empty();
	}

	void Insert(const TSharedRef<ChildType>& Child, int32 Index)
	{
		if (GetOwnerPtr() && bChangesInvalidatePrepass)
		{
			GetOwnerPtr()->Invalidate(EInvalidateWidget::ChildOrder);
		}

		Widgets.Insert(Child, Index);

		if (GetOwnerPtr())
		{
			if (Child != SNullWidget::NullWidget)
			{
				Child->AssignParentWidget(GetOwnerPtr()->AsShared());
			}
		}
	}

	int32 Remove(const TSharedRef<ChildType>& Child)
	{
		if (Child != SNullWidget::NullWidget)
		{
			Child->ConditionallyDetatchParentWidget(GetOwnerPtr());
		}

		const int32 NumFoundAndRemoved = Widgets.Remove(Child);
		return NumFoundAndRemoved;
	}

	void RemoveAt(int32 Index)
	{
		TSharedRef<SWidget> Child = GetChildAt(Index);
		if (Child != SNullWidget::NullWidget)
		{
			Child->ConditionallyDetatchParentWidget(GetOwnerPtr());
		}

		Widgets.RemoveAt(Index);
	}

	int32 Find(const TSharedRef<ChildType>& Item) const { return Widgets.Find(Item); }

	TArray<TSharedRef<ChildType>> AsArrayCopy() const { return Widgets; }

	const TSharedRef<ChildType>& operator[](int32 Index) const { return Widgets[Index]; }
	TSharedRef<ChildType>& operator[](int32 Index) { return Widgets[Index]; }

	void Move(int32 IndexToMove, int32 IndexToDestination)
	{
		check(IndexToMove != IndexToDestination && Widgets.IsValidIndex(IndexToMove) && Widgets.IsValidIndex(IndexToDestination));

		auto Delta = IndexToMove < IndexToDestination ? 1 : -1;
		auto Left = Delta * (IndexToDestination - IndexToMove);
		for (auto i = IndexToMove; Left > 0; i += Delta)
		{
			--Left;
			Swap(i, i + Delta);
		}

		if (GetOwnerPtr() && bChangesInvalidatePrepass)
		{
			GetOwnerPtr()->Invalidate(EInvalidateWidget::ChildOrder);
		}
	}

	template<class PREDICATE_CLASS>
	void Sort(const PREDICATE_CLASS& Predicate)
	{
		Widgets.Sort(Predicate);
		if (GetOwnerPtr() && bChangesInvalidatePrepass)
		{
			GetOwnerPtr()->Invalidate(EInvalidateWidget::ChildOrder);
		}
	}

	void Swap(int32 IndexA, int32 IndexB)
	{
		Widgets.Swap(IndexA, IndexB);
		if (GetOwnerPtr() && bChangesInvalidatePrepass)
		{
			GetOwnerPtr()->Invalidate(EInvalidateWidget::ChildOrder);
		}
	}

private:
	TArray<TSharedRef<ChildType>> Widgets;
	bool bChangesInvalidatePrepass;
};

}  // namespace ImSlate
