
#include "ImSlateTemplate/ImMultiStateButton.h"
#include "Components/ButtonSlot.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "ImSlateTemplate/ImButton.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Engine/GameViewportClient.h"

TSharedRef<SWidget> UImMultiStateButton::RebuildWidget()
{
	MyImButton = SNew(ImSlate::SImButton, this)
				   .OnClicked(BIND_UOBJECT_DELEGATE(FOnClicked, SlateHandleClicked))
				   .OnPressed(BIND_UOBJECT_DELEGATE(FSimpleDelegate, SlateHandlePressed))
				   .OnReleased(BIND_UOBJECT_DELEGATE(FSimpleDelegate, SlateHandleReleased))
				   .OnHovered_UObject(this, &ThisClass::SlateHandleHovered)
				   .OnUnhovered_UObject(this, &ThisClass::SlateHandleUnhovered)
				   .ButtonStyle(&GetStyle())
				   .ClickMethod(GetClickMethod())
				   .TouchMethod(GetTouchMethod())
				   .PressMethod(GetPressMethod())
				   .IsFocusable(GetIsFocusable());

	MyButton = MyImButton;
	if (GetChildrenCount() > 0)
	{
		Cast<UButtonSlot>(GetContentSlot())->BuildSlot(MyButton.ToSharedRef());
	}
	return MyButton.ToSharedRef();
}

 UImMultiStateButton::UImMultiStateButton(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

TSharedRef<ImSlate::SImButton> UImMultiStateButton::ConstructImWidget()
{
	TSharedPtr<ImSlate::SImButton> MyStealButton = SNew(ImSlate::SImButton, this)
											//.OnClicked(BIND_UOBJECT_DELEGATE(FOnClicked, SlateHandleClicked))
											//.OnPressed(BIND_UOBJECT_DELEGATE(FSimpleDelegate, SlateHandlePressed))
											//.OnReleased(BIND_UOBJECT_DELEGATE(FSimpleDelegate, SlateHandleReleased))
											//.OnHovered_UObject(this, &ThisClass::SlateHandleHovered)
											//.OnUnhovered_UObject(this, &ThisClass::SlateHandleUnhovered)
											.ButtonStyle(&GetStyle())
											.ClickMethod(GetClickMethod())
											.TouchMethod(GetTouchMethod())
											.PressMethod(GetPressMethod())
											.IsFocusable(GetIsFocusable());

	if (GetChildrenCount() > 0)
	{
		Cast<UButtonSlot>(GetContentSlot())->BuildSlot(MyStealButton.ToSharedRef());
	}

	// UWidget

	// #if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// 	bRoutedSynchronizeProperties = true;
	// #endif

#if WIDGET_INCLUDE_RELFECTION_METADATA
	// We only need to do this once, when the slate widget is created.
	MyStealButton->AddMetadata<FReflectionMetaData>(MakeShared<FReflectionMetaData>(GetFName(), GetClass(), (UObject*)this, GetSourceAssetOrClass()));
#endif

	{
		if (bOverride_Cursor /*|| CursorDelegate.IsBound()*/)
		{
			MyStealButton->SetCursor(GetCursor());  // PROPERTY_BINDING(EMouseCursor::Type, Cursor));
		}

		//MyStealButton->SetEnabled(BITFIELD_PROPERTY_BINDING(bIsEnabled));
		MyStealButton->SetVisibility(ConvertVisibility(GetVisibility()));
	}

	MyStealButton->SetClipping(GetClipping());

	MyStealButton->SetFlowDirectionPreference(GetFlowDirectionPreference());
	MyStealButton->ForceVolatile(bIsVolatile);
	MyStealButton->SetRenderOpacity(GetRenderOpacity());

	if (GetRenderTransform().IsIdentity())
	{
		MyStealButton->SetRenderTransform(TOptional<FSlateRenderTransform>());
	}
	else
	{
		MyStealButton->SetRenderTransform(GetRenderTransform().ToSlateRenderTransform());
	}

	MyStealButton->SetRenderTransformPivot(GetRenderTransformPivot());

	// 	if (ToolTipWidgetDelegate.IsBound() && !IsDesignTime())
	// 	{
	// 		TSharedRef<ImSlate::FDelegateToolTip> ToolTip = MakeShared<ImSlate::FDelegateToolTip>();
	// 		ToolTip->ToolTipWidgetDelegate = ToolTipWidgetDelegate;
	// 		MyStealEditableText->SetToolTip(GetToolTip());
	// 	}
	// 	else if (ToolTipWidget != nullptr)
	// 	{
	// 		TSharedRef<SToolTip> ToolTip = SNew(SToolTip)
	// 										.TextMargin(FMargin(0))
	// 										.BorderImage(nullptr)
	// 										[
	// 											ToolTipWidget->TakeWidget()
	// 										];
	//
	// 		MyStealEditableText->SetToolTip(GetToolTip());
	// 	}
	// 	else if (!ToolTipText.IsEmpty() || ToolTipTextDelegate.IsBound())
	// 	{
	// 		MyStealEditableText->SetToolTipText(PROPERTY_BINDING(FText, ToolTipText));
	// 	}

	// UWidget End

	MyStealButton->SetColorAndOpacity(GetColorAndOpacity());
	MyStealButton->SetBorderBackgroundColor(GetBackgroundColor());

	return MyStealButton.ToSharedRef();
}

void UImMultiStateButton::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyImButton.Reset();
}

