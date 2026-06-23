// Copyright ImSlate, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "InputCoreTypes.h"
#include "Input/Reply.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SLeafWidget.h"
#include "ImSlateFactory.h"
#include "Components/Widget.h"
#include "GameFramework/TouchInterface.h"
#include "Widgets/Input/SVirtualJoystick.h"

#include "ImVirtualJoystick.generated.h"

class FPaintArgs;
class FSlateWindowElementList;
class ISlateBrushSource;
class UEnhancedPlayerInput;
class UPlayerInput;
class UInputAction;
class UTexture2D;
class UInputModifier;
class UInputTrigger;

USTRUCT()
struct FImTouchInputControl
{
	GENERATED_BODY()

public:
	// basically mirroring SVirtualJoystick::FControlInfo but as an editable class
	UPROPERTY(EditAnywhere, Category="Control", meta=(ToolTip="Set this to true to treat the joystick as a simple button"))
	bool bTreatAsButton = false;

	UPROPERTY(EditAnywhere, Category="Control", meta=(ToolTip="For sticks, this is the Thumb"))
	FSlateBrush ThumbImage;

	UPROPERTY(EditAnywhere, Category="Control", meta=(ToolTip="For sticks, this is the Background"))
	FSlateBrush BackgroundImage;

	UPROPERTY(EditAnywhere, Category="Control", meta=(ToolTip="The initial center point of the control. If Time Until Reset is < 0, control resets back to here.\nUse negative numbers to invert positioning from top to bottom, left to right. (if <= 1.0, it's relative to container, > 1.0 is absolute)"))
	FVector2D Center{0.5f, 0.5f};

	UPROPERTY(EditAnywhere, Category="Control", meta=(ToolTip="The size of the control (if <= 1.0, it's relative to container, > 1.0 is absolute)"))
	FVector2D VisualSize{0.7f, 0.7f};

	UPROPERTY(EditAnywhere, Category="Control", meta=(ToolTip="For sticks, the size of the thumb (if <= 1.0, it's relative to container, > 1.0 is absolute)"))
	FVector2D ThumbSize{0.5f, 0.5f};

	UPROPERTY(EditAnywhere, Category="Control", meta=(ToolTip="The interactive size of the control. Measured outward from Center. (if <= 1.0, it's relative to container, > 1.0 is absolute)"))
	FVector2D InteractionSize{1.f, 1.f};

	UPROPERTY(EditAnywhere, Category = "Control", meta = (ToolTip = "The scale for control input"))
	FVector2D InputScale{1.f, 1.f};

	UPROPERTY(EditAnywhere, Category="Control", meta=(ToolTip="The main input to send from this control (for sticks, this is the horizontal axis)"))
	FKey MainInputKey;

	UPROPERTY(EditAnywhere, Category="Control", meta=(ToolTip="The alternate input to send from this control (for sticks, this is the vertical axis)"))
	FKey AltInputKey;


	UPROPERTY(EditAnywhere, Category = "Control", meta = (ToolTip = "The input to send from this control"))
	FKey TriggerKey;

	/*
	 * if Axis2D or Axis3D is set, the control will send axis event
	 * x,y is represent the container position of the control, z is the duration of the pressing time
	 * else Axis1D x is represent the offset scale from center
	 * else Digital is set, the control will send digital event
	 */
	UPROPERTY(EditAnywhere, Category = "Control", meta = (MetaClass = "/Script/EnhancedInput.InputAction", ToolTip = "The input action to send from this control"))
	TObjectPtr<UObject> InputAction;
};

namespace ImSlate
{
class IMSLATE_API SImVirtualJoystick : public SLeafWidget
{

public:
	/** The settings of each zone we render */ 
	struct IMSLATE_API FControlInfo : public SVirtualJoystick::FControlInfo
	{
		FKey TriggerKey;

#if defined(ENHANCEDINPUT_API)
		TWeakObjectPtr<UInputAction> InputAction;
#endif
		bool bAlowOverBorder = true;
	};

	/** The settings and current state of each zone we render */ 
	struct IMSLATE_API FControlData
	{
		/** Control settings */
		FControlInfo Info;

		/**
		* Reset the control to a centered/inactive state
		*/
		void Reset();

		// Current state

		/** The position of the thumb, in relation to the VisualCenter */
		FVector2D ThumbPosition = FVector2D::ZeroVector;

		/** For recentered joysticks, this is the re-center location */
		FVector2D VisualCenter = FVector2D::ZeroVector;

