// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "Components/TextBlock.h"
#include "ImSlateFactory.h"

//
#include "ImText.generated.h"

UCLASS(BlueprintType)
class IMSLATE_API UImTextBlock
	: public UTextBlock
	, public TImFactory<STextBlock>
{
	GENERATED_BODY()
public:
	TSharedRef<STextBlock> ConstructImWidget() const;

protected:
	IM_SLATE_PALETTECATEGORY()
};
