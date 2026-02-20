// Simple Pulse — NowPlaying + TrackList

#include "flux.hpp"

constexpr COLORREF BG      = RGB( 14,  14,  20);
constexpr COLORREF SURFACE = RGB( 24,  24,  36);
constexpr COLORREF ACCENT  = RGB(255, 180,  60);
constexpr COLORREF TEXT1   = RGB(235, 230, 215);
constexpr COLORREF TEXT2   = RGB(130, 120, 105);

struct Track {
    std::string title;
    std::string artist;
    std::string duration;
};

static const std::vector<Track> TRACKS = {
    { "Midnight Architecture", "Neon Vessel",  "5:42" },
    { "Amber Static",          "Pale Ghost",   "4:18" },
    { "The Long Descent",      "Orbweaver",    "7:01" },
    { "Copper Signal",         "Neon Vessel",  "3:55" },
    { "Frozen Latitude",       "Mirelle Sanz", "6:13" },
};


// ── NowPlaying ────────────────────────────────────────────────
class NowPlaying : public Component {
    State<int>* current;

public:
    explicit NowPlaying(State<int>* current) : current(current) {}

    WidgetPtr build() override {
        return Card(
            Column(
                Text("NOW PLAYING")
                    ->setFontSize(10)
                    ->setTextColor(ACCENT),

                Text(*current, [](int i) { return TRACKS[i].title; })
                    ->setFontSize(22)
                    ->setFontWeight(FontWeight::Bold)
                    ->setTextColor(TEXT1),

                Text(*current, [](int i) { return TRACKS[i].artist; })
                    ->setFontSize(13)
                    ->setTextColor(TEXT2),

                SizedBox(0, 8),

                ProgressBar(0.4)
                    ->setProgressColors({ ACCENT })
                    ->setHeight(4)
                    ->setBorderRadius(2),

                Row(
                    Text("2:18")
                        ->setFontSize(11)
                        ->setTextColor(TEXT2),
                    Expanded(SizedBox(0, 0)),
                    Text(*current, [](int i) { return TRACKS[i].duration; })
                        ->setFontSize(11)
                        ->setTextColor(TEXT2)
                )
            )->setSpacing(6)
        )->setHeight(160);  // fixed height so parent Column knows its size
    }
};


// ── TrackList ─────────────────────────────────────────────────
class TrackList : public Component {
    State<int>*               current;
    State<std::vector<Track>> tracks;

public:
    explicit TrackList(State<int>* current)
        : current(current), tracks(TRACKS, context) {}

    WidgetPtr build() override {
        return Card(
            Column(
                Text("QUEUE")
                    ->setFontSize(10)
                    ->setTextColor(ACCENT),

                Divider(),

                ListView(tracks)
                    ->itemBuilder([this](int i, const Track& t) -> WidgetPtr {
                        return GestureDetector(
                            Container(
                                Row(
                                    Text(*current, [i](int sel) -> std::string {
                                        return sel == i ? "▶" : std::to_string(i + 1);
                                    })->setFontSize(12)
                                      ->setTextColor(TEXT2)
                                      ->setMinWidth(24),

                                    Expanded(
                                        Column(
                                            Text(t.title)
                                                ->setFontSize(13)
                                                ->setFontWeight(FontWeight::Bold)
                                                ->setTextColor(TEXT1),
                                            Text(t.artist)
                                                ->setFontSize(11)
                                                ->setTextColor(TEXT2)
                                        )->setSpacing(2)
                                    ),

                                    Text(t.duration)
                                        ->setFontSize(11)
                                        ->setTextColor(TEXT2)

                                )->setSpacing(12)
                                 ->setCrossAlignment(Alignment::Center)
                            )->setBackgroundColor(BG)
                             ->setHoverBackgroundColor(SURFACE)
                             ->setBorderRadius(6)
                             ->setPadding(10)
                             ->setHeight(100)
                        )->setOnTap([this, i]{ current->set(i); });
                    })
                    ->separator([]{ return SizedBox(0, 4); })

            )->setSpacing(8)
        )->setFlex(1);  // stretch to fill Expanded parent
    }
};


// ── Root ──────────────────────────────────────────────────────
class PulseApp : public Component {
    State<int> current;

public:
    PulseApp() : current(0, context) {}

    WidgetPtr build() override {
        return Scaffold(
            AppBar("PULSE"),
            Column(
                CHILD(NowPlaying, &current),   // fixed 160px
                Expanded(
                    CHILD(TrackList, &current)  // fills remaining space
                )
            )->setSpacing(12)
             ->setPadding(16)
             ->setBackgroundColor(BG)
        );
    }
};


// ── Entry Point ───────────────────────────────────────────────
WidgetPtr createApp(FluxUI* app) {
    return FluxApp("PULSE", BuildComponent<PulseApp>(), AppTheme::materialBlue());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    GdiplusInitializer gdiplusInit;
    FluxUI app(hInstance);

    app.build([&]() { return createApp(&app); });

    int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    app.createWindow("PULSE — Music Player", screenWidth, screenHeight);

    return app.run();
}