#include "flux/flux.hpp"

#include <numeric>

class MyApp : public Component {
  State<int> boxCount;
  State<double> fillRatio;
  State<std::vector<int>> boxIndices;

  // Toast widget pointer — declared as member so buttons can call it
  ToastWidgetPtr toast_;

  static constexpr int kMax = 30;

  void rebuildIndices(int count) {
    std::vector<int> v(count);
    std::iota(v.begin(), v.end(), 0);
    boxIndices.set(v);
    fillRatio.set((double)count / kMax);
  }

public:
  MyApp()
      : boxCount(9, context), fillRatio(9.0 / kMax, context),
        boxIndices({}, context) {}

  void initState() override { rebuildIndices(boxCount.get()); }

  WidgetPtr build() override {

    // ── Create the toast anchor (zero-size, placed inside Scaffold) ─────────
    toast_ = Toast()
        ->setPosition(ToastPosition::BottomRight)
        ->setMaxVisible(3)
        ->setToastWidth(300)
        ->setSpacing(8)
        ->setMarginEdge(16);

    return Scaffold(
        AppBar("Layout Stress Test"),
        Column(
            {

                // ── Controls ─────────────────────────────────────────────
                Row({

                        Row({
                                Icon(FluxIcons::Remove, "Segoe MDL2 Assets", 14)
                                    ->setColor(Color::fromRGB(255, 255, 255)),
                                Text("  -3"),
                            })
                            ->setSpacing(0),

                        Button(Icon(FluxIcons::Remove)
                                   ->setColor(Color::fromRGB(255, 255, 255)),
                               [this] {
                                 int n = std::max(1, boxCount.get() - 3);
                                 boxCount.set(n);
                                 rebuildIndices(n);
                               })
                            ->setBackgroundColor(Color::fromRGB(76, 175, 80)),

                        Text(
                            boxCount,
                            [](int v) { return std::to_string(v) + " boxes"; }),

                        Button(
                            Icon(FluxIcons::Add)->setColor(Color::fromRGB(255, 255, 255)),
                            [this] {
                              int n = std::min(kMax, boxCount.get() + 3);
                              boxCount.set(n);
                              rebuildIndices(n);
                            })
                            ->setBackgroundColor(Color::fromRGB(76, 175, 80)),

                        Row({
                                Icon(FluxIcons::Add, "Segoe MDL2 Assets", 14)
                                    ->setColor(Color::fromRGB(255, 255, 255)),
                                Text("  +3"),
                            })
                            ->setSpacing(0),

                    })
                    ->setSpacing(8)
                    ->setPadding(12),

                // ── Progress ─────────────────────────────────────────────
                Column({
                           Row({
                                   Icon(FluxIcons::ChartBar,
                                        "Segoe MDL2 Assets", 13)
                                       ->setColor(Color::fromRGB(100, 100, 100)),

                                   Text(boxCount,
                                        [](int v) {
                                          return "  " + std::to_string(v) +
                                                 " / " + std::to_string(kMax) +
                                                 " boxes";
                                        })
                                       ->setFontSize(12)
                                       ->setTextColor(Color::fromRGB(100, 100, 100)),
                               })
                               ->setSpacing(0),

                           SizedBox(0, 4),

                           ProgressBar()
                               ->setValue(fillRatio)
                               ->setHeight(8)
                               ->setProgressColors(
                                   {Color::fromRGB(66, 165, 245), Color::fromRGB(102, 187, 106)})
                               ->setBorderRadius(4),

                       })
                    ->setSpacing(0)
                    ->setPadding(12),

                Divider(),

                // ── Toast Demo Panel ──────────────────────────────────────
                Container(
                    Column({
                        // Header
                        Row({
                                Icon(FluxIcons::Check, "Segoe MDL2 Assets", 14)
                                    ->setColor(Color::fromRGB(33, 150, 243)),
                                Text("  Toast Notifications")
                                    ->setFontWeight(FontWeight::Bold),
                            })
                            ->setSpacing(0),

                        SizedBox(0, 10),

                        // Row 1 — four toast types
                        Row({
                                // Info
                                Button(
                                    Row({
                                        Icon(FluxIcons::Info, "Segoe MDL2 Assets", 13)
                                            ->setColor(Color::fromRGB(255, 255, 255)),
                                        Text("  Info"),
                                    })->setSpacing(0),
                                    [this] {
                                        toast_->show(
                                            "Background sync completed successfully.",
                                            ToastType::Info, 3000);
                                    })
                                    ->setBackgroundColor(Color::fromRGB(33, 150, 243))
                                    ->setBorderRadius(4)
                                    ->setPadding(8),

                                // Success
                                Button(
                                    Row({
                                        Icon(FluxIcons::Check, "Segoe MDL2 Assets", 13)
                                            ->setColor(Color::fromRGB(255, 255, 255)),
                                        Text("  Success"),
                                    })->setSpacing(0),
                                    [this] {
                                        toast_->show(
                                            "File saved successfully.",
                                            ToastType::Success, 3000);
                                    })
                                    ->setBackgroundColor(Color::fromRGB(76, 175, 80))
                                    ->setBorderRadius(4)
                                    ->setPadding(8),

                                // Warning
                                Button(
                                    Row({
                                        Icon(FluxIcons::Warning, "Segoe MDL2 Assets", 13)
                                            ->setColor(Color::fromRGB(255, 255, 255)),
                                        Text("  Warning"),
                                    })->setSpacing(0),
                                    [this] {
                                        toast_->show(
                                            "Disk space is running low (< 10% free).",
                                            ToastType::Warning, 4000);
                                    })
                                    ->setBackgroundColor(Color::fromRGB(255, 152, 0))
                                    ->setBorderRadius(4)
                                    ->setPadding(8),

                                // Error
                                Button(
                                    Row({
                                        Icon(FluxIcons::Error, "Segoe MDL2 Assets", 13)
                                            ->setColor(Color::fromRGB(255, 255, 255)),
                                        Text("  Error"),
                                    })->setSpacing(0),
                                    [this] {
                                        toast_->show(
                                            "Failed to connect to the server.",
                                            ToastType::Error, 4000);
                                    })
                                    ->setBackgroundColor(Color::fromRGB(244, 67, 54))
                                    ->setBorderRadius(4)
                                    ->setPadding(8),

                            })
                            ->setSpacing(8),

                        SizedBox(0, 8),

                        // Row 2 — advanced toast types
                        Row({
                                // Toast with title
                                Button(
                                    Text("With Title"),
                                    [this] {
                                        toast_->showEntry({
                                            .message    = "Upload failed — check your connection.",
                                            .title      = "Network Error",
                                            .type       = ToastType::Error,
                                            .durationMs = 5000,
                                        });
                                    })
                                    ->setBackgroundColor(Color::fromRGB(244, 67, 54))
                                    ->setBorderRadius(4)
                                    ->setPadding(8),

                                // Sticky toast with action button
                                Button(
                                    Text("Sticky + Action"),
                                    [this] {
                                        toast_->showEntry({
                                            .message     = "A new version is available.",
                                            .title       = "Update Ready",
                                            .type        = ToastType::Info,
                                            .durationMs  = 0,        // sticky
                                            .actionLabel = "Restart",
                                            .onAction    = [this] {
                                                toast_->show(
                                                    "Restarting application…",
                                                    ToastType::Success, 2500);
                                            },
                                        });
                                    })
                                    ->setBackgroundColor(Color::fromRGB(33, 150, 243))
                                    ->setBorderRadius(4)
                                    ->setPadding(8),

                                // Queued burst (fires 4 toasts, max 3 visible at once)
                                Button(
                                    Text("Queue Burst"),
                                    [this] {
                                        toast_->show("Task 1 complete",   ToastType::Success, 2500);
                                        toast_->show("Task 2 complete",   ToastType::Success, 2500);
                                        toast_->show("Task 3 complete",   ToastType::Info,    2500);
                                        toast_->show("All tasks finished",ToastType::Info,    3000);
                                    })
                                    ->setBackgroundColor(Color::fromRGB(102, 187, 106))
                                    ->setBorderRadius(4)
                                    ->setPadding(8),

                                // Dismiss all
                                Button(
                                    Text("Dismiss All"),
                                    [this] {
                                        toast_->dismissAll();
                                    })
                                    ->setBackgroundColor(Color::fromRGB(150, 150, 150))
                                    ->setBorderRadius(4)
                                    ->setPadding(8),

                            })
                            ->setSpacing(8),

                        SizedBox(0, 8),

                        // Row 3 — position switcher
                        Row({
                                Text("Position:")
                                    ->setFontSize(12)
                                    ->setTextColor(Color::fromRGB(100, 100, 100)),

                                Button(Text("↙ BL"), [this] {
                                    toast_->setPosition(ToastPosition::BottomLeft);
                                    toast_->show("Bottom-left", ToastType::Info, 2000);
                                })->setBorderRadius(4)->setPadding(6),

                                Button(Text("↓ BC"), [this] {
                                    toast_->setPosition(ToastPosition::BottomCenter);
                                    toast_->show("Bottom-center", ToastType::Info, 2000);
                                })->setBorderRadius(4)->setPadding(6),

                                Button(Text("↘ BR"), [this] {
                                    toast_->setPosition(ToastPosition::BottomRight);
                                    toast_->show("Bottom-right", ToastType::Info, 2000);
                                })->setBorderRadius(4)->setPadding(6),

                                Button(Text("↖ TL"), [this] {
                                    toast_->setPosition(ToastPosition::TopLeft);
                                    toast_->show("Top-left", ToastType::Info, 2000);
                                })->setBorderRadius(4)->setPadding(6),

                                Button(Text("↑ TC"), [this] {
                                    toast_->setPosition(ToastPosition::TopCenter);
                                    toast_->show("Top-center", ToastType::Info, 2000);
                                })->setBorderRadius(4)->setPadding(6),

                                Button(Text("↗ TR"), [this] {
                                    toast_->setPosition(ToastPosition::TopRight);
                                    toast_->show("Top-right", ToastType::Info, 2000);
                                })->setBorderRadius(4)->setPadding(6),

                            })
                            ->setSpacing(6),

                    })
                    ->setSpacing(0))
                    ->setPadding(12)
                    ->setBackgroundColor(Color::fromRGB(250, 250, 255))
                    ->setBorderColor(Color::fromRGB(220, 220, 230))
                    ->setBorderWidth(1),

                Divider(),

                // ── Body ────────────────────────────────────────────────
                Expanded(
                    Row({

                            // ── Left Panel
                            Container(
                                Column(
                                    {

                                        Row({
                                                Icon(FluxIcons::Dashboard,
                                                     "Segoe MDL2 Assets", 14)
                                                    ->setColor(
                                                        Color::fromRGB(33, 150, 243)),

                                                Text("  Left Panel")
                                                    ->setFontWeight(
                                                        FontWeight::Bold),
                                            })
                                            ->setSpacing(0),

                                        SizedBox(0, 8), Divider(),
                                        SizedBox(0, 8),

                                        Text("Active boxes")
                                            ->setFontSize(11)
                                            ->setTextColor(Color::fromRGB(120, 120, 120)),

                                        Text(boxCount,
                                             [](int v) {
                                               return std::to_string(v);
                                             })
                                            ->setFontSize(28)
                                            ->setFontWeight(FontWeight::Bold),

                                        SizedBox(0, 8),

                                        Text("Capacity")
                                            ->setFontSize(11)
                                            ->setTextColor(Color::fromRGB(120, 120, 120)),

                                        SizedBox(0, 4),

                                        ProgressBar()
                                            ->setValue(fillRatio)
                                            ->setHeight(6)
                                            ->setProgressColors(
                                                {Color::fromRGB(66, 165, 245),
                                                 Color::fromRGB(102, 187, 106)})
                                            ->setBorderRadius(3),

                                        SizedBox(0, 12),

                                        Row({
                                                Icon(FluxIcons::Check,
                                                     "Segoe MDL2 Assets", 14)
                                                    ->setColor(
                                                        Color::fromRGB(76, 175, 80)),

                                                Text("  Running")
                                                    ->setFontSize(12)
                                                    ->setTextColor(
                                                        Color::fromRGB(76, 175, 80)),
                                            })
                                            ->setSpacing(0),

                                        SizedBox(0, 12),

                                        // ── Toast shortcut inside left panel
                                        Button(
                                            Row({
                                                Icon(FluxIcons::Cancel,
                                                     "Segoe MDL2 Assets", 12)
                                                    ->setColor(Color::fromRGB(255, 255, 255)),
                                                Text("  Notify")
                                                    ->setFontSize(12),
                                            })->setSpacing(0),
                                            [this] {
                                                toast_->showEntry({
                                                    .message     = std::to_string(boxCount.get()) +
                                                                   " boxes are currently active.",
                                                    .title       = "Status Update",
                                                    .type        = ToastType::Success,
                                                    .durationMs  = 3500,
                                                });
                                            })
                                            ->setBackgroundColor(Color::fromRGB(33, 150, 243))
                                            ->setBorderRadius(4)
                                            ->setPadding(8),

                                        GestureDetector(Text("Hello Gesture"))
                                            ->setOnTap([this] {
                                              toast_->show("Tapped!", ToastType::Info, 2000);
                                              std::cout << "Tapped box " << +1 << std::endl;
                                            })
                                            ->setOnDoubleTap([this] {
                                              toast_->show("Double tapped!", ToastType::Warning, 2000);
                                              std::cout << "Double tapped box " << +1 << std::endl;
                                            })
                                            ->setOnSecondaryTap([this] {
                                              toast_->show("Right clicked!", ToastType::Error, 2000);
                                              std::cout << "Right click box " << +1 << std::endl;
                                            })

                                    })
                                    ->setSpacing(4))
                                ->setWidth(200)
                                ->setPadding(16)
                                ->setBackgroundColor(Color::fromRGB(245, 245, 250))
                                ->setBorderColor(Color::fromRGB(220, 220, 230))
                                ->setBorderWidth(1),

                            // ── Right Panel
                            Expanded(
                                Column(
                                    {

                                        Row({
                                                Icon(FluxIcons::Grid,
                                                     "Segoe MDL2 Assets", 13)
                                                    ->setColor(
                                                        Color::fromRGB(120, 120, 120)),

                                                Text("  GridView (reactive)")
                                                    ->setFontSize(12)
                                                    ->setTextColor(
                                                        Color::fromRGB(120, 120, 120)),
                                            })
                                            ->setSpacing(0),

                                        SizedBox(0, 8),

                                        Expanded(
                                            Column(
                                                {Text("GridView (reactive)")
                                                     ->setFontSize(12)
                                                     ->setTextColor(
                                                         Color::fromRGB(120, 120, 120)),
                                                 SizedBox(0, 8),
                                                 Expanded(
                                                     GridView(boxIndices)
                                                         ->columns(3)
                                                         ->itemBuilder([this](int i,
                                                                          const int &idx)
                                                                           -> WidgetPtr {
                                                           static const Color colors[] = {
                                                               Color::fromRGB(239, 83, 80),
                                                               Color::fromRGB(66, 165, 245),
                                                               Color::fromRGB(102, 187, 106),
                                                               Color::fromRGB(255, 167, 38),
                                                               Color::fromRGB(171, 71, 188),
                                                               Color::fromRGB(38, 198, 218),
                                                           };
                                                           Color c = colors[idx % 6];

                                                           // Tap a grid box to toast its number
                                                           return GestureDetector(
                                                               Container(
                                                                   Center(
                                                                       Text(std::to_string(idx + 1))
                                                                           ->setTextColor(Color::fromRGB(255, 255, 255))
                                                                           ->setFontWeight(FontWeight::Bold)))
                                                               ->setHeight(80)
                                                               ->setBackgroundColor(c)
                                                               ->setBorderRadius(6))
                                                               ->setOnTap([this, idx] {
                                                                   toast_->show(
                                                                       "Box " + std::to_string(idx + 1) + " tapped.",
                                                                       ToastType::Info, 1500);
                                                               });
                                                         })
                                                         ->setSpacing(8))})
                                                ->setSpacing(0)
                                                ->setPadding(12))

                                    })
                                    ->setSpacing(0)
                                    ->setPadding(12)),

                        })
                        ->setSpacing(0)),

                // ── Toast anchor (zero-size, must live inside Scaffold) ──────
                toast_,

            })
            ->setSpacing(0));
  }
};

WidgetPtr createApp(FluxUI* app) {
    return FluxApp(
        "FluxUI - Paint",
        BuildComponent<MyApp>(),
        AppTheme::light(),
        false,   // debugShowWidgetBounds
        900,     // width
        700,     // height
        false,   // maximize
        true     // fullscreen
    );
}