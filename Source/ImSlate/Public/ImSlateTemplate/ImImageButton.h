// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "Components/Button.h"
#include "Components/Widget.h"
#include "ImSlateFactory.h"
#include "ProtectFieldAccessor.h"
#include "Types/ReflectionMetadata.h"

//
#include "ImImageButton.generated.h"

namespace ImSlate
{

class IMSLATE_API SImageButton : public SButton
{
public:
	virtual FVector2D ComputeDesiredSize(float InSize) const override { return SCompoundWidget::ComputeDesiredSize(InSize); }
};

}  // namespace ImSlate

UCLASS(BlueprintType)
class IMSLATE_API UImImageButton
	: public UButton
	, public TImFactory<ImSlate::SImageButton>
{
	GENERATED_BODY()
public:
	TSharedRef<ImSlate::SImageButton> ConstructImWidget() const;

protected:
	IM_SLATE_PALETTECATEGORY()
};
