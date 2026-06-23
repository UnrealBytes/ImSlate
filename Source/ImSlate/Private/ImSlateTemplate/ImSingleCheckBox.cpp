// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImSingleCheckBox.h"
#include "ImSlateTemplate/ImCheckBox.h"

TSharedRef<SWidget> UImSingleCheckBox::RebuildWidget()
{
	Super::RebuildWidget();
	MyImCheckbox->SetOnCheckStateChanged(BIND_UOBJECT_DELEGATE(FOnCheckStateChanged, HandleStateChanged));
	return MyImCheckbox.ToSharedRef();
}

void UImSingleCheckBox::HandleStateChanged(ECheckBoxState NewState)
{
	SlateOnCheckStateChangedCallback(NewState);
	OnReportChange.Broadcast(this, NewState);
}
