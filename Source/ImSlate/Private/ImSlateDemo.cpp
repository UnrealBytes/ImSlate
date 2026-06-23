// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateDemo.h"

//
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "GMPCore.h"
#include "GameFramework/Pawn.h"
#include "GenericSingletons.h"
#include "ImSlate.h"
#include "ImListDataComboImpl.h"
#include "ImSlateTemplates.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "XConsoleManager.h"
#if WITH_EDITOR
#include "Editor.h"
#endif

FXConsoleCommandLambdaFull XVar_ImSlateDemo(TEXT("imslate.ShowDemo"), TEXT("imslate.ShowDemo bIsOpen"), [](TOptional<bool> bOpen, UWorld* InWorld, FOutputDevice& Ar) {
	do
	{
		if (!InWorld)
			break;
		UGenericSingletons::GetSingleton<UImSlateDemo>(InWorld)->EnableTick(bOpen);
	} while (false);
});

UImSlateDemo::UImSlateDemo()
{
	//SetRetriggerInstancedAbility();
}

namespace ImSpinBoxNS
{
typedef SSpinBox<float> SSpinBoxFloat;
GS_PRIVATEACCESS_MEMBER(SSpinBoxFloat, OnBeginSliderMovement, FSimpleDelegate);
GS_PRIVATEACCESS_MEMBER(SSpinBoxFloat, OnEndSliderMovement, SSpinBoxFloat::FOnValueChanged);
GS_PRIVATEACCESS_MEMBER(SSpinBoxFloat, OnValueChanged, SSpinBoxFloat::FOnValueChanged);
GS_PRIVATEACCESS_MEMBER(SSpinBoxFloat, OnValueCommitted, SSpinBoxFloat::FOnValueCommitted);
}  // namespace ImSpinBoxNS

static bool DefaultTextFilter(const FText& InFilterText, const FString& InOwnName)
{
	if (InFilterText.IsEmpty())
		return true;
	auto FilterStr = InFilterText.ToString();
	TArray<FString> SplitedStr;
	FilterStr.ParseIntoArray(SplitedStr, TEXT(" "));
	for (auto& Str : SplitedStr)
	{
		if (!InOwnName.Contains(Str))
		{
			return false;
		}
	}

	return true;
}

