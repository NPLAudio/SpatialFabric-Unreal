// Copyright (c) 2026 AudioLoom Contributors. MIT License.
// Opening this panel never opens a microphone: device lists refresh only on the explicit button, and Start asks first.
#include "AudioLoomLiveInputDetails.h"
#include "AudioLoomLiveInputComponent.h"
#include "AudioInputDeviceEnumerator.h"
#include "AudioOutputDeviceEnumerator.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/MessageDialog.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "AudioLoomLiveInputDetails"

namespace
{
using FDeviceList = TArray<FAudioOutputDeviceInfo>;

TSharedRef<SWidget> MakeDeviceCombo(TSharedPtr<IPropertyHandle> Handle, TSharedRef<FDeviceList> Devices, FText DefaultLabel)
{
	return SNew(SComboButton)
		.OnGetMenuContent_Lambda([Handle, Devices, DefaultLabel]()
		{
			FMenuBuilder Menu(true, nullptr);
			Menu.AddMenuEntry(DefaultLabel, LOCTEXT("DefaultTip", "Use the system default device"), FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([Handle]() { if (Handle.IsValid()) { Handle->SetValue(FString()); Handle->NotifyPostChange(EPropertyChangeType::ValueSet); } })));
			for (const FAudioOutputDeviceInfo& D : *Devices)
			{
				if (!D.bIsValid) continue;
				const FString Id = D.DeviceId;
				Menu.AddMenuEntry(FText::FromString(FString::Printf(TEXT("%s (%d ch / %d Hz)"), *D.FriendlyName, D.NumChannels, D.SampleRate)), FText::FromString(Id), FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([Handle, Id]() { if (Handle.IsValid()) { Handle->SetValue(Id); Handle->NotifyPostChange(EPropertyChangeType::ValueSet); } })));
			}
			return Menu.MakeWidget();
		})
		.ButtonContent()
		[
			SNew(STextBlock).Font(IDetailLayoutBuilder::GetDetailFont())
			.Text_Lambda([Handle, Devices, DefaultLabel]()
			{
				FString Current;
				if (Handle.IsValid() && Handle->GetValue(Current) == FPropertyAccess::Success)
				{
					if (Current.IsEmpty()) return DefaultLabel;
					for (const FAudioOutputDeviceInfo& D : *Devices) if (D.DeviceId == Current) return FText::FromString(D.FriendlyName);
					return FText::Format(LOCTEXT("MissingDevice", "Not in list (ID kept, no fallback): {0}"), FText::FromString(Current));
				}
				return DefaultLabel;
			})
		];
}

TSharedRef<SWidget> MakeChannelCombo(TSharedPtr<IPropertyHandle> ChannelHandle, TSharedPtr<IPropertyHandle> DeviceHandle, TSharedRef<FDeviceList> Devices, FText AllLabel, FText AllTip, bool bCapture)
{
	return SNew(SComboButton)
		.OnGetMenuContent_Lambda([ChannelHandle, DeviceHandle, Devices, AllLabel, AllTip, bCapture]()
		{
			int32 Count = 8; // generous fallback when the device is not in the cached list
			FString Id;
			if (DeviceHandle.IsValid() && DeviceHandle->GetValue(Id) == FPropertyAccess::Success)
			{
				if (Id.IsEmpty())
				{
					const FAudioOutputDeviceInfo Default = bCapture ? FAudioInputDeviceEnumerator::GetDefaultInputDevice() : FAudioOutputDeviceEnumerator::GetDefaultOutputDevice();
					if (Default.bIsValid) Count = Default.NumChannels;
				}
				else for (const FAudioOutputDeviceInfo& D : *Devices) if (D.DeviceId == Id) Count = D.NumChannels;
			}
			FMenuBuilder Menu(true, nullptr);
			Menu.AddMenuEntry(AllLabel, AllTip, FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([ChannelHandle]() { if (ChannelHandle.IsValid()) { ChannelHandle->SetValue(0); ChannelHandle->NotifyPostChange(EPropertyChangeType::ValueSet); } })));
			for (int32 Ch = 1; Ch <= FMath::Clamp(Count, 1, 64); ++Ch)
				Menu.AddMenuEntry(FText::FromString(FString::Printf(TEXT("Channel %d"), Ch)), FText::GetEmpty(), FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([ChannelHandle, Ch]() { if (ChannelHandle.IsValid()) { ChannelHandle->SetValue(Ch); ChannelHandle->NotifyPostChange(EPropertyChangeType::ValueSet); } })));
			return Menu.MakeWidget();
		})
		.ButtonContent()
		[
			SNew(STextBlock).Font(IDetailLayoutBuilder::GetDetailFont())
			.Text_Lambda([ChannelHandle, AllLabel]()
			{
				int32 Value = 0;
				if (ChannelHandle.IsValid() && ChannelHandle->GetValue(Value) == FPropertyAccess::Success && Value > 0) return FText::FromString(FString::Printf(TEXT("Channel %d"), Value));
				return AllLabel;
			})
		];
}
}

