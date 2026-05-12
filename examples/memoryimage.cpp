#include "flux/flux.hpp"
#include <fstream>
#include <vector>

class MyApp : public Widget {
  State<std::vector<uint8_t>> imageBytes{{}};
  State<std::string> pickedName{""};

public:
  WidgetPtr build() override {

    return Scaffold(
        AppBar("MemoryImage Demo"),
        Column({// ── Pick button ───────────────────────────────────────
                FilePicker()
                    ->setMode(FilePickerMode::Open)
                    ->addFilter("Images", {"*.png", "*.jpg", "*.jpeg", "*.bmp"})
                    ->addFilter("All Files", {"*.*"})
                    ->setShowPath(false)
                    ->setOnChanged([this](const std::string &path) {
                      if (path.empty())
                        return;

                      // Read file bytes into a vector
                      std::ifstream file(path, std::ios::binary);
                      if (!file)
                        return;
                      std::vector<uint8_t> bytes(
                          (std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
                      file.close();

                      // Extract filename for display
                      auto pos = path.find_last_of("\\/");
                      pickedName.set(pos != std::string::npos
                                         ? path.substr(pos + 1)
                                         : path);

                      // Push bytes into state → triggers rebuild
                      imageBytes.set(std::move(bytes));
                    }),

                // ── Filename label ────────────────────────────────────
                Text(pickedName,
                     [](const std::string &n) -> std::string {
                       return n.empty() ? "No file selected" : n;
                     })
                    ->setTextColor(Color::fromRGB(80, 80, 90))
                    ->setFontSize(13),

                Conditional(imageBytes,
                            [](std::vector<uint8_t> v) { return v.empty(); })
                    ->Then([]() {
                      return Container(Text("Select Image")
                                           ->setTextAlign(TextAlign::Center)
                                           ->setTextAlignVertical(
                                               TextAlignVertical::Center))
                          ->setWidth(400)
                          ->setHeight(300)
                          ->setBackgroundColor(Color::fromRGB(200, 200, 200))
                          ->setBorderRadius(8);
                    })
                    ->Else([this]() {
                      return MemoryImage(imageBytes.get())
                          ->setFit(ImageFit::Cover)
                          ->setWidth(400)
                          ->setHeight(300)
                          ->setBorderRadius(8);
                    })

               })
            ->setSpacing(16)
            ->setPadding(24)
            ->setCrossAxisAlignment(CrossAxisAlignment::Center),
        nullptr, nullptr);
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("FluxUI - MemoryImage", std::make_shared<MyApp>(),
                 AppTheme::light(), false, 600, 500, false, false);
}