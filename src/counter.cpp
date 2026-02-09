#include "flux.hpp"

class UserProfile
{
private:
    FluxUI *ui;
    State<std::string> username;
    State<std::string> status;
    State<int> messageCount;

public:
    UserProfile(FluxUI *app)
        : ui(app),
          username("Guest", app),
          status("Offline", app),
          messageCount(0, app) {}

    void updateUsername(const std::string &name)
    {
        username.set(name);
    }

    void toggleStatus()
    {
        if (status.get() == "Offline")
        {
            status.set("Online");
        }
        else
        {
            status.set("Offline");
        }
    }

    void sendMessage()
    {
        messageCount++; // Uses operator++
    }

    void clearMessages()
    {
        messageCount = 0; // Uses operator=
    }

    void decreaseMessage()
    {
        messageCount--;
        // message = "Counter decremented to " + std::to_string(counter.get());
    }

    WidgetPtr build()
    {
        return Center(
            Card(
                Column(
                    // Title
                    Text("User Profile")
                        ->setFontSize(28)
                        ->setFontWeight(FontWeight::Bold)
                        ->setTextColor(RGB(33, 150, 243))
                        ->setMargin(10),

                    Divider(),

                    // Username display (State<string>)
                    Row(
                        Text("Username: ")
                            ->setFontSize(16)
                            ->setMargin(10),

                        Text(username) // ← State<string> binding!
                            ->setFontSize(16)
                            ->setFontWeight(FontWeight::Bold)
                            ->setTextColor(RGB(76, 175, 80)))
                        ->setSpacing(5),

                    // Status display (State<string>)
                    Row(
                        Text("Status: ")
                            ->setFontSize(16)
                            ->setMargin(10),

                        Text(status) // ← State<string> binding!
                            ->setFontSize(16)
                            ->setFontWeight(FontWeight::Bold)
                            ->setTextColor(RGB(255, 152, 0)))
                        ->setSpacing(5),

                    // Message count (State<int>)
                    Row(
                        Text("Messages: ")
                            ->setFontSize(16)
                            ->setMargin(10),

                        Text(messageCount) // ← State<int> binding!
                            ->setFontSize(16)
                            ->setFontWeight(FontWeight::Bold)
                            ->setTextColor(RGB(156, 39, 176)))
                        ->setSpacing(5),

                    Divider(),

                    // Username buttons
                    Text("Change Username:")
                        ->setFontSize(14)
                        ->setMargin(10),

                    Row(
                        Button("Alice", [this]()
                               { updateUsername("Alice"); })
                            ->setBackgroundColor(RGB(33, 150, 243))
                            ->setMargin(5),

                        Button("Bob", [this]()
                               { updateUsername("Bob"); })
                            ->setBackgroundColor(RGB(33, 150, 243))
                            ->setMargin(5),

                        Button("Charlie", [this]()
                               { updateUsername("Charlie"); })
                            ->setBackgroundColor(RGB(33, 150, 243))
                            ->setMargin(5))
                        ->setMainAxisAlignment(MainAxisAlignment::Center)
                        ->setSpacing(10),

                    // Status toggle
                    Button("Toggle Status", [this]()
                           { toggleStatus(); })
                        ->setBackgroundColor(RGB(255, 152, 0))
                        ->setMargin(10),

                    Divider(),

                    // Message controls
                    Row(
                        Button("Send Message", [this]()
                               { sendMessage(); })
                            ->setBackgroundColor(RGB(76, 175, 80))
                            ->setMargin(5),
                        Button("Read Message", [this]()
                               { decreaseMessage(); })
                            ->setBackgroundColor(RGB(76, 175, 80))
                            ->setMargin(5),
                        Button("Clear Messages", [this]()
                               { clearMessages(); })
                            ->setBackgroundColor(RGB(244, 67, 54))
                            ->setMargin(5))
                        ->setMainAxisAlignment(MainAxisAlignment::Center)
                        ->setSpacing(10))
                    ->setSpacing(5))
                ->setWidth(400));
    }
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    FluxUI app(hInstance);
    UserProfile profile(&app);

    app.build([&profile]()
              { return profile.build(); });

    app.createWindow("String State Example - User Profile", 500, 500);

    return app.run();
}