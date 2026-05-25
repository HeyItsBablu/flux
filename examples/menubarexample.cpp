#include "flux/flux.hpp"

class MyApp : public Widget
{
    State<std::string> statusText{"Ready."};

public:
    WidgetPtr build() override
    {
        auto menuBar = MenuBar({

            MenuBarItem(
                "File",
                {
                    ContextMenuItem::Action(
                        "New", [this]
                        { statusText.set("New file."); }),
                    ContextMenuItem::Action(
                        "Open", [this]
                        { statusText.set("Open file."); }),
                    ContextMenuItem::Action("Save",
                                            [this]
                                            { statusText.set("Saved."); }),
                    ContextMenuItem::Separator(),
                    ContextMenuItem::Action("Exit", []
                                            { PostQuitMessage(0); }),
                }),

            MenuBarItem("Edit",
                        {
                            ContextMenuItem::Action(
                                "Cut", [this]
                                { statusText.set("Cut."); }),
                            ContextMenuItem::Action(
                                "Copy", [this]
                                { statusText.set("Copied."); }),
                            ContextMenuItem::Action(
                                "Paste", [this]
                                { statusText.set("Pasted."); }),
                        }),

            MenuBarItem(
                "View",
                {
                    ContextMenuItem::Action("Zoom In",
                                            [this]
                                            { statusText.set("Zoom in."); }),
                    ContextMenuItem::Action(
                        "Zoom Out", [this]
                        { statusText.set("Zoom out."); }),
                    ContextMenuItem::Separator(),
                    ContextMenuItem::Action(
                        "Full Screen", [this]
                        { statusText.set("Full screen."); }),
                }),

            MenuBarItem("Help",
                        {
                            ContextMenuItem::Action(
                                "About",
                                [this]
                                { statusText.set("FluxUI MenuBar demo."); }),
                        }),
        });

        return Scaffold(
            nullptr,

            // ── Body ──────────────────────────────────────────────────────────
            Expanded(Column({
                                menuBar,
                                Text("Click a menu above.")
                                    ->setFontSize(15)
                                    ->setTextColor(Color::fromRGB(100, 100, 100)),
                                SizedBox(0, 16),
                                Text(statusText)
                                    ->setFontSize(20)
                                    ->setFontWeight(FontWeight::Bold),
                            })
                         ->setSpacing(0)),
            nullptr, nullptr);
    }
};

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp(
        "FluxUI - Paint",
        std::make_shared<MyApp>(),
        AppTheme::light(),
        false, // debugShowWidgetBounds
        900,   // width
        700,   // height
        false, // maximize
        true   // fullscreen
    );
}