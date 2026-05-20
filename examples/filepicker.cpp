#include "flux/flux.hpp"

class MyApp : public Widget {
    State<std::string>              singlePath{""};
    State<std::vector<std::string>> multiPaths{{}};

public:
    WidgetPtr build() override {
        return Scaffold(
            AppBar("File Picker Demo"),
            Column({
                // ── Single file ───────────────────────────────────────
                Text("Single File"),
                FilePicker()
                    ->setMode(FilePickerMode::Open)
                    ->addFilter("Images", {"*.png", "*.jpg", "*.jpeg"})
                    ->addFilter("All Files", {"*.*"})
                    ->setShowPath(false)
                    ->bindPath(singlePath),
                Text(singlePath, [](const std::string& p) -> std::string {
                  
                    if (p.empty()) return "No file selected";
                    auto pos = p.find_last_of("\\/");
                    return (pos != std::string::npos) ? p.substr(pos + 1) : p;
                }),

                Divider(),

                // ── Multiple files ────────────────────────────────────
                Text("Multiple Files"),
                FilePicker()
                    ->setMode(FilePickerMode::OpenMultiple)
                    ->addFilter("Images", {"*.png", "*.jpg", "*.jpeg"})
                    ->addFilter("All Files", {"*.*"})
                    ->setShowPath(false)
                    ->bindPaths(multiPaths),
                Text(multiPaths, [](const std::vector<std::string>& ps) -> std::string {
                    std::cout <<"The file path is "<< ps.empty() <<"the name "<< std::endl;
                    if (ps.empty()) return "No files selected";
                    std::string out;
                    for (const auto& p : ps) {
                        
                        auto pos = p.find_last_of("\\/");
                        if (!out.empty()) out += ", ";
                        out += (pos != std::string::npos) ? p.substr(pos + 1) : p;
                    }
                    return out;
                }),
            }),nullptr,nullptr
        );
    }
};

WidgetPtr createApp(FluxUI* app) {
    return FluxApp("FluxUI - App", std::make_shared<MyApp>(), AppTheme::light(),
                   false, 900, 700, false, false);
}