// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "Slate.h"
#include "SlateCore.h"

#include "Components/TextBlock.h"
#include "ImSlateFactory.h"
#include "Sound/SlateSound.h"

//
#include "ImComboButton.generated.h"

namespace ImSlate
{

class IMSLATE_API IImComboBoxItem : public TSharedFromThis<IImComboBoxItem>
{
protected:
	friend class SImComboBox;
	virtual ~IImComboBoxItem() = default;

public:
	virtual TSharedRef<SWidget> GenWidget() = 0;
	virtual void SelectionChanged(bool bInSelected, ESelectInfo::Type) = 0;
	virtual bool OnMeetConditions(const FText& InFilterText) { return true; }
};

struct IMSLATE_API FComboSourceType
{
	FComboSourceType() = default;
	FComboSourceType(const TArray<TSharedPtr<IImComboBoxItem>>* InOptions)
	{
		Options = InOptions;
		if (Options)
			h = GetHash(*Options);
	};
	FComboSourceType(const TArray<TSharedPtr<IImComboBoxItem>>* InOptions, uint32 InHash)
	{
		Options = InOptions;
		h = InHash;
	};

	const TArray<TSharedPtr<IImComboBoxItem>>* Options = nullptr;
	uint32 h = 0x811c9dd7;

	bool operator==(const FComboSourceType& Rhs) { return h == Rhs.h; }
	void operator=(const FComboSourceType& In)
	{
		Options = In.Options;
		h = In.h;
	}

	static uint32 GetHash(const TArray<TSharedPtr<IImComboBoxItem>>& In);
};

class IMSLATE_API SImComboBox : public SComboButton
{
public:
	typedef TSharedPtr<IImComboBoxItem> DataType;
	typedef SListView<TSharedPtr<IImComboBoxItem>> ListType;

	SLATE_BEGIN_ARGS(SImComboBox)
		: _bHasSearchBox(false)
		, _Content()
		, _ComboBoxStyle(&FCoreStyle::Get().GetWidgetStyle<FComboBoxStyle>("ComboBox"))
		, _ButtonStyle(nullptr)
		, _ItemStyle(&FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row"))
		, _SearchBoxStyle(&FCoreStyle::Get().GetWidgetStyle<FSearchBoxStyle>("SearchBox"))
		, _ContentPadding(FMargin(4.0, 2.0))
		, _ForegroundColor(FCoreStyle::Get().GetSlateColor("InvertedForeground"))
		, _OptionsSource()
		, _InitiallySelectedItem(nullptr)
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
	SLATE_ARGUMENT(FComboSourceType, OptionsSource)
	//SLATE_EVENT( FOnComboBoxOpening, OnComboBoxOpening )
	SLATE_ARGUMENT(TSharedPtr<::SScrollBar>, CustomScrollbar)
	SLATE_ARGUMENT(DataType, InitiallySelectedItem)
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

	/** The ListView that we pop up; visualized the available options. */
	TSharedPtr<ListType> ComboListView;
	/** The Scrollbar used in the ListView. */
	TSharedPtr<::SScrollBar> CustomScrollbar = nullptr;
	TSharedPtr<::SVerticalBox> MenuVerticalBox = nullptr;
	TSharedPtr<::SSearchBox> MenuSearchBox = nullptr;

	FComboSourceType OptionsSource;
	TArray<DataType> OptionsSourceForShow;
	DataType SelectedItem;
	bool bIsSelectedChanged = false;
	float SearchBoxHeight;

public:
	void SetOptionsSoure(const FComboSourceType& InOptionsSource);
	void SetSelectedItem(const DataType& InItem);
	DataType GetSelectedItem() const;
	bool GetAndResetChangedState();
	void SetSearchBoxDisplay(bool bShow);

protected:
	TSharedRef<ITableRow> GenerateMenuItemRow(DataType InItem, const TSharedRef<STableViewBase>& OwnerTable);
	void OnSelectionChanged_Internal(DataType ProposedSelection, ESelectInfo::Type SelectInfo);
	void OnMenuOpenChanged(bool bOpen);
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FPopupMethodReply OnQueryPopupMethod() const override { return FPopupMethodReply::UseMethod(EPopupMethod::CreateNewWindow).SetShouldThrottle(EShouldThrottle::No); }

	void OnFilterTextCommitted(const FText& InFilterText, ETextCommit::Type InCommitType);
	void OnFilterTextChanged(const FText& InFilterText);

	bool IsSameSource(const FComboSourceType& InOptionsSource);
};

}  // namespace ImSlate

UCLASS(BlueprintType)
class IMSLATE_API UImComboButton
	: public UWidget
	, public TImFactory<ImSlate::SImComboBox>
{
	GENERATED_BODY()
public:
	TSharedRef<ImSlate::SImComboBox> ConstructImWidget() const;

	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget();

	void ReSetSearchBoxStyle() const;
	TSharedPtr<SWidget> MyWidgetPtr;

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
	ImSlate::FComboSourceType EmptyOption;

protected:
	IM_SLATE_PALETTECATEGORY()
};
