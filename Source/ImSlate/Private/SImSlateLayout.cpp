// Copyright ImSlate, Inc. All Rights Reserved.
#include "SImSlateLayout.h"

#include "PrivateFieldAccessor.h"
#include "SlotBase.h"
namespace ImSlate
{
#if UE_5_00_OR_LATER
GS_PRIVATEACCESS_MEMBER(FSlotBase, Widget, TSharedRef<SWidget>);
GS_PRIVATEACCESS_MEMBER(FSlotBase, Owner, const FChildren*);
#else
GS_PRIVATEACCESS_MEMBER(FSlotBase, RawParentPtr, SWidget*);
#endif
void ResetSlotBase(FSlotBase* InSlotBase)
{
#if UE_5_00_OR_LATER
	PrivateAccess::Widget(*InSlotBase) = SNullWidget::NullWidget;
	PrivateAccess::Owner(*InSlotBase) = nullptr;
#else
	PrivateAccess::RawParentPtr(*InSlotBase) = nullptr;
#endif
}
}  // namespace ImSlate