void UImMultiStateButton::SetExtraStyle(const FImButtonExtraStyle& InStyle)
{
	if (MyImButton.IsValid())
	{
		MyImButton->SetButtonExtraStyle(&InStyle);
	}
}

void UImMultiStateButton::EnterCustomState(const FName& StateName)
{
	auto State = CustomStates.FindByPredicate([StateName](const FImCustomWidgetState& InState) { return InState.StateName == StateName; });
	if (!State)
	{
		return;
	}
	MyImButton->EnterCustomState(State);
}

void UImMultiStateButton::QuitCustomState()
{
	MyImButton->QuitCustomState();
}

//////////////////////////////////////////////////////////////////////////////////////////////////

FImButtonDragDropOp::FImButtonDragDropOp()
	: DragOperation(nullptr)
	, GameViewport(nullptr)
{
	StartTime = FSlateApplicationBase::Get().GetCurrentTime();
}

void FImButtonDragDropOp::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(DragOperation);
	Collector.AddReferencedObject(GameViewport);
}

FString FImButtonDragDropOp::GetReferencerName() const
{
	return TEXT("FXButtonDragDropOp");
}

void FImButtonDragDropOp::Construct()
{
}

bool FImButtonDragDropOp::AffectedByPointerEvent(const FPointerEvent& PointerEvent)
{
	return DragOperation && PointerEvent.GetPointerIndex() == PointerIndex;
}

void FImButtonDragDropOp::OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent)
{
	if (DragOperation)
	{
		if (bDropWasHandled && MouseEvent.GetPointerIndex() == PointerIndex)
		{
			DragOperation->Drop(MouseEvent);
		}
		else
		{
			DragOperation->DragCancelled(MouseEvent);
		}
		OnButtonDropOperation.ExecuteIfBound();
	}

	FDragDropOperation::OnDrop(bDropWasHandled, MouseEvent);
}

