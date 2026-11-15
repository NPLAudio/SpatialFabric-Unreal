// Copyright (c) 2026 AudioLoom Contributors. MIT License.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/SAudioLoomPanel.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"
#include "Styling/AppStyle.h"

namespace
{
TSharedPtr<SWidget> FindEndpointEditor(const TSharedRef<SWidget>& Widget, FName Tag)
{
    if (Widget->GetTag() == Tag) return Widget;
    FChildren* Children = Widget->GetChildren();
    for (int32 Index = 0; Index < Children->Num(); ++Index)
    {
        if (auto Found = FindEndpointEditor(Children->GetChildAt(Index), Tag)) return Found;
    }
    return nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioLoomEndpointLayoutTest,
    "SpatialFabric.Playback.EndpointEditorsFitFullIPv4AndPort",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAudioLoomEndpointLayoutTest::RunTest(const FString&)
{
    if (!TestTrue(TEXT("Slate initialized"), FSlateApplication::IsInitialized())) return false;
    const TSharedRef<SAudioLoomPanel> Panel = SNew(SAudioLoomPanel);
    const auto Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    const auto Font = FAppStyle::GetFontStyle("NormalFont");
    const float IPTextWidth = Measure->Measure(TEXT("255.255.255.255"), Font).X;
    const float PortTextWidth = Measure->Measure(TEXT("65535"), Font).X;
    for (float Scale : {1.f, 1.5f, 2.f})
    {
        Panel->SlatePrepass(Scale);
        for (const TCHAR* Name : {TEXT("AudioLoom.ListenIP"), TEXT("AudioLoom.SendIP"),
                                 TEXT("AudioLoom.ListenPort"), TEXT("AudioLoom.SendPort")})
        {
            const auto Field = FindEndpointEditor(Panel, FName(Name));
            if (!TestTrue(FString::Printf(TEXT("Endpoint editor exists: %s"), Name), Field.IsValid())) continue;
            const bool bIP = FString(Name).EndsWith(TEXT("IP"));
            const float Width = Field->GetDesiredSize().X;
            TestTrue(FString::Printf(TEXT("%s reserves full editor width at scale %.1f"), Name, Scale),
                     Width >= (bIP ? 192.f : 112.f));
            TestTrue(FString::Printf(TEXT("%s fits all digits plus editor chrome"), Name),
                     Width >= (bIP ? IPTextWidth + 24.f : PortTextWidth + 48.f));
        }
    }
    return true;
}
#endif
