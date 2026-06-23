// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once
#include "ImCheckBox.h"

#include "ImSingleCheckBox.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCheckBoxReportChange, UImSingleCheckBox*, CheckBox, ECheckBoxState, NewState);

UCLASS()
class IMSLATE_API UImSingleCheckBox
	: public UImCheckBox
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SingleCheckBox")
	FName Tag = TEXT("Default");

	UPROPERTY(BlueprintAssignable, Category = "SingleCheckBox")
	FOnCheckBoxReportChange OnReportChange;

protected:
	TSharedRef<SWidget> RebuildWidget() override;
	void HandleStateChanged(ECheckBoxState NewState);
};