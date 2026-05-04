#include "flux/flux.hpp"

class ToastTestApp : public Widget {
    std::shared_ptr<ToastWidget> toast;

public:
    WidgetPtr build() override {

        toast = Toast()
            ->setPosition(ToastPosition::BottomRight)
            ->setMaxVisible(4)
            ->setToastWidth(340);

        return Scaffold(
            AppBar("Toast Test"),
            Center(
                Container(
                    Column({

                        Text("Basic Toasts")
                            ->setFontSize(15)
                            ->setFontWeight(FontWeight::Bold),

                        Row({
                            Button("Info", [this]{
                                toast->show("This is an info message.",
                                            ToastType::Info, 3000);
                            }),
                            Button("Success", [this]{
                                toast->show("File saved successfully!",
                                            ToastType::Success, 3000);
                            })->setBackgroundColor(Color::fromRGB(76, 175, 80)),
                            Button("Warning", [this]{
                                toast->show("Disk space is low.",
                                            ToastType::Warning, 4000);
                            })->setBackgroundColor(Color::fromRGB(255, 152, 0)),
                            Button("Error", [this]{
                                toast->show("Upload failed.",
                                            ToastType::Error, 5000);
                            })->setBackgroundColor(Color::fromRGB(244, 67, 54)),
                        })
                        ->setSpacing(8)
                        ->setCrossAxisAlignment(CrossAxisAlignment::Center),

                        Text("With Title")
                            ->setFontSize(15)
                            ->setFontWeight(FontWeight::Bold),

                        Button("Show titled toast", [this]{
                            ToastEntry e;
                            e.title      = "Connection lost";
                            e.message    = "Unable to reach the server. "
                                           "Check your network.";
                            e.type       = ToastType::Error;
                            e.durationMs = 5000;
                            toast->showEntry(e);
                        }),

                        Text("With Action Button")
                            ->setFontSize(15)
                            ->setFontWeight(FontWeight::Bold),

                        Button("Show action toast", [this]{
                            ToastEntry e;
                            e.message     = "Message sent to trash.";
                            e.type        = ToastType::Info;
                            e.durationMs  = 5000;
                            e.actionLabel = "Undo";
                            e.onAction    = []{
                                std::cout << "Undo clicked!" << std::endl;
                            };
                            toast->showEntry(e);
                        }),

                        Text("Sticky (no auto-dismiss)")
                            ->setFontSize(15)
                            ->setFontWeight(FontWeight::Bold),

                        Button("Show sticky toast", [this]{
                            ToastEntry e;
                            e.title      = "Update available";
                            e.message    = "Click × to dismiss manually.";
                            e.type       = ToastType::Warning;
                            e.durationMs = 0;  // sticky
                            toast->showEntry(e);
                        }),

                        Button("Dismiss all", [this]{
                            toast->dismissAll();
                        })->setBackgroundColor(Color::fromRGB(120, 120, 120)),

                    })
                    ->setSpacing(14)
                    ->setCrossAxisAlignment(CrossAxisAlignment::Start)
                )
                ->setWidth(500)
                ->setPadding(28)
                ->setBorderRadius(10)
                ->setBackgroundColor(Color::fromRGB(255, 255, 255))
            ),
            nullptr, // no FAB
            toast    // pass toast as overlay anchor to scaffold
        );
    }
};

WidgetPtr createApp(FluxUI *app) {
    return FluxApp(
        "Toast Test",
        std::make_shared<ToastTestApp>(),
        AppTheme::light(),
        false, 680, 560, true, false
    );
}