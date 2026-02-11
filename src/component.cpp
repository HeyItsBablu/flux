

#include "flux.hpp"

class StatCard
{
private:
    std::string label;
    int value;
    COLORREF color;

public:
    static WidgetPtr create(const std::string &label, State<int> &state, COLORREF color = RGB(33, 150, 243))
    {
        return Card(
                   Column(
                       Text(label)
                           ->setFontSize(12)
                           ->setTextColor(RGB(150, 150, 150))
                           ->setFontWeight(FontWeight::Normal),

                       Text(state) // Use the State<int> overload directly
                           ->setFontSize(32)
                           ->setFontWeight(FontWeight::Bold)
                           ->setTextColor(color))
                       ->setSpacing(5)
                       ->setPadding(20)
                       ->setCrossAlignment(Alignment::Center))
            ->setBackgroundColor(RGB(250, 250, 250))
            ->setBorderRadius(8)
            ->setMinWidth(150);
    }
};

// ============================================================================
// EXAMPLE USAGE - Main Application
// ============================================================================
class MyApp
{
private:
    FluxUI *ui;
    State<int> clicks;
    State<int> likes;
    State<int> shares;

public:
    MyApp(FluxUI *app)
        : ui(app),
          clicks(0, app),
          likes(42, app),
          shares(17, app)
    {
    }

    void handleClick()
    {
        clicks.set(clicks.get() + 1);
    }

    void handleLike()
    {
        likes.set(likes.get() + 1);
    }

    void handleShare()
    {
        shares.set(shares.get() + 1);
    }

    WidgetPtr build()
    {
        return Center(
            Column(
                StatCard::create("Total Clicks", clicks, RGB(76, 175, 80)),
                StatCard::create("Likes", likes, RGB(33, 150, 243)),
                StatCard::create("Shares", shares, RGB(255, 152, 0)),
                Divider(),

                // Control buttons
                Row(
                    Button("Click", [this]()
                           { handleClick(); })
                        ->setBackgroundColor(RGB(244, 67, 54))
                        ->setWidth(80),

                    Button("Like", [this]()
                           { handleLike(); })
                        ->setBackgroundColor(RGB(158, 158, 158))
                        ->setWidth(80),

                    Button("Share", [this]()
                           { handleShare(); })
                        ->setBackgroundColor(RGB(76, 175, 80))
                        ->setWidth(80))
                    ->setSpacing(10)
                    ->setMainAxisAlignment(MainAxisAlignment::Center))
                ->setSpacing(15)
                ->setMainAxisAlignment(MainAxisAlignment::SpaceEvenly));
    }
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    FluxUI app(hInstance);
    MyApp myApp(&app);

    app.build([&myApp]()
              { return myApp.build(); });

    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
    int width = workArea.right - workArea.left;
    int height = workArea.bottom - workArea.top;

    app.createWindow("Custom Components Demo", width * 0.7, height * 0.7);

    return app.run();
}