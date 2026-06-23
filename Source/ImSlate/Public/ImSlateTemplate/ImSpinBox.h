// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "Components/SpinBox.h"
#include "ImSlateFactory.h"

#include "ImSpinBox.generated.h"

using ImFactorySpinBox = TImFactory<SSpinBox<float>>;
UCLASS(BlueprintType)
class IMSLATE_API UImSpinBox
	: public USpinBox
	, public ImFactorySpinBox
{
	GENERATED_BODY()
public:
	TSharedRef<SSpinBox<float>> ConstructImWidget() const;

protected:
	IM_SLATE_PALETTECATEGORY()
};
