// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "SlateCore.h"

#include "Components/EditableTextBox.h"
#include "ImSlateFactory.h"
#include "Widgets/Input/SEditableTextBox.h"

#include "ImEditableTextBox.generated.h"

namespace ImSlate
{

class IMSLATE_API SImEditableTextBox : public SEditableTextBox
{
public:
	using FArguments = SEditableTextBox::FArguments;
	void Construct(const FArguments& InArgs);
	void SetBackgroundImageError(const FSlateBrush* ImageError);
	bool bInErrorState = false;

protected:
	const FSlateBrush* BorderImageError = nullptr;
	const FSlateBrush* GetBorderImage() const;
};

}  // namespace ImSlate

UCLASS(BlueprintType)
class IMSLATE_API UImEditableTextBox
	: public UEditableTextBox
	, public TImFactory<SEditableTextBox>
{
	GENERATED_BODY()
public:
	TSharedRef<SEditableTextBox> ConstructImWidget() const;

protected:
	IM_SLATE_PALETTECATEGORY()
};