void UImSlateDemo::StartShow()
{
	BindingData = MakeShared<ImSlate::TImSlateListArray<TSharedPtr<ImSlate::FImListViewItemBase>>>(false);
	BindingData->SetOnBindingData([](int32 DataIndex, TSharedPtr<SWidget> WidgetRef, TSharedPtr<ImSlate::FImListViewItemBase>& DataRef) {
		if (!DataRef.IsValid())
			return;

		if (auto ItemText = DataRef->As<ImSlate::FImListViewItemText>())
		{
			auto DataWidget = StaticCastSharedPtr<STextBlock>(WidgetRef);
			DataWidget->SetText(FText::FromString(ItemText->Text));
		}
		else if (auto EditableText = DataRef->As<ImSlate::FImListViewItemEditableText>())
		{
			auto DataWidget = StaticCastSharedPtr<SEditableText>(WidgetRef);
			DataWidget->SetText(FText::FromString(EditableText->EditText));
		}
		else if (auto ItemFloat = DataRef->As<ImSlate::FImListViewItemFloat>())
		{
			auto DataWidget = StaticCastSharedPtr<SSpinBox<float>>(WidgetRef);
			DataWidget->SetValue(ItemFloat->Val);
		}
	});
	BindingData->SetWidgetFactory([](TSharedPtr<ImSlate::FImListViewItemBase>& InData, TSharedRef<SWidget>& InOutWidget) {
		if (auto ItemButton = InData->As<ImSlate::FImListViewItemButton>())
		{
			if (InOutWidget->GetType() == FName("SButton"))
			{
				// re-use?
			}

			TSharedRef<SButton> WidgetRef = ImSlate::ImFactoryCreate<UImButton>();
			WidgetRef->SetOnClicked(FOnClicked::CreateLambda([]() -> FReply {
				FGMPHelper::NotifyWorldMessage(GImSlate->GetWorldChecked(), MSGKEY("ImSlateDemo.ShowToast"), NSLOCTEXT("VirualList_Test", "FImListViewItemButtonClick", "FImListViewItemButtonClick"), FName("Info"));
				return FReply::Handled();
			}));

			InOutWidget = WidgetRef;
		}
		else if (auto TextButton = InData->As<ImSlate::FImListViewItemTextButton>())
		{
			TSharedRef<SButton> WidgetRef = ImSlate::ImFactoryCreate<UImButton>();
			WidgetRef->SetContent(SNew(STextBlock).Text(FText::FromString(TextButton->ButtonText)).Clipping(EWidgetClipping::ClipToBoundsAlways));
			WidgetRef->SetOnClicked(FOnClicked::CreateLambda([]() -> FReply {
				FGMPHelper::NotifyWorldMessage(GImSlate->GetWorldChecked(), MSGKEY("ImSlateDemo.ShowToast"), NSLOCTEXT("VirualList_Test", "FImListViewItemTextButtonClick", "FImListViewItemTextButtonClick"), FName("Info"));
				return FReply::Handled();
			}));

			InOutWidget = WidgetRef;
		}
		else if (auto ItemText = InData->As<ImSlate::FImListViewItemText>())
		{
			TSharedRef<STextBlock> WidgetRef = ImSlate::ImFactoryCreate<UImTextBlock>();
			WidgetRef->SetText(FText::FromString(ItemText->Text));

			InOutWidget = WidgetRef;
		}
		else if (auto EditableText = InData->As<ImSlate::FImListViewItemEditableText>())
		{
			TSharedRef<SEditableText> WidgetRef = ImSlate::ImFactoryCreate<UImEditableText>();
			WidgetRef->SetText(FText::FromString(EditableText->EditText));
			GS_ACCESS_PROTECT(WidgetRef, SEditableText, OnTextCommittedCallback)->OnTextCommittedCallback = FOnTextCommitted::CreateLambda([](const FText& InCommitText, ETextCommit::Type InTyp) -> void {
				if (InTyp != ETextCommit::OnCleared)
				{
					FGMPHelper::NotifyWorldMessage(GImSlate->GetWorldChecked(), MSGKEY("ImSlateDemo.ShowToast"), InCommitText, FName("Info"));
				}
				return;
			});

			InOutWidget = WidgetRef;
		}
		else if (auto ItemFloat = InData->As<ImSlate::FImListViewItemFloat>())
		{
			TSharedRef<SSpinBox<float>> WidgetRef = ImSlate::ImFactoryCreate<UImSpinBox>();
			WidgetRef->SetValue(ItemFloat->Val);

			TWeakPtr<ImSlate::FImListViewItemFloat> Weak = StaticCastSharedPtr<ImSlate::FImListViewItemFloat>(InData);
			ImSpinBoxNS::PrivateAccess::OnValueChanged(WidgetRef.Get()) = SSpinBox<float>::FOnValueChanged::CreateLambda([Weak](float InVal) -> void {
				if (auto Pinned = Weak.Pin())
				{
					Pinned->Val = InVal;
				}
			});

			ImSpinBoxNS::PrivateAccess::OnValueCommitted(WidgetRef.Get()) = SSpinBox<float>::FOnValueCommitted::CreateLambda([Weak](float InVal, ETextCommit::Type InTyp) -> void {
				auto Pinned = Weak.Pin();
				if (Pinned && InTyp != ETextCommit::OnCleared)
				{
					Pinned->Val = InVal;
				}
			});

			InOutWidget = WidgetRef;
		}
	});
	BindingData->SetItemAxisBinding([](int32 InIndex) { return 22.f; });
	BindingData->SetFilterExpr([](const TSharedPtr<ImSlate::FImListViewItemBase>& InData, const FGMPStructUnion& InFilter) -> bool {
		//if (InFilter.IsValid(FComboSearchType::StaticStruct()))
		{
			if (auto Filter = InFilter.GetStruct<FComboSearchType>())
			{
				if (Filter->FilterText.IsEmpty())
				{
					return true;
				}

				if (auto ItemButton = InData->As<ImSlate::FImListViewItemButton>())
				{
					return true;
				}
				else if (auto TextButton = InData->As<ImSlate::FImListViewItemTextButton>())
				{
					return DefaultTextFilter(Filter->FilterText, TextButton->ButtonText);
				}
				else if (auto ItemText = InData->As<ImSlate::FImListViewItemText>())
				{
					return DefaultTextFilter(Filter->FilterText, ItemText->Text);
				}
				else if (auto EditableText = InData->As<ImSlate::FImListViewItemEditableText>())
				{
					return DefaultTextFilter(Filter->FilterText, EditableText->EditText);
				}
				else if (auto ItemFloat = InData->As<ImSlate::FImListViewItemFloat>())
				{
					return true;
				}
			}
		}

		return false;
	});

	static TArray<TSharedPtr<ImSlate::FImListViewItemBase>> VirtualListSource = {
		MakeShared<ImSlate::FImListViewItemTextButton>(TEXT("ButtonTextPreview")),
		MakeShared<ImSlate::FImListViewItemButton>(),
		MakeShared<ImSlate::FImListViewItemTextButton>(TEXT("ButtonText")),
		MakeShared<ImSlate::FImListViewItemTextButton>(TEXT("ButtonText1")),
		MakeShared<ImSlate::FImListViewItemTextButton>(TEXT("ButtonText2")),
		MakeShared<ImSlate::FImListViewItemTextButton>(TEXT("ButtonText3")),
		MakeShared<ImSlate::FImListViewItemTextButton>(TEXT("ButtonText4")),
		MakeShared<ImSlate::FImListViewItemTextButton>(TEXT("ButtonText5")),
		MakeShared<ImSlate::FImListViewItemText>(TEXT("Text")),
		MakeShared<ImSlate::FImListViewItemText>(TEXT("Text1")),
		MakeShared<ImSlate::FImListViewItemText>(TEXT("Text2")),
		MakeShared<ImSlate::FImListViewItemText>(TEXT("Text3")),
		MakeShared<ImSlate::FImListViewItemText>(TEXT("Text4")),
		MakeShared<ImSlate::FImListViewItemText>(TEXT("Text5")),
		MakeShared<ImSlate::FImListViewItemEditableText>(TEXT("EditableText")),
		MakeShared<ImSlate::FImListViewItemEditableText>(TEXT("EditableText1")),
		MakeShared<ImSlate::FImListViewItemEditableText>(TEXT("EditableText2")),
		MakeShared<ImSlate::FImListViewItemEditableText>(TEXT("EditableText3")),
		MakeShared<ImSlate::FImListViewItemEditableText>(TEXT("EditableText4")),
		MakeShared<ImSlate::FImListViewItemEditableText>(TEXT("EditableText5")),
		MakeShared<ImSlate::FImListViewItemFloat>(100.f),
	};
	BindingData->Reload(VirtualListSource, false);

	// Sources
	ComboBoxSourceArray = {
		MakeShared<ImSlate::FImComboBoxItem_Test>(TEXT("ButtonTextPreview")),
		MakeShared<ImSlate::FImComboBoxItem_Test>(TEXT("ButtonText")),
		MakeShared<ImSlate::FImComboBoxItem_Test>(TEXT("ButtonText1")),
		MakeShared<ImSlate::FImComboBoxItem_Test>(TEXT("ButtonText2")),
		MakeShared<ImSlate::FImComboBoxItem_Test>(TEXT("ButtonText3")),
		MakeShared<ImSlate::FImComboBoxItem_Test>(TEXT("ButtonText4")),
		MakeShared<ImSlate::FImComboBoxItem_Test>(TEXT("ButtonText5")),
		MakeShared<ImSlate::FImComboBoxItem_Test>(TEXT("Text")),
		MakeShared<ImSlate::FImComboBoxItem_Test>(TEXT("Text1")),
		MakeShared<ImSlate::FImComboBoxItem_Test>(TEXT("Text2")),
		MakeShared<ImSlate::FImComboBoxItem_Test>(TEXT("Text3")),
		MakeShared<ImSlate::FImComboBoxItem_Test>(TEXT("Text4")),
		MakeShared<ImSlate::FImComboBoxItem_Test>(TEXT("Text5")),
		MakeShared<ImSlate::FImComboBoxItem_Test>(TEXT("EditableText")),
		MakeShared<ImSlate::FImComboBoxItem_Test>(TEXT("EditableText1")),
		MakeShared<ImSlate::FImComboBoxItem_Test>(TEXT("EditableText2")),
		MakeShared<ImSlate::FImComboBoxItem_Test>(TEXT("EditableText3")),
		MakeShared<ImSlate::FImComboBoxItem_Test>(TEXT("EditableText4")),
		MakeShared<ImSlate::FImComboBoxItem_Test>(TEXT("EditableText5")),
	};

	// Combo Data Binding
	ComboBindingData = MakeShared<ImSlate::FImListDataComboImpl>();
	ComboBindingData->Init();
	ComboBindingData->SetMultiSelect(true);
	ComboBindingData->SetEnableSearchBox(true);
	ComboBindingDataSource = {
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview0"))),   //
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview1"))),   //
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview2"))),   //
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview3"))),   //
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview4"))),   //
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview5"))),   //
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview6"))),   //
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview10"))),  //
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview11"))),  //
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview12"))),  //
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview13"))),  //
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview14"))),  //
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview15"))),  //
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview16"))),  //
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview20"))),  //
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview21"))),  //
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview22"))),  //
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview23"))),  //
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview24"))),  //
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview25"))),  //
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview26"))),  //
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview30"))),  //
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview31"))),  //
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview32"))),  //
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview33"))),  //
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview34"))),  //
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview35"))),  //
		MakeShared<ImSlate::FImListDataComboData>(FText::FromString(TEXT("TextPreview36"))),  //
	};
	ComboBindingData->Reload(ComboBindingDataSource, false);
	bDemoOpen = true;
}

