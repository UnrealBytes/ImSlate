// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "Slate.h"
#include "SlateCore.h"

#include "Components/TextBlock.h"
#include "ImSlateFactory.h"
#include "ImSlateListDataInc.h"
#include "Sound/SlateSound.h"
#include "GMPCore.h"

class SVerticalBox;
class SSearchBox;
class SBorder;
class STextBlock;

namespace ImSlate
{
class FImListDataComboImpl;
class SImSlateVirtualList;
}  // namespace ImSlate

//
#include "ImVirtualComboButton.generated.h"

namespace ImSlate
{

class IMSLATE_API SImVirtualComboBox : public SComboButton
{
public:
	SLATE_BEGIN_ARGS(SImVirtualComboBox)
		: _bHasSearchBox(false)
		, _Content()
		, _ComboBoxStyle(&FCoreStyle::Get().GetWidgetStyle<FComboBoxStyle>("ComboBox"))
		, _ButtonStyle(nullptr)
		, _ItemStyle(&FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row"))
		, _SearchBoxStyle(&FCoreStyle::Get().GetWidgetStyle<FSearchBoxStyle>("SearchBox"))
		, _ContentPadding(FMargin(4.0, 2.0))
		, _ForegroundColor(FCoreStyle::Get().GetSlateColor("InvertedForeground"))
		, _DataStore(nullptr)
		, _Method()
		, _MaxListHeight(300.0f)
		, _SearchBoxHeight(30.0f)
		, _HasDownArrow(true)
		, _IsFocusable(true)
	{
	}

	SLATE_ARGUMENT(bool, bHasSearchBox)
	/** Slot for this button's content (optional) */
	SLATE_DEFAULT_SLOT(FArguments, Content)

	SLATE_STYLE_ARGUMENT(FComboBoxStyle, ComboBoxStyle)
	SLATE_STYLE_ARGUMENT(FButtonStyle, ButtonStyle)
	SLATE_STYLE_ARGUMENT(FTableRowStyle, ItemStyle)
	SLATE_STYLE_ARGUMENT(FSearchBoxStyle, SearchBoxStyle)
	SLATE_ATTRIBUTE(FMargin, ContentPadding)
	SLATE_ATTRIBUTE(FSlateColor, ForegroundColor)
	SLATE_ARGUMENT(TSharedPtr<ImSlate::FImListDataComboImpl>, DataStore)
	SLATE_ARGUMENT(TOptional<EPopupMethod>, Method)

	/** The max height of the combo box menu */
	SLATE_ARGUMENT(float, MaxListHeight)
	SLATE_ARGUMENT(float, SearchBoxHeight)

	/** The sound to play when the button is pressed (overrides ComboBoxStyle) */
	SLATE_ARGUMENT(TOptional<FSlateSound>, PressedSoundOverride)

	/** The sound to play when the selection changes (overrides ComboBoxStyle) */
	SLATE_ARGUMENT(TOptional<FSlateSound>, SelectionChangeSoundOverride)

	/**
		 * When false, the down arrow is not generated and it is up to the API consumer
		 * to make their own visual hint that this is a drop down.
		 */
	SLATE_ARGUMENT(bool, HasDownArrow)

	/** When true, allows the combo box to receive keyboard focus */
	SLATE_ARGUMENT(bool, IsFocusable)

	/** True if this combo's menu should be collapsed when our parent receives focus, false (default) otherwise */
	SLATE_ARGUMENT(bool, CollapseMenuOnParentFocus)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	const FTableRowStyle* ItemStyle;
	const FSearchBoxStyle* SearchBoxStyle;

	/** The Sound to play when the button is pressed */
	FSlateSound PressedSound;
	/** The Sound to play when the selection is changed */
	FSlateSound SelectionChangeSound;

	TSharedPtr<class SXMouseEventBorder> MouseEventBorder = nullptr;
	TSharedPtr<ImSlate::SImSlateVirtualList> VirtualList = nullptr;
	TSharedPtr<ImSlate::FImListDataComboImpl> DataStore = nullptr;
	TSharedPtr<::SVerticalBox> MenuVerticalBox = nullptr;
	TSharedPtr<::SSearchBox> MenuSearchBox = nullptr;
	TSharedPtr<::SBorder> ButtonContent = nullptr;
	TSharedPtr<::SVerticalBox> ButtonContentBox = nullptr;
	TSharedPtr<::STextBlock> ButtonContentBoxText = nullptr;

private:
	float SearchBoxHeight;
	FGMPStructUnion FilterInfo;
	bool bIsValueChanged = false;

public:
	void SetDataStore(const TSharedPtr<ImSlate::FImListDataComboImpl>& InDataStore);
	void SetSearchBoxDisplay(bool bShow);
	void SetEnableSearchBox(bool bEnable);
	void SetCurrentSelectedIndex(int32 InIndex, bool bInSelected);
	int32 GetCurrentSelectedIndex() const;
	bool& GetValueChangedRef() { return bIsValueChanged; };
	auto& GetDataStore() { return DataStore; }

protected:
	void OnMenuOpenChanged(bool bOpen);

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FPopupMethodReply OnQueryPopupMethod() const override { return FPopupMethodReply::UseMethod(EPopupMethod::CreateNewWindow).SetShouldThrottle(EShouldThrottle::No); }

	void OnFilterTextCommitted(const FText& InFilterText, ETextCommit::Type InCommitType);
	void OnFilterTextChanged(const FText& InFilterText);
};

}  // namespace ImSlate

UCLASS(BlueprintType)
class IMSLATE_API UImVirtualComboButton
	: public UWidget
	, public TImFactory<ImSlate::SImVirtualComboBox>
{
	GENERATED_BODY()
public:
	TSharedRef<ImSlate::SImVirtualComboBox> ConstructImWidget() const;

	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget();

	void ReSetSearchBoxStyle() const;

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FComboBoxStyle ComboBoxStyle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FButtonStyle ButtonStyle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FTableRowStyle ItemStyle;

	// Style for search box
	mutable FSearchBoxStyle SearchBoxStyle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SearchBoxAppearance)
	FEditableTextBoxStyle TextBoxStyle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SearchBoxAppearance)
	FSlateFontInfo ActiveFontInfo;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SearchBoxAppearance)
	FSlateBrush UpArrowImage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SearchBoxAppearance)
	FSlateBrush DownArrowImage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SearchBoxAppearance)
	FSlateBrush GlassImage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SearchBoxAppearance)
	FSlateBrush ClearImage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SearchBoxAppearance)
	FMargin ImagePadding;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SearchBoxAppearance)
	bool bLeftAlignButtons;
	//

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Interaction)
	bool bIsFocusable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bHasSearchBox = false;

protected:
	TSharedPtr<ImSlate::SImVirtualComboBox> MyComboBox;

protected:
	IM_SLATE_PALETTECATEGORY()
};


USTRUCT(BlueprintType)
struct IMSLATE_API FComboSearchType
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText FilterText;
};