		/** The corrected actual center of the control */
		FVector2D CorrectedCenter = FVector2D::ZeroVector;

		/** The corrected size of a joystick that can be re-centered within InteractionSize area */
		FVector2D CorrectedVisualSize = FVector2D::ZeroVector;

		/** The corrected size of the thumb that can be re-centered within InteractionSize area */
		FVector2D CorrectedThumbSize = FVector2D::ZeroVector;

		/** The corrected size of a the interactable area around Center */
		FVector2D CorrectedInteractionSize = FVector2D::ZeroVector;

		/** The corrected scale for control input */
		FVector2D CorrectedInputScale = FVector2D::ZeroVector;

		/** Which pointer index is interacting with this control right now, or -1 if not interacting */
		int32 CapturedPointerIndex = -1;

		/** Time to activate joystick **/
		float ElapsedTime = 0.0f;

		/** Visual center to be updated */
		FVector2D NextCenter = FVector2D::ZeroVector;

		/** Whether or not to send one last "release" event next tick */
		bool bSendOneMoreEvent = false;

		/** Whether or not we need position the control against the geometry */
		bool bHasBeenPositioned = false;

		/** Whether or not to update center position */
		bool bNeedUpdatedCenter = false;
	};

	SLATE_BEGIN_ARGS(SImVirtualJoystick)
		{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	* Shows or hides the controls (for instance during cinematics)
	*/
	void SetJoystickVisibility(const bool bVisible, const bool bFade);

	void AddControl(const FControlInfo& Control);
	void ClearControls();
	void SetControls(const TArray<FControlInfo>& InControls);

	/**
	* Sets parameters that control all controls
	*/
	void SetGlobalParameters(float InActiveOpacity, float InInactiveOpacity, float InTimeUntilDeactive, float InTimeUntilReset, float InActivationDelay, bool bInPreventReCenter, bool bInReleaseReCenter, float InStartupDelay);
	void SetPlayerInput(UPlayerInput* InPlayerInput, const TArray<UInputModifier*>& Modifiers = {}, const TArray<UInputTrigger*>& Triggers = {});

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FVector2D ComputeDesiredSize(float) const override;

	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& Event) override;
	virtual FReply OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& Event) override;
	virtual FReply OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& Event) override;

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	virtual bool SupportsKeyboardFocus() const override;

protected:
	enum ETriggerStatus
	{
		ETriggerStart,
		ETriggerMoving,
		ETriggerEnd,
	};
	void TriggerTouchEvent(FControlData& Control, const FVector2D& LocalCoord, ETriggerStatus Status = ETriggerMoving, const TArray<UInputModifier*>& Modifiers = {}, const TArray<UInputTrigger*> Triggers = {});
	/** Callback for handling display metrics changes. */
	virtual void HandleDisplayMetricsChanged(const FDisplayMetrics& NewDisplayMetric);

	static void AlignBoxIntoContainer(FVector2D& Position, const FVector2D& Size, const FVector2D& ContainerSize);
	FVector2D ComputeThumbPosition(int32 ControlIndex, const FVector2D& LocalCoord, float* OutDistanceToTouchSqr = nullptr, float* OutDistanceToEdgeSqr = nullptr);

	/**
	* Process a touch event (on movement and possibly on initial touch)
	*
	* @return true if the touch was successful
	*/
	virtual bool HandleTouch(int32 ControlIndex, const FVector2D& LocalCoord, const FVector2D& ContainerSize);

	/** 
	* Return the target opacity to lerp to given the current state
	*/
	FORCEINLINE float GetBaseOpacity();

	/**
	* TArray specialization for controls. In the game only few joysticks presented
	* so we can predict their count and store in memory in more efficient way
	*/ 
	template <typename T>
	using TControlArray = TArray<T, TInlineAllocator<2>>;

	/** List of controls set by the UTouchInterface */
	TControlArray<FControlData> Controls;

	/** Global settings from the UTouchInterface */
	float ActiveOpacity = 1.0f;
	float InactiveOpacity = 0.1f;
	float TimeUntilDeactive = 0.5f;
	float TimeUntilReset = 2.0f;
	float ActivationDelay = 0.0f;
	float StartupDelay = 0.0f;

	enum EVirtualJoystickState
	{
		State_Active,
		State_CountingDownToInactive,
		State_CountingDownToReset,
		State_Inactive,
		State_WaitForStart,
		State_CountingDownToStart,
	};

	/** The current state of all controls */
	EVirtualJoystickState State = State_Inactive;

	/** True if the joystick should be visible */
	uint32 bVisible:1;

	/** If true, this zone will have it's "center" set when you touch it, otherwise the center will be set to the center of the zone */
	uint32 bCenterOnEvent:1;

	/** If true, ignore re-centering */
	uint32 bPreventReCenter:1;

	/** If true, joystick base re-centers immediately when touch is released, instead of waiting for TimeUntilReset */
	uint32 bReleaseReCenter:1;

	/** If true, allow over border */
	uint32 bAlowOverBorder:1;

	/** Target opacity */
	float CurrentOpacity = InactiveOpacity;

	/* Countdown until next state change */
	float Countdown;

	/** Last used scaling value for  */
	float PreviousScalingFactor = 0.0f;