void UImSlateDemo::EndShow()
{
	BindingData = nullptr;
	ComboBindingData = nullptr;
	bDemoOpen = false;
}

void UImSlateDemo::DrawUI(class FPrimitiveDrawInterface* PDI)
{
}

void UImSlateDemo::Tick(float Delta)
{
	if (bDemoOpen)
	{
		auto FrameNo = GFrameNumber;
		ImSlate::SetNextWindowPos(FVector2D(800, 600), ImSlateCond_Once, FVector2D(0.5f, 0.5f));
		ImSlate::SetNextWindowSize(FVector2D(400.f, 600.f), ImSlateCond_Once);
		//ImSlate::SetNextWindowBgAlpha(0.5f);

		ImSlate::Begin("Text", &bDemoOpen);
		if (ImSlate::Button("ButtonTest", FVector2D(40.f, 20.f)))
		{
			UE_LOG(LogTemp, Log, TEXT("ButtonTestClick"));
			FGMPHelper::NotifyWorldMessage(GImSlate->GetWorldChecked(), MSGKEY("ImSlateDemo.ShowToast"), NSLOCTEXT("UPathDrawAbility", "ButtonTest", "ButtonTest"), FName("Info"));
		}
		ImSlate::Spacing();
		if (ImSlate::Button("ButtonTest2", FVector2D(40.f, 20.f)))
		{
			UE_LOG(LogTemp, Log, TEXT("ButtonTestClick"));
			FGMPHelper::NotifyWorldMessage(GImSlate->GetWorldChecked(), MSGKEY("ImSlateDemo.ShowToast"), NSLOCTEXT("UPathDrawAbility", "ButtonTest2", "ButtonTest2"), FName("Info"));
		}

		if (!ImageButtonTestTexture)
		{
			
		}
		if (ImSlate::ImageButton("ImageButtonTest", ImageButtonTestTexture, FVector2D(200.f, 22.f)))
		{
			UE_LOG(LogTemp, Log, TEXT("ImageButtonTestClick"));
			FGMPHelper::NotifyWorldMessage(GImSlate->GetWorldChecked(), MSGKEY("ImSlateDemo.ShowToast"), NSLOCTEXT("UPathDrawAbility", "ImageButtonTest", "ImageButtonTest"), FName("Info"));
		}

		if (!ImageButtonTestMaterial)
		{
			
		}
		if (ImSlate::ImageButton("ImageButtonTest2", ImageButtonTestMaterial, FVector2D(200.f, 22.f)))
		{
			UE_LOG(LogTemp, Log, TEXT("ImageButtonTestClick2"));
			FGMPHelper::NotifyWorldMessage(GImSlate->GetWorldChecked(), MSGKEY("ImSlateDemo.ShowToast"), NSLOCTEXT("UPathDrawAbility", "ImageButtonTest2", "ImageButtonTest2"), FName("Info"));
		}
		ImSlate::Spacing();
		ImSlate::Spacing();
		static FString InputTextVal = TEXT("JustTest");
		if (ImSlate::InputText("InputText1", InputTextVal, FVector2D(200.f, 22.f)))
		{
			FGMPHelper::NotifyWorldMessage(GImSlate->GetWorldChecked(), MSGKEY("ImSlateDemo.ShowToast"), NSLOCTEXT("UPathDrawAbility", "InputTextEdit", "InputTextEdit"), FName("Info"));
		}

		if (ImSlate::CheckBox("UPathDrawAbility_EditCheckBox", BoolVal))
		{
			UE_LOG(LogTemp, Log, TEXT("UPathDrawAbility_ State ValueChanged: change from to %d"), BoolVal ? 1 : 0);
			FGMPHelper::NotifyWorldMessage(GImSlate->GetWorldChecked(), MSGKEY("ImSlateDemo.ShowToast"), FText::FromString(FString::Printf(TEXT("Test CheckBox ValueChanged: change from to %d"), BoolVal ? 1 : 0)), FName("Info"));
		}

		static TArray<TSharedPtr<ImSlate::FImListViewItemBase>> Sources1 = {
			MakeShared<ImSlate::FImListViewItemTextButton>(TEXT("ButtonTextPreview")),
			MakeShared<ImSlate::FImListViewItemButton>(),
			MakeShared<ImSlate::FImListViewItemTextButton>(TEXT("ButtonText")),
			MakeShared<ImSlate::FImListViewItemTextButton>(TEXT("ButtonText1")),
			MakeShared<ImSlate::FImListViewItemTextButton>(TEXT("ButtonText2")),
			MakeShared<ImSlate::FImListViewItemTextButton>(TEXT("ButtonText3")),
			MakeShared<ImSlate::FImListViewItemTextButton>(TEXT("ButtonText4")),
			MakeShared<ImSlate::FImListViewItemTextButton>(TEXT("ButtonText5")),
			MakeShared<ImSlate::FImListViewItemText>(TEXT("Text")),
			MakeShared<ImSlate::FImListViewItemText>(TEXT("Text1")),
			MakeShared<ImSlate::FImListViewItemText>(TEXT("Text2")),
			MakeShared<ImSlate::FImListViewItemText>(TEXT("Text3")),
			MakeShared<ImSlate::FImListViewItemText>(TEXT("Text4")),
			MakeShared<ImSlate::FImListViewItemText>(TEXT("Text5")),
			MakeShared<ImSlate::FImListViewItemEditableText>(TEXT("EditableText")),
			MakeShared<ImSlate::FImListViewItemEditableText>(TEXT("EditableText1")),
			MakeShared<ImSlate::FImListViewItemEditableText>(TEXT("EditableText2")),
			MakeShared<ImSlate::FImListViewItemEditableText>(TEXT("EditableText3")),
			MakeShared<ImSlate::FImListViewItemEditableText>(TEXT("EditableText4")),
			MakeShared<ImSlate::FImListViewItemEditableText>(TEXT("EditableText5")),
			MakeShared<ImSlate::FImListViewItemFloat>(100.f),
		};

		TSharedPtr<ImSlate::FImListViewItemBase> SelectedItem = nullptr;
		if (ImSlate::ListView_Test("ListView_Test1", SelectedItem, Sources1, FVector2D(300.f, 100.f)))
		{
			FGMPHelper::NotifyWorldMessage(GImSlate->GetWorldChecked(), MSGKEY("ImSlateDemo.ShowToast"), NSLOCTEXT("UPathDrawAbility", "ListView_Test1", "ListView_Test1"), FName("Info"));
		}

		ImSlate::Spacing(10.f);
		static TArray<TSharedPtr<ImSlate::FImListViewItemBase>> Sources2 = {
			MakeShared<ImSlate::FImListViewItemTextButton>(TEXT("ButtonTextPreview")),
			MakeShared<ImSlate::FImListViewItemButton>(),
			MakeShared<ImSlate::FImListViewItemTextButton>(TEXT("ButtonText")),
			MakeShared<ImSlate::FImListViewItemTextButton>(TEXT("ButtonText1")),
			MakeShared<ImSlate::FImListViewItemTextButton>(TEXT("ButtonText2")),
			MakeShared<ImSlate::FImListViewItemTextButton>(TEXT("ButtonText3")),
			MakeShared<ImSlate::FImListViewItemTextButton>(TEXT("ButtonText4")),
			MakeShared<ImSlate::FImListViewItemTextButton>(TEXT("ButtonText5")),
			MakeShared<ImSlate::FImListViewItemText>(TEXT("Text")),
			MakeShared<ImSlate::FImListViewItemText>(TEXT("Text1")),
			MakeShared<ImSlate::FImListViewItemText>(TEXT("Text2")),
			MakeShared<ImSlate::FImListViewItemText>(TEXT("Text3")),
			MakeShared<ImSlate::FImListViewItemText>(TEXT("Text4")),
			MakeShared<ImSlate::FImListViewItemText>(TEXT("Text5")),
			MakeShared<ImSlate::FImListViewItemEditableText>(TEXT("EditableText")),
			MakeShared<ImSlate::FImListViewItemEditableText>(TEXT("EditableText1")),
			MakeShared<ImSlate::FImListViewItemEditableText>(TEXT("EditableText2")),
			MakeShared<ImSlate::FImListViewItemEditableText>(TEXT("EditableText3")),
			MakeShared<ImSlate::FImListViewItemEditableText>(TEXT("EditableText4")),
			MakeShared<ImSlate::FImListViewItemEditableText>(TEXT("EditableText5")),
			MakeShared<ImSlate::FImListViewItemFloat>(100.f),
		};
		TSharedPtr<ImSlate::FImListViewItemBase> TreeSelectedItem = nullptr;
		if (ImSlate::TreeView_Test("TreeView_Test1", SelectedItem, Sources2, FVector2D(300.f, 100.f)))
		{
			FGMPHelper::NotifyWorldMessage(GImSlate->GetWorldChecked(), MSGKEY("ImSlateDemo.ShowToast"), NSLOCTEXT("UPathDrawAbility", "TreeView_Test1", "TreeView_Test1"), FName("Info"));
		}
		/*ImSlate::Spacing(20.f);
		if (ImSlate::VirualList_Test("DataBindingListView_Test1", VirtualListSelectedItem, DataBinding, FVector2D(300.f, 200.f)))
		{
			FGMPHelper::NotifyWorldMessage(GImSlate->GetWorldChecked(), MSGKEY("ImSlateDemo.ShowToast"), NSLOCTEXT("UPathDrawAbility", "DataBindingListView_Test1", "DataBindingListView_Test1"), FName("Info"));
		}*/

		ImSlate::Spacing(20.f);

		//static FComboSourceType Sources4(&ComboBoxSourceArray);
		//ImSlate::SetNextItemMaxHeight(30.f);
		//if (ImSlate::ComboBox("ComboBox_Test", ComboBoxSelectedItem, Sources4, true))
		//{
		//	FGMPHelper::NotifyWorldMessage(GImSlate->GetWorldChecked(), MSGKEY("ImSlateDemo.ShowToast"), NSLOCTEXT("UPathDrawAbility", /"ComboBox_Test_SelectionChanged", /"ComboBox_Test_SelectionChanged"), FName("Info"));
		//}
		//ImSlate::SetNextItemMaxHeight(30.f);
		//if (ImSlate::ComboBox("ComboBox_Test2", ComboBoxSelectedItem, Sources4, false))
		//{
		//	FGMPHelper::NotifyWorldMessage(GImSlate->GetWorldChecked(), MSGKEY("ImSlateDemo.ShowToast"), NSLOCTEXT("UPathDrawAbility", /"ComboBox_Test_SelectionChanged2", /"ComboBox_Test_SelectionChanged2"), FName("Info"));
		//}

		// 		ImSlate::Spacing(20.f);
		// 		ImSlate::SetNextItemMaxHeight(30.f);
		// 		if (ImSlate::ComboBox("VirtualComboBox", CurrentIndex, DataBinding.ToSharedRef(), true))
		// 		{
		// 			FGMPHelper::NotifyWorldMessage(GImSlate->GetWorldChecked(), MSGKEY("ImSlateDemo.ShowToast"), NSLOCTEXT("UPathDrawAbility", "VirtualComboBox_SelectionChanged", "VirtualComboBox_SelectionChanged"), FName("Info"));
		// 		}

		ImSlate::Spacing(20.f);
		ImSlate::SetNextItemMaxHeight(30.f);
		if (ImSlate::ComboBox("VirtualComboBox2", ComboCurrentIndex, ComboBindingData.ToSharedRef()))
		{
			FGMPHelper::NotifyWorldMessage(GImSlate->GetWorldChecked(), MSGKEY("ImSlateDemo.ShowToast"), NSLOCTEXT("UPathDrawAbility", "VirtualComboBox_SelectionChanged2", "VirtualComboBox_SelectionChanged2"), FName("Info"));
		}
		ImSlate::End();
	}
}