void FImButtonDragDropOp::OnDragged(const class FDragDropEvent& DragDropEvent)
{
	if (DragOperation && DragDropEvent.GetPointerIndex() == PointerIndex)
	{
		DragOperation->Dragged(DragDropEvent);

		FVector2D CachedDesiredSize = DecoratorWidget->GetDesiredSize();

		FVector2D Position = DragDropEvent.GetScreenSpacePosition();
		Position += CachedDesiredSize * DragOperation->Offset;

		switch (DragOperation->Pivot)
		{
			case EDragPivot::MouseDown:
				Position += MouseDownOffset;
				break;

			case EDragPivot::TopLeft:
				// Position is already Top Left.
				break;
			case EDragPivot::TopCenter:
				Position -= CachedDesiredSize * FVector2D(0.5f, 0);
				break;
			case EDragPivot::TopRight:
				Position -= CachedDesiredSize * FVector2D(1, 0);
				break;

			case EDragPivot::CenterLeft:
				Position -= CachedDesiredSize * FVector2D(0, 0.5f);
				break;
			case EDragPivot::CenterCenter:
				Position -= CachedDesiredSize * FVector2D(0.5f, 0.5f);
				break;
			case EDragPivot::CenterRight:
				Position -= CachedDesiredSize * FVector2D(1.0f, 0.5f);
				break;

			case EDragPivot::BottomLeft:
				Position -= CachedDesiredSize * FVector2D(0, 1);
				break;
			case EDragPivot::BottomCenter:
				Position -= CachedDesiredSize * FVector2D(0.5f, 1);
				break;
			case EDragPivot::BottomRight:
				Position -= CachedDesiredSize * FVector2D(1, 1);
				break;
		}

		const double AnimationTime = 0.150;

		double DeltaTime = FSlateApplicationBase::Get().GetCurrentTime() - StartTime;

		if (DeltaTime < AnimationTime)
		{
			float T = DeltaTime / AnimationTime;
			FVector2D LerpPosition = (Position - StartingScreenPos) * T;

			DecoratorPosition = StartingScreenPos + LerpPosition;
		}
		else
		{
			DecoratorPosition = Position;
		}
	}
}

TSharedPtr<SWidget> FImButtonDragDropOp::GetDefaultDecorator() const
{
	return DecoratorWidget;
}

FCursorReply FImButtonDragDropOp::OnCursorQuery()
{
	FCursorReply CursorReply = FGameDragDropOperation::OnCursorQuery();

	if (!CursorReply.IsEventHandled())
	{
		CursorReply = CursorReply.Cursor(EMouseCursor::Default);
	}

	if (GameViewport)
	{
		TOptional<TSharedRef<SWidget>> CursorWidget = GameViewport->MapCursor(nullptr, CursorReply);
		if (CursorWidget.IsSet())
		{
			CursorReply.SetCursorWidget(GameViewport->GetWindow(), CursorWidget.GetValue());
		}
	}

	return CursorReply;
}

TSharedRef<FImButtonDragDropOp>
	FImButtonDragDropOp::New(UDragDropOperation* InOperation, const int32 PointerIndex, const FVector2D& PointerPosition, const FVector2D& ScreenPositionOfDragee, float DPIScale, UWorld* World, FOnButtonDropOperation OnButtonDropOperation)
{
	check(InOperation);

	TSharedRef<FImButtonDragDropOp> Operation = MakeShareable(new FImButtonDragDropOp());
	Operation->PointerIndex = PointerIndex;
	Operation->MouseDownOffset = ScreenPositionOfDragee - PointerPosition;
	Operation->StartingScreenPos = ScreenPositionOfDragee;
	Operation->GameViewport = World->GetGameViewport();
	Operation->DragOperation = InOperation;
	Operation->OnButtonDropOperation = OnButtonDropOperation;

	TSharedPtr<SWidget> DragVisual;
	if (InOperation->DefaultDragVisual == nullptr)
	{
		DragVisual = SNew(STextBlock).Text(FText::FromString(InOperation->Tag));
	}
	else
	{
		//TODO Make sure users are not trying to add a widget that already exists elsewhere.
		DragVisual = InOperation->DefaultDragVisual->TakeWidget();
	}

	Operation->DecoratorWidget = SNew(SDPIScaler).DPIScale(DPIScale)[DragVisual.ToSharedRef()];

	Operation->DecoratorWidget->SlatePrepass();

	Operation->Construct();

	return Operation;
}