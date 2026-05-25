#include "flux/flux.hpp"


class MyApp : public Widget {
  State<std::string> selectedPath{"Nothing selected"};

public:


  WidgetPtr build() override {

    // ── Build the tree ────────────────────────────────────────────────────
    TreeNode root("MyProject");
    root.expanded = true;

    auto &src = root.addChild(TreeNode("src"));
    src.expanded = true;
    auto &components = src.addChild(TreeNode("components"));
    components.expanded = true;
    components.addChild(TreeNode("flux_widget.hpp"));
    components.addChild(TreeNode("flux_layout.hpp"));
    components.addChild(TreeNode("flux_state.hpp"));
    auto &overlays = src.addChild(TreeNode("overlays"));
    overlays.addChild(TreeNode("flux_overlays.hpp"));
    overlays.addChild(TreeNode("flux_menu_bar.hpp"));
    src.addChild(TreeNode("main.cpp"));
    src.addChild(TreeNode("CMakeLists.txt"));

    auto &assets = root.addChild(TreeNode("assets"));
    assets.addChild(TreeNode("icon.png"));
    assets.addChild(TreeNode("splash.png"));

    auto &build = root.addChild(TreeNode("build"));
    build.addChild(TreeNode("Debug"));
    build.addChild(TreeNode("Release"));

    root.addChild(TreeNode("README.md"));

    // ── Layout ────────────────────────────────────────────────────────────
    return Scaffold(
        AppBar("TreeView Demo"),
        Row({

            // ── Tree panel ────────────────────────────────────────────────
            Container(
                TreeView(root)
                    ->setOnSelectionChanged([this](const TreeNode *n) {
                        selectedPath.set("Selected: " + n->label);
                    })
                    ->setOnNodeDoubleClicked([this](const TreeNode *n) {
                        selectedPath.set("Opened: " + n->label);
                    })
                    ->setShowGuideLines(true)
                    ->setFlex(1)
            )
            ->setWidth(260)
            ->setBorderColor(Color::fromRGB(210, 210, 210))
            ->setBorderWidth(1),

            // ── Detail panel ──────────────────────────────────────────────
            Expanded(
                Center(
                    Column({
                        Text("File Explorer")
                            ->setFontSize(18)
                            ->setFontWeight(FontWeight::Bold)
                            ->setTextColor(Color::fromRGB(80, 80, 80)),
                        SizedBox(0, 16),
                        Text(selectedPath)
                            ->setFontSize(13)
                            ->setTextColor(Color::fromRGB(33, 150, 243)),
                        SizedBox(0, 24),
                        Text("Click a node to select")
                            ->setFontSize(12)
                            ->setTextColor(Color::fromRGB(160, 160, 160)),
                        Text("Double-click a folder to open")
                            ->setFontSize(12)
                            ->setTextColor(Color::fromRGB(160, 160, 160)),
                        Text("Arrow keys to navigate")
                            ->setFontSize(12)
                            ->setTextColor(Color::fromRGB(160, 160, 160)),
                    })
                    ->setSpacing(0)
                )
            )

        })
        ->setSpacing(0),nullptr,nullptr
    );
  }
};

WidgetPtr createApp(FluxUI* app) {
    return FluxApp(
        "FluxUI - Paint",
        std::make_shared<MyApp>(),
        AppTheme::light(),
        false,   // debugShowWidgetBounds
        900,     // width
        700,     // height
        false,   // maximize
        true     // fullscreen
    );
}