#if defined(ENHANCEDINPUT_API)
	TWeakObjectPtr<UEnhancedPlayerInput> EnhancedPlayerInput;
	TSharedPtr<void> DefaultTriggerRefs;
#endif
};
}  // namespace ImSlate

UCLASS(BlueprintType)
class IMSLATE_API UImVirtualJoystick
	: public UWidget
	, public TImFactory<ImSlate::SImVirtualJoystick>

{
	GENERATED_BODY()
public:
	TSharedRef<ImSlate::SImVirtualJoystick> ConstructImWidget() const;
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	void SynchronizeProperties() override;

	UPROPERTY(EditAnywhere, Category = "TouchInterface")
	TArray<FImTouchInputControl> Controls;

	UPROPERTY(EditAnywhere, Category = "TouchInterface", meta = (ToolTip = "Opacity (0.0 - 1.0) of all controls while any control is active"))
	float ActiveOpacity = 1.f;

	UPROPERTY(EditAnywhere, Category = "TouchInterface", meta = (ToolTip = "Opacity (0.0 - 1.0) of all controls while no controls are active"))
	float InactiveOpacity = 0.1f;

	UPROPERTY(EditAnywhere, Category = "TouchInterface", meta = (ToolTip = "How long after user interaction will all controls fade out to Inactive Opacity"))
	float TimeUntilDeactive = 0.5f;

	UPROPERTY(EditAnywhere, Category = "TouchInterface", meta = (ToolTip = "How long after going inactive will controls reset/recenter themselves (0.0 will disable this feature)"))
	float TimeUntilReset = 2.f;

	UPROPERTY(EditAnywhere, Category = "TouchInterface", meta = (ToolTip = "How long after joystick enabled for touch (0.0 will disable this feature)"))
	float ActivationDelay = 0.f;

	UPROPERTY(EditAnywhere, Category = "TouchInterface", meta = (ToolTip = "Whether to prevent the joystick base from moving to the touch position on touch start"))
	bool bPreventRecenter = false;

	UPROPERTY(EditAnywhere, Category = "TouchInterface", meta = (ToolTip = "Whether the joystick base re-centers immediately when touch is released", EditCondition = "!bPreventRecenter"))
	bool bReleaseReCenter = true;

	UPROPERTY(EditAnywhere, Category = "TouchInterface", meta = (ToolTip = "Whether to allow over border"))
	bool bAlowOverBorder = false;

	UPROPERTY(EditAnywhere, Category = "TouchInterface", meta = (ToolTip = "Delay at startup before virtual joystick is drawn"))
	float StartupDelay = 0.f;

	UPROPERTY(EditAnywhere, Category = "Control", meta = (MetaClass = "/Script/EnhancedInput.InputModifier", ToolTip = "Injected Input Modifiers"))
	TArray<TObjectPtr<UObject>> InputModifiers;

	UPROPERTY(EditAnywhere, Category = "Control", meta = (MetaClass = "/Script/EnhancedInput.InputTrigger", ToolTip = "Injected Input Triggers"))
	TArray<TObjectPtr<UObject>> InputTriggers;


	UFUNCTION(BlueprintCallable, Category = "TouchInterface")
	void SetJoystickVisibility(const bool bVisible, const bool bFade);

protected:
	TSharedPtr<ImSlate::SImVirtualJoystick> MyVirtualJoystick;
	void SynchronizePropertiesImpl(TSharedPtr<ImSlate::SImVirtualJoystick> VirtualJoystick) const;

protected:
	IM_SLATE_PALETTECATEGORY()
};