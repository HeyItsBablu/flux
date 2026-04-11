#include "flux/flux.hpp"

class MyApp : public Widget {
  State<std::string>              singlePath{""};
  State<std::vector<std::string>> multiPaths{{}};

public:
  WidgetPtr build() override {

    // Single file name (just the filename, not full path)
    std::string singleName = "";
    if (!singlePath.get().empty()) {
      auto p = singlePath.get();
      auto pos = p.find_last_of("\\/");
      singleName = (pos != std::string::npos) ? p.substr(pos + 1) : p;
    }

    // Multi file names joined
    std::string multiNames = "";
    for (auto& p : multiPaths.get()) {
      auto pos = p.find_last_of("\\/");
      if (!multiNames.empty()) multiNames += ", ";
      multiNames += (pos != std::string::npos) ? p.substr(pos + 1) : p;
    }

    return Scaffold(
      AppBar("File Picker Demo"),
      Column({

        // ── Single file ───────────────────────────────────────────────
        Text("Single File"),
        FilePicker()
          ->setMode(FilePickerMode::Open)
          ->addFilter("Images", {"*.png", "*.jpg", "*.jpeg"})
          ->addFilter("All Files", {"*.*"})
          ->bindPath(singlePath),

        

        Divider(),

        // ── Multiple files ────────────────────────────────────────────
        Text("Multiple Files"),
        FilePicker()
          ->setMode(FilePickerMode::OpenMultiple)
          ->addFilter("Images", {"*.png", "*.jpg", "*.jpeg"})
          ->addFilter("All Files", {"*.*"})
          ->bindPaths(multiPaths),



      })
    );
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("FluxUI - App", std::make_shared<MyApp>(), AppTheme::light(),
                 false, 900, 700, false, false);
}