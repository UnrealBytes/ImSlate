// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Components/CheckBox.h"
#include "Templates/SharedPointer.h"
#include "ImSlateFactory.h"
#include "Widgets/Input/SCheckBox.h"

#include "ImCheckBox.generated.h"

USTRUCT(BlueprintType)
struct IMSLATE_API FImCheckBoxExtraStyle
{
	GENERATED_USTRUCT_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateBrush DisabledCheckedImage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateBrush DisabledUncheckedImage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateBrush DisabledUndeterminedImage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateBrush FocusedCheckedImage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateBrush FocusedUncheckedImage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateBrush FocusedUndeterminedImage;
};

namespace ImSlate
{

class IMSLATE_API SImCheckBox : public SCheckBox
{
public:
	using FArguments = SCheckBox::FArguments;

	SImCheckBox();

	void Construct(const FArguments& InArgs);

	void SetStyle(const FCheckBoxStyle* InStyle, const FImCheckBoxExtraStyle* InExtraStyle = nullptr);

	void BuildCheckBox(TSharedRef<SWidget> InContent);
	const FSlateBrush* OnGetCheckImage() const;
	bool IsFocused() const { return HasKeyboardFocus(); }

	void SetOnCheckStateChanged(FOnCheckStateChanged InDelegate);

	// Accent (Checked fill) color of the self-painted mark. Default blue; e.g. green to mark an
	// "enable / is-set" toggle distinctly from a regular value checkbox. Rebuilds the mark.
	void SetCheckAccentColor(const FLinearColor& InColor)
	{
		CheckAccentColor = InColor;
		if (ContentContainer.IsValid())
			BuildCheckBox(ContentContainer->GetContent());
	}

	// Our tri-state mark (SImCheckMark) is a self-painted leaf that reads its state in OnPaint via
	// a getter — it has NO TAttribute dependency, so a click (ToggleCheckedState changes the state
	// WITHOUT invalidating anything) wouldn't repaint it. Override the click/touch handlers to
	// invalidate paint after the base toggles, so the mark reflects the new state.
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;

protected:
	const FImCheckBoxExtraStyle* ExtraStyle = nullptr;
	FLinearColor CheckAccentColor = FLinearColor(0.10f, 0.45f, 0.90f, 1.f);  // Checked-fill color (blue default)
};

}  // namespace ImSlate

UCLASS()
class IMSLATE_API UImCheckBox
	: public UCheckBox
	, public TImFactory<ImSlate::SImCheckBox>
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (DisplayName = "Extra Style"))
	FImCheckBoxExtraStyle ExtraStyle;

public:
	TSharedRef<ImSlate::SImCheckBox> ConstructImWidget() const;

	void SynchronizeProperties() override;
	void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	TSharedPtr<ImSlate::SImCheckBox> MyImCheckbox;

	TSharedRef<SWidget> RebuildWidget() override;
};
