// Copyright ImSlate, Inc. All Rights Reserved.

#pragma once

#include "Components/Border.h"
#include "ImSlateFactory.h"

//
#include "ImBorder.generated.h"

UCLASS(BlueprintType)
class IMSLATE_API UImBorder
	: public UBorder
	, public TImFactory<SBorder>
{
	GENERATED_BODY()
public:
	TSharedRef<SBorder> ConstructImWidget() const;

protected:
	IM_SLATE_PALETTECATEGORY()
};
