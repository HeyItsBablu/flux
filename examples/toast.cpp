#include "flux/flux.hpp"
#include "flux/widgets/flux_file_picker.hpp"

class MyApp : public Component {
    State<bool>        isDark;
    State<std::string> singleFile;
    State<std::string> saveFile;
    State<std::string> folderPath;
    State<std::vector<std::string>> multiFiles;

public:
    MyApp()
        : isDark(false, context),
          singleFile("", context),
          saveFile("", context),
          folderPath("", context),
          multiFiles({}, context) {}

    WidgetPtr build() override {
        return Scaffold(
            AppBar("File Picker + Theme Toggle"),
            Column({

                // ── Theme toggle ──────────────────────────────────────────
                Container(
                    Row({
                        Text("Dark mode")
                            ->setFontSize(14)
                            ->setTextColor(Color::fromRGB(60, 60, 60)),
                        Toggle()
                            ->setValue(isDark)
                            ->setTrackOnColor(Color::fromRGB(99, 102, 241))
                            ->setOnToggleChanged([this](bool v) {
                                FluxAppWidget::getInstance()->setTheme(
                                    v ? AppTheme::dark() : AppTheme::light());
                            }),
                    })->setSpacing(12)
                     ->setCrossAxisAlignment(CrossAxisAlignment::Center)
                )
                ->setPadding(12)
                ->setBackgroundColor(Color::fromRGB(248, 249, 250))
                ->setBorderRadius(8),

                // ── Section: Open single file ─────────────────────────────
                Text("Open Single File")
                    ->setFontSize(13)
                    ->setFontWeight(FontWeight::Bold)
                    ->setTextColor(Color::fromRGB(40, 40, 40)),

                FilePicker()
                    ->setMode(FilePickerMode::Open)
                    ->addFilter("Images",    {"*.png", "*.jpg", "*.jpeg", "*.bmp"})
                    ->addFilter("PNG",       {"*.png"})
                    ->addFilter("JPEG",      {"*.jpg", "*.jpeg"})
                    ->addFilter("All files", {"*.*"})
                    ->setDefaultExtension("png")
                    ->bindPath(singleFile)
                    ->setOnChanged([this](const std::string &path) {
                        std::cout << "[FilePicker] opened: " << path << "\n";
                    }),

                // Show selected path in a text widget too
                Text(singleFile, [](const std::string &p) {
                    return p.empty() ? "No file selected" : p;
                })
                ->setFontSize(11)
                ->setTextColor(Color::fromRGB(120, 120, 130)),

                // ── Section: Open multiple files ──────────────────────────
                Text("Open Multiple Files")
                    ->setFontSize(13)
                    ->setFontWeight(FontWeight::Bold)
                    ->setTextColor(Color::fromRGB(40, 40, 40)),

                FilePicker("Browse multiple...")
                    ->setMode(FilePickerMode::OpenMultiple)
                    ->addFilter("Images", {"*.png", "*.jpg", "*.jpeg"})
                    ->addFilter("All files", {"*.*"})
                    ->bindPaths(multiFiles)
                    ->setOnMultiChanged([](const std::vector<std::string> &paths) {
                        std::cout << "[FilePicker] " << paths.size() << " files:\n";
                        for (auto &p : paths)
                            std::cout << "  " << p << "\n";
                    }),

                Text(multiFiles, [](const std::vector<std::string> &ps) {
                    if (ps.empty()) return std::string("No files selected");
                    return std::to_string(ps.size()) + " file(s) selected";
                })
                ->setFontSize(11)
                ->setTextColor(Color::fromRGB(120, 120, 130)),

                // ── Section: Save dialog ──────────────────────────────────
                Text("Save / Export")
                    ->setFontSize(13)
                    ->setFontWeight(FontWeight::Bold)
                    ->setTextColor(Color::fromRGB(40, 40, 40)),

                FilePicker("Save As...")
                    ->setMode(FilePickerMode::Save)
                    ->setTitle("Export File")
                    ->setDefaultFilename("output.png")
                    ->addFilter("PNG",       {"*.png"})
                    ->addFilter("JPEG",      {"*.jpg", "*.jpeg"})
                    ->addFilter("All files", {"*.*"})
                    ->setDefaultExtension("png")
                    ->bindPath(saveFile)
                    ->setOnChanged([](const std::string &path) {
                        std::cout << "[FilePicker] save to: " << path << "\n";
                    }),

                Text(saveFile, [](const std::string &p) {
                    return p.empty() ? "No destination chosen" : p;
                })
                ->setFontSize(11)
                ->setTextColor(Color::fromRGB(120, 120, 130)),

                // ── Section: Folder picker ────────────────────────────────
                Text("Choose Folder")
                    ->setFontSize(13)
                    ->setFontWeight(FontWeight::Bold)
                    ->setTextColor(Color::fromRGB(40, 40, 40)),

                FilePicker()
                    ->setMode(FilePickerMode::Folder)
                    ->setTitle("Select output folder")
                    ->bindPath(folderPath)
                    ->setOnChanged([](const std::string &path) {
                        std::cout << "[FilePicker] folder: " << path << "\n";
                    }),

                Text(folderPath, [](const std::string &p) {
                    return p.empty() ? "No folder selected" : p;
                })
                ->setFontSize(11)
                ->setTextColor(Color::fromRGB(120, 120, 130)),

            })
            ->setSpacing(10)
            ->setPadding(20)
        );
    }
};

WidgetPtr createApp(FluxUI* app) {
    return FluxApp(
        "FluxUI - Paint",
        BuildComponent<MyApp>(),
        AppTheme::dark(),
        false,   // debugShowWidgetBounds
        900,     // width
        700,     // height
        false,   // maximize
        true     // fullscreen
    );
}