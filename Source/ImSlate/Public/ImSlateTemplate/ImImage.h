// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "ImSlateFactory.h"

//
#include "ImImage.generated.h"

UCLASS(BlueprintType)
class IMSLATE_API UImImage
	: public UImage
	, public TImFactory<SImage>
{
	GENERATED_BODY()
public:
	TSharedRef<SImage> ConstructImWidget() const;

protected:
	IM_SLATE_PALETTECATEGORY()
};