TSharedRef<IDetailCustomization> FAudioLoomLiveInputDetails::MakeInstance() { return MakeShareable(new FAudioLoomLiveInputDetails()); }

void FAudioLoomLiveInputDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	TWeakObjectPtr<UAudioLoomLiveInputComponent> Live;
	for (TWeakObjectPtr<UObject>& Object : Objects) if (Object.IsValid()) { Live = Cast<UAudioLoomLiveInputComponent>(Object.Get()); break; }
	if (!Live.IsValid()) return;

	TSharedPtr<IPropertyHandle> InputId = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UAudioLoomLiveInputComponent, InputDeviceId));
	TSharedPtr<IPropertyHandle> InputCh = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UAudioLoomLiveInputComponent, InputChannel));
	TSharedPtr<IPropertyHandle> OutputId = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UAudioLoomLiveInputComponent, OutputDeviceId));
	TSharedPtr<IPropertyHandle> OutputCh = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UAudioLoomLiveInputComponent, OutputChannel));
	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("AudioLoom|Live Input", LOCTEXT("LiveCategory", "Live Input"));

	// Device lists are captured once; only the explicit refresh button re-enumerates them.
	TSharedRef<FDeviceList> Inputs = MakeShared<FDeviceList>(FAudioInputDeviceEnumerator::GetInputDevices());
	TSharedRef<FDeviceList> Outputs = MakeShared<FDeviceList>(FAudioOutputDeviceEnumerator::GetOutputDevices());

	Category.AddProperty(InputId).CustomWidget()
		.NameContent()[SNew(STextBlock).Text(LOCTEXT("InputDevice", "Input Device")).Font(IDetailLayoutBuilder::GetDetailFont())]
		.ValueContent().MinDesiredWidth(280.f)[MakeDeviceCombo(InputId, Inputs, LOCTEXT("DefaultInput", "Default Input"))];
	Category.AddProperty(InputCh).CustomWidget()
		.NameContent()[SNew(STextBlock).Text(LOCTEXT("InputChannel", "Input Channel")).Font(IDetailLayoutBuilder::GetDetailFont())]
		.ValueContent().MinDesiredWidth(160.f)[MakeChannelCombo(InputCh, InputId, Inputs, LOCTEXT("AllInputs", "All inputs (0)"), LOCTEXT("AllInputsTip", "Pass every input channel"), true)];
	Category.AddProperty(OutputId).CustomWidget()
		.NameContent()[SNew(STextBlock).Text(LOCTEXT("OutputDevice", "Output Device")).Font(IDetailLayoutBuilder::GetDetailFont())]
		.ValueContent().MinDesiredWidth(280.f)[MakeDeviceCombo(OutputId, Outputs, LOCTEXT("DefaultOutput", "Default Output"))];
	Category.AddProperty(OutputCh).CustomWidget()
		.NameContent()[SNew(STextBlock).Text(LOCTEXT("OutputChannel", "Output Channel")).Font(IDetailLayoutBuilder::GetDetailFont())]
		.ValueContent().MinDesiredWidth(160.f)[MakeChannelCombo(OutputCh, OutputId, Outputs, LOCTEXT("AllOutputs", "Corresponding (0)"), LOCTEXT("AllOutputsTip", "Corresponding channels; a single selected input lands on output 1"), false)];
	Category.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UAudioLoomLiveInputComponent, bUseExclusiveOutput)));
	Category.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UAudioLoomLiveInputComponent, BufferSizeMs)));
	Category.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UAudioLoomLiveInputComponent, Gain)));

	Category.AddCustomRow(LOCTEXT("LiveActions", "Live actions"))
		.WholeRowContent()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
				[
					SNew(SButton).Text(LOCTEXT("Refresh", "Refresh devices"))
					.ToolTipText(LOCTEXT("RefreshTip", "Re-list input and output devices. Opens no stream and requests no microphone access."))
					.OnClicked_Lambda([Inputs, Outputs]()
					{
						*Inputs = FAudioInputDeviceEnumerator::GetInputDevices();
						*Outputs = FAudioOutputDeviceEnumerator::GetOutputDevices();
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
				[
					SNew(SButton).Text(LOCTEXT("StartLive", "Start live"))
					.ToolTipText(LOCTEXT("StartTip", "Opens the selected microphone/line input (the OS may ask permission) and plays it through the real output."))
					.OnClicked_Lambda([Live]()
					{
						if (UAudioLoomLiveInputComponent* C = Live.Get())
						{
							const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT("ConfirmStart",
								"Start live input?\n\nThis opens the selected microphone or line input (the OS may prompt for permission) and plays it through the selected output. Use headphones or a low gain to avoid acoustic feedback."));
							if (Answer == EAppReturnType::Yes) C->StartLive();
						}
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton).Text(LOCTEXT("StopLive", "Stop"))
					.OnClicked_Lambda([Live]() { if (UAudioLoomLiveInputComponent* C = Live.Get()) C->StopLive(); return FReply::Handled(); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
			[
				SNew(STextBlock).Font(IDetailLayoutBuilder::GetDetailFont())
				.Text_Lambda([Live]()
				{
					const UAudioLoomLiveInputComponent* C = Live.Get();
					if (!C) return FText::GetEmpty();
					return FText::FromString(FString::Printf(TEXT("%s  in %d ch / %d Hz  out %d ch / %d Hz  buffered %.0f ms  underruns %d  overruns %d"),
						C->IsLive() ? TEXT("LIVE (worker started)") : TEXT("Not live"), C->GetActualInputChannels(), C->GetActualInputRate(),
						C->GetActualOutputChannels(), C->GetActualOutputRate(), C->GetLiveLatencyMs(), C->GetUnderruns(), C->GetOverruns()));
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
			[
				SNew(SBox).HeightOverride(10.f)
				[
					SNew(SProgressBar).Percent_Lambda([Live]() { const UAudioLoomLiveInputComponent* C = Live.Get(); return C ? TOptional<float>(C->GetInputPeak()) : TOptional<float>(0.f); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
			[
				SNew(STextBlock).Font(IDetailLayoutBuilder::GetDetailFont()).AutoWrapText(true).ColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.6f, 0.2f)))
				.Text_Lambda([Live]() { const UAudioLoomLiveInputComponent* C = Live.Get(); return C ? FText::FromString(C->GetLastLiveError()) : FText::GetEmpty(); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
			[
				SNew(STextBlock).Font(IDetailLayoutBuilder::GetDetailFont()).AutoWrapText(true)
				.Text(LOCTEXT("Note", "Live means the native worker really started, not that Start was requested. Exclusive applies to the output only. Monitoring a microphone through nearby speakers causes feedback."))
			]
		];
}
#undef LOCTEXT_NAMESPACE
