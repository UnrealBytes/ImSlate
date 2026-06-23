// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Components/ContentWidget.h"
#include "Input/Reply.h"
#include "Styling/SlateTypes.h"
#include "UObject/ObjectMacros.h"
#include "Widgets/SWidget.h"
#include "Components/Button.h"
#include "Framework/SlateDelegates.h"
#include "Blueprint/DragDropOperation.h"
#include "ImSlateFactory.h"
#include "Engine/GameViewportClient.h"

#include "ImMultiStateButton.generated.h"

namespace ImSlate { class SImButton; }  // SImButton lives in namespace ImSlate now

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnButtonFocused);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnButtonUnFocused);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnButtonDragBegin);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnButtonDragEnd);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnButtonDragQuery, const FGeometry&, MyGeometry, const FPointerEvent&, MouseEvent);
DECLARE_DELEGATE(FOnButtonDropOperation);


USTRUCT(BlueprintType)
struct IMSLATE_API FImCustomWidgetState
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FName StateName;
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FSlateBrush ActivatedBrush;
};

/**
 * Some extra styles other than FButtonStyle
 */
USTRUCT(BlueprintType)
struct IMSLATE_API FImButtonExtraStyle
{
	GENERATED_USTRUCT_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateBrush Focused;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateBrush Dragged;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Mostly Copied from FUMGDragDropOp which is sadly not exported
 * 不要轻易修改！可能导致此类cast到FUMGDragDropOp时失败
 */
class FImButtonDragDropOp
	: public FGameDragDropOperation
	, public FGCObject
{
public:
	//DRAG_DROP_OPERATOR_TYPE(FXButtonDragDropOp, FGameDragDropOperation)

	static const FString& GetTypeId()
	{
		static FString Type = TEXT("FUMGDragDropOp");
		return Type;
	}
	virtual bool IsOfTypeImpl(const FString& Type) const override { return GetTypeId() == Type || FGameDragDropOperation::IsOfTypeImpl(Type); }

	static TSharedRef<FImButtonDragDropOp>
		New(UDragDropOperation* Operation, const int32 PointerIndex, const FVector2D& CursorPosition, const FVector2D& ScreenPositionOfNode, float DPIScale, UWorld* World, FOnButtonDropOperation OnButtonDropOperation);

	FImButtonDragDropOp();

	// Begin FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;
	// End FGCObject

	virtual bool AffectedByPointerEvent(const FPointerEvent& PointerEvent) override;
	virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) override;
	virtual void OnDragged(const class FDragDropEvent& DragDropEvent) override;
	virtual FCursorReply OnCursorQuery() override;
	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;

	UDragDropOperation* GetOperation() const { return DragOperation.Get(); }

protected:
	virtual void Construct() override;

private:
	// Raw pointer to the drag operation, kept alive by AddReferencedObjects.
	TObjectPtr<UDragDropOperation> DragOperation;

	/** The viewport this drag/drop operation is associated with. */
	TObjectPtr<UGameViewportClient> GameViewport;

	/** The widget used during the drag/drop action to show something being dragged. */
	TSharedPtr<SWidget> DecoratorWidget;

	/** The offset to use when dragging the object so that it says the same distance away from the mouse. */
	FVector2D MouseDownOffset;

	/** The starting screen location where the drag operation started. */
	FVector2D StartingScreenPos;

	int32 PointerIndex;

	/** Allows smooth interpolation of the dragged visual over a few frames. */
	double StartTime;

	FOnButtonDropOperation OnButtonDropOperation;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * A button with multiple states.
 * One kind of states includes clicked, pressed, released, hovered, unhovered, focused, unfocused, dragquery, dragbegin, dragend
 * that are handled internally in slate.
 * Other states are custom states defined by the user. The diplay of the widget prioritizes custom states first.
 * See EnterCustomState and QuitCustomState
 *
 * * Single Child
 * * Clickable
 */
UCLASS()
class IMSLATE_API UImMultiStateButton
	: public UButton
	, public TImFactory<ImSlate::SImButton>
{
	GENERATED_BODY()

public:

	UImMultiStateButton(const FObjectInitializer& ObjectInitializer);

	TSharedRef<ImSlate::SImButton> ConstructImWidget();

	/** Custom States */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (TitleProperty = StateName))
	TArray<FImCustomWidgetState> CustomStates;

	UPROPERTY(BlueprintReadWrite, Transient, Category = "Interaction")
	UDragDropOperation* DragDropOp = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (DisplayName = "ExtraStyle"))
	FImButtonExtraStyle ExtraStyle;

public:
	void ReleaseSlateResources(bool bReleaseChildren) override;

public:

	UPROPERTY(BlueprintAssignable, Category = "Button|Event")
	FOnButtonFocused OnFocused;

	UPROPERTY(BlueprintAssignable, Category = "Button|Event")
	FOnButtonUnFocused OnUnFocused;
	
	/**
	 * Fired when there's a drag attempt.
	 * If you set the DragDropOperation here, OnDragDetected will get fired.
	 * else nothing will happen.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Button|Event")
	FOnButtonDragQuery OnDragQuery;

	UPROPERTY(BlueprintAssignable, Category = "Button|Event")
	FOnButtonDragBegin OnDragBegin;

	UPROPERTY(BlueprintAssignable, Category = "Button|Event")
	FOnButtonDragEnd OnDragEnd;

public:
	void SetExtraStyle(const FImButtonExtraStyle& InStyle);

	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void EnterCustomState(const FName& StateName);

	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void QuitCustomState();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	TSharedPtr<ImSlate::SImButton> MyImButton;
};