void UImSlateDemo::EnableTick(TOptional<bool> bEnable)
{
	using namespace ImSlate;
	do
	{
		if (!GetWorld())
			break;
		bool bTargetOpen = bEnable.Get(!bDemoOpen);
		if (bDemoOpen != bTargetOpen)
		{
			TickHandle.Reset();
			EndShow();
		}
		if (bTargetOpen)
		{
			TickHandle = ImSlateTicker::BindDelegate(ImSlateTicker::FOnTick::CreateWeakLambda(this, [this](float Delta) { Tick(Delta); }), GetWorld());
			StartShow();
		}
		return;
	} while (false);
}

namespace ImSlate
{
TSharedRef<SWidget> FImComboBoxItem_Test::GenWidget()
{
	TSharedRef<STextBlock> WidgetRef = ImFactoryCreate<UImTextBlock>();
	WidgetRef->SetText(FText::FromString(Val));
	WidgetRef->SetAutoWrapText(true);
	return WidgetRef;
}

void FImComboBoxItem_Test::SelectionChanged(bool bInSelected, ESelectInfo::Type)
{
	bSelected = bInSelected;
}

bool FImComboBoxItem_Test::OnMeetConditions(const FText& InFilterText)
{
	if (InFilterText.IsEmpty())
		return true;
	auto FilterStr = InFilterText.ToString();
	TArray<FString> SplitedStr;
	FilterStr.ParseIntoArray(SplitedStr, TEXT(" "));
	for (auto& Str : SplitedStr)
	{
		if (!Val.Contains(Str))
		{
			return false;
		}
	}

	return true;
}
bool ListView_Test(ImStr label, TSharedPtr<FImListViewItemBase>& InOutSelected, const TArray<TSharedPtr<FImListViewItemBase>>& InSource, const ImVec2& InSize /*= ImVec2(0, 0)*/, bool bClearSelection /*= false*/)
{
	if (InSource.Num() <= 0)
		return false;

	auto DataBinding = MakeShared<TImSlateDataStorage<FImListViewItemBase>>();
	DataBinding->SetDataBindding([InSize](TSharedPtr<FImListViewItemBase> InData, TSharedPtr<SWidget>& OutWidget) {
		if (!InData.IsValid())
		{
			OutWidget = SNullWidget::NullWidget;
			return;
		}

		if (InData->CType == ITS::TypeStr<FImListViewItemButton>())
		{
			auto Data = static_cast<FImListViewItemButton*>(InData.Get());
			TSharedRef<SButton> WidgetRef = ImFactoryCreate<UImButton>();
			WidgetRef->SetOnClicked(FOnClicked::CreateLambda([]() -> FReply {
				FGMPHelper::NotifyWorldMessage(ImSlate::GetWorldChecked(), MSGKEY("ImSlateDemo.ShowToast"), NSLOCTEXT("ListView_Test", "FImListViewItemButtonClick", "FImListViewItemButtonClick"), FName("Info"));
				return FReply::Handled();
			}));

			OutWidget = WidgetRef;
		}
		else if (InData->CType == ITS::TypeStr<FImListViewItemTextButton>())
		{
			auto Data = static_cast<FImListViewItemTextButton*>(InData.Get());
			TSharedRef<SButton> WidgetRef = ImFactoryCreate<UImButton>();
			WidgetRef->SetContent(SNew(STextBlock).Text(FText::FromString(Data->ButtonText)).Clipping(EWidgetClipping::ClipToBoundsAlways));
			WidgetRef->SetOnClicked(FOnClicked::CreateLambda([]() -> FReply {
				FGMPHelper::NotifyWorldMessage(ImSlate::GetWorldChecked(), MSGKEY("ImSlateDemo.ShowToast"), NSLOCTEXT("ListView_Test", "FImListViewItemTextButtonClick", "FImListViewItemTextButtonClick"), FName("Info"));
				return FReply::Handled();
			}));

			OutWidget = WidgetRef;
		}
		else if (InData->CType == ITS::TypeStr<FImListViewItemText>())
		{
			auto Data = static_cast<FImListViewItemText*>(InData.Get());
			TSharedRef<STextBlock> WidgetRef = ImFactoryCreate<UImTextBlock>();
			WidgetRef->SetText(FText::FromString(Data->Text));

			OutWidget = WidgetRef;
		}
		else if (InData->CType == ITS::TypeStr<FImListViewItemEditableText>())
		{
			auto Data = static_cast<FImListViewItemEditableText*>(InData.Get());
			TSharedRef<SEditableText> WidgetRef = ImFactoryCreate<UImEditableText>();
			WidgetRef->SetText(FText::FromString(Data->EditText));
			GS_ACCESS_PROTECT(WidgetRef, SEditableText, OnTextCommittedCallback)->OnTextCommittedCallback = FOnTextCommitted::CreateLambda([](const FText& InCommitText, ETextCommit::Type InTyp) -> void {
				if (InTyp != ETextCommit::OnCleared)
				{
					FGMPHelper::NotifyWorldMessage(ImSlate::GetWorldChecked(), MSGKEY("ImSlateDemo.ShowToast"), InCommitText, FName("Info"));
				}
				return;
			});

			OutWidget = WidgetRef;
		}
		else if (InData->CType == ITS::TypeStr<FImListViewItemFloat>())
		{
			auto Data = static_cast<FImListViewItemFloat*>(InData.Get());
			TSharedRef<SSpinBox<float>> WidgetPtr = ImFactoryCreate<UImSpinBox>();

			WidgetPtr->SetValue(Data->Val);

			TWeakPtr<FImListViewItemBase> Weak = InData;
			ImSpinBoxNS::PrivateAccess::OnValueChanged(WidgetPtr.Get()) = SSpinBox<float>::FOnValueChanged::CreateLambda([Weak](float InVal) -> void {
				if (Weak.IsValid())
				{
					auto Data = static_cast<FImListViewItemFloat*>(Weak.Pin().Get());
					Data->Val = InVal;
				}
			});

			ImSpinBoxNS::PrivateAccess::OnValueCommitted(WidgetPtr.Get()) = SSpinBox<float>::FOnValueCommitted::CreateLambda([Weak](float InVal, ETextCommit::Type InTyp) -> void {
				if (Weak.IsValid() && InTyp != ETextCommit::OnCleared)
				{
					auto Data = static_cast<FImListViewItemFloat*>(Weak.Pin().Get());
					Data->Val = InVal;
				}
			});

			OutWidget = WidgetPtr;
		}
	});

	// 	DataBinding->SetMouseButtonClickBinding([&InOutSelected](TSharedPtr<FString> InData) {
	// 		if (InData.IsValid())
	// 		{
	// 			InOutSelected = InData;
	// 		}
	// 	});

	return ListView<FImListViewItemBase>(label, InOutSelected, InSource, DataBinding, InSize, bClearSelection);
}

bool TreeView_Test(ImStr label, TSharedPtr<FImListViewItemBase>& InOutSelected, const TArray<TSharedPtr<FImListViewItemBase>>& InSource, const ImVec2& InSize /*= ImVec2(0, 0)*/, bool bClearSelection /*= false*/)
{
	if (InSource.Num() <= 0)
		return false;

	auto DataBinding = MakeShared<TImSlateDataStorage<FImListViewItemBase>>();
	DataBinding->SetDataBindding([InSize](TSharedPtr<FImListViewItemBase> InData, TSharedPtr<SWidget>& OutWidget) {
		if (!InData.IsValid())
		{
			OutWidget = SNullWidget::NullWidget;
			return;
		}

		if (auto Data = InData->As<FImListViewItemButton>())
		{
			TSharedRef<SButton> WidgetRef = ImFactoryCreate<UImButton>();
			WidgetRef->SetOnClicked(FOnClicked::CreateLambda([]() -> FReply {
				FGMPHelper::NotifyWorldMessage(ImSlate::GetWorldChecked(), MSGKEY("ImSlateDemo.ShowToast"), NSLOCTEXT("ListView_Test", "FImListViewItemButtonClick", "FImListViewItemButtonClick"), FName("Info"));
				return FReply::Handled();
			}));

			OutWidget = WidgetRef;
		}
		else if (auto TextButton = InData->As<FImListViewItemTextButton>())
		{
			TSharedRef<SButton> WidgetRef = ImFactoryCreate<UImButton>();
			WidgetRef->SetContent(SNew(STextBlock).Text(FText::FromString(TextButton->ButtonText)).Clipping(EWidgetClipping::ClipToBoundsAlways));
			WidgetRef->SetOnClicked(FOnClicked::CreateLambda([]() -> FReply {
				FGMPHelper::NotifyWorldMessage(ImSlate::GetWorldChecked(), MSGKEY("ImSlateDemo.ShowToast"), NSLOCTEXT("ListView_Test", "FImListViewItemTextButtonClick", "FImListViewItemTextButtonClick"), FName("Info"));
				return FReply::Handled();
			}));

			OutWidget = WidgetRef;
		}
		else if (auto ItemText = InData->As<FImListViewItemText>())
		{
			TSharedRef<STextBlock> WidgetRef = ImFactoryCreate<UImTextBlock>();
			WidgetRef->SetText(FText::FromString(ItemText->Text));

			OutWidget = WidgetRef;
		}
		else if (auto EditableText = InData->As<FImListViewItemEditableText>())
		{
			TSharedRef<SEditableText> WidgetRef = ImFactoryCreate<UImEditableText>();
			WidgetRef->SetText(FText::FromString(EditableText->EditText));
			GS_ACCESS_PROTECT(WidgetRef, SEditableText, OnTextCommittedCallback)->OnTextCommittedCallback = FOnTextCommitted::CreateLambda([](const FText& InCommitText, ETextCommit::Type InTyp) -> void {
				if (InTyp != ETextCommit::OnCleared)
				{
					FGMPHelper::NotifyWorldMessage(ImSlate::GetWorldChecked(), MSGKEY("ImSlateDemo.ShowToast"), InCommitText, FName("Info"));
				}
				return;
			});

			OutWidget = WidgetRef;
		}
		else if (auto ItemFloat = InData->As<FImListViewItemFloat>())
		{
			TSharedRef<SSpinBox<float>> WidgetPtr = ImFactoryCreate<UImSpinBox>();

			WidgetPtr->SetValue(ItemFloat->Val);

			TWeakPtr<FImListViewItemBase> Weak = InData;
			ImSpinBoxNS::PrivateAccess::OnValueChanged(WidgetPtr.Get()) = SSpinBox<float>::FOnValueChanged::CreateLambda([Weak](float InVal) -> void {
				if (Weak.IsValid())
				{
					auto Data = static_cast<FImListViewItemFloat*>(Weak.Pin().Get());
					Data->Val = InVal;
				}
			});

			ImSpinBoxNS::PrivateAccess::OnValueCommitted(WidgetPtr.Get()) = SSpinBox<float>::FOnValueCommitted::CreateLambda([Weak](float InVal, ETextCommit::Type InTyp) -> void {
				if (Weak.IsValid() && InTyp != ETextCommit::OnCleared)
				{
					auto Data = static_cast<FImListViewItemFloat*>(Weak.Pin().Get());
					Data->Val = InVal;
				}
			});

			OutWidget = WidgetPtr;
		}
	});

	return TreeView<FImListViewItemBase>(label, InOutSelected, InSource, DataBinding, InSize, bClearSelection);
}

bool VirualList_Test(ImStr label,
					 TSharedPtr<FImListViewItemBase>& InOutSelected,
					 const TSharedPtr<TImSlateListArray<TSharedPtr<FImListViewItemBase>>>& InDataBinding,
					 const ImVec2& InSize /*= ImVec2(0, 0)*/,
					 bool bClearSelection /*= false*/)
{
	return VirtualList(label, InDataBinding.ToSharedRef(), InSize);
}

}  // namespace ImSlate
