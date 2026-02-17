#include "flux.hpp"
#include "flux_dialog.hpp"
#include <windows.h>

class DialogDemoComponent : public Component {
private:
  State<std::string> userName;
  State<std::string> userEmail;
  State<int> itemCount;
  State<bool> isActive;
  State<std::vector<std::string>> listItems;

  // ✅ Store dialog as member
  DialogWidgetPtr formDialog;

public:
  DialogDemoComponent()
      : userName("", context), userEmail("", context), itemCount(0, context),
        listItems({}, context), isActive(false, context) {

    // Initialize with many items to demonstrate scrolling
    std::vector<std::string> leftData;

    for (int i = 1; i <= 5; i++) {
      leftData.push_back("Left Item " + std::to_string(i));
    }

    listItems.set(leftData);

    // Create dialog ONCE in constructor
    formDialog =
        Dialog(
            Column(
                Text("User Information")
                    ->setFontSize(20)
                    ->setFontWeight(FontWeight::Bold),

                SizedBox(0, 20),

                Text("Name")->setFontSize(14)->setFontWeight(FontWeight::Bold),

                TextInput("Enter username...")
                    ->setInputValue(userEmail)
                    ->setWidth(300),

                SizedBox(0, 16),
                CheckBox("Enable feature")->setInputValue(isActive),
                SizedBox(0, 16),

                Text("Email")->setFontSize(14)->setFontWeight(FontWeight::Bold),

                SizedBox(0, 24),

                Row(Button("Cancel",
                           [this]() {
                             // ✅ Call close() directly on the persistent
                             // dialog
                             formDialog->close();
                             std::cout << "Closing: " << std::endl;
                           })
                        ->setWidth(120)
                        ->setHeight(40),

                    Button("Submit",
                           [this]() {
                             std::cout << "Name: " << userName.get()
                                       << std::endl;
                             std::cout << "Email: " << userEmail.get()
                                       << std::endl;

                             std::cout << "Enable: " << isActive.get()
                                       << std::endl;

                             formDialog->close();
                           })
                        ->setWidth(120)
                        ->setHeight(40))
                    ->setSpacing(12)
                    ->setMainAxisAlignment(MainAxisAlignment::End))
                ->setCrossAlignment(Alignment::Stretch))
            ->setSize(500, 380)
            ->setCloseOnClickOutside(false);
  }

  WidgetPtr build() override {
    // ================================================================
    // MAIN UI
    // ================================================================
    return Scaffold(
        AppBar("Dialog Demo"),
        Center(
            Container(
                Column(Container(
                           ListView(listItems)
                               ->itemBuilder([this](int i,
                                                    const std::string &item) {
                                 return Card(
                                            Row(Column(Text(item)
                                                           ->setFontWeight(
                                                               FontWeight::Bold)
                                                           ->setFontSize(14),
                                                       Text("Index: " +
                                                            std::to_string(i))
                                                           ->setFontSize(12)
                                                           ->setTextColor(RGB(
                                                               120, 120, 120)))
                                                    ->setFlex(1)
                                                    ->setSpacing(4),

                                                Button("×",
                                                       [this, i]() {
                                                         listItems.update(
                                                             [i](std::vector<
                                                                 std::string>
                                                                     v) {
                                                               if (i < v.size())
                                                                 v.erase(
                                                                     v.begin() +
                                                                     i);
                                                               return v;
                                                             });
                                                       })

                                                    ->setPadding(8))
                                                ->setSpacing(8)
                                                ->setCrossAlignment(
                                                    Alignment::Center))
                                     ->setPadding(12);
                               })
                               ->separator([]() { return SizedBox(0, 4); })
                               ->spacing(0))
                           ->setHeight(50)
                           ->setPadding(8),
                       SizedBox(0, 20),

                       SizedBox(0, 32))
                    ->setSpacing(0)
                    ->setCrossAlignment(Alignment::Center))
                ->setPadding(32)
                ->setMaxWidth(600)));
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Dialog Demo App", BuildComponent<DialogDemoComponent>(),
                 AppTheme::materialBlue());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  AllocConsole();
  FILE *fp;
  freopen_s(&fp, "CONOUT$", "w", stdout);

  std::cout << "=== Dialog Demo ===" << std::endl;

  GdiplusInitializer gdiplusInit;

  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - Dialog Demo", 900, 700);

  return app.run();
}