#include "flux/flux.hpp"
#include "flux/flux_navigator.hpp"
#include "flux/flux_secure_storage.hpp"

// ── Shared storage instance ───────────────────────────────────────────────────

static std::shared_ptr<flux::FluxSecureStorage> gStorage =
    std::make_shared<flux::FluxSecureStorage>();

// ============================================================================
// HOME PAGE
// ============================================================================

class HomePage : public Widget
{
public:
    WidgetPtr build() override
    {
        return Scaffold(
            AppBar("Home"),
            Expanded(Center(
                Column({
                           Text("Welcome back!"),
                           Button("Logout", []
                                  { gStorage->deleteKey("auth_token",
                                                        [](bool, flux::SecureStorageError)
                                                        {
                                                            Navigator::navigate("/login");
                                                        }); }),
                       })
                    ->setSpacing(12)
                    ->setCrossAxisAlignment(CrossAxisAlignment::Center))),
            nullptr, nullptr);
    }
};

// ============================================================================
// LOGIN PAGE
// ============================================================================

class LoginPage : public Widget
{
public:
    WidgetPtr build() override
    {
        return Scaffold(
            AppBar("Login"),
            Expanded(Center(
                Button("Login", []
                       { gStorage->write("auth_token", "tok_abc123",
                                         [](bool ok, flux::SecureStorageError)
                                         {
                                             if (ok)
                                                 Navigator::navigate("/home");
                                         }); }))),
            nullptr, nullptr);
    }
};

// ============================================================================
// SPLASH PAGE
// ============================================================================

class SplashPage : public Widget
{
public:
    void onMount() override
    {
        gStorage->read("auth_token",
                       [](std::optional<std::string> val, flux::SecureStorageError)
                       {
                           if (val && !val->empty())
                               Navigator::navigate("/home");
                           else
                               Navigator::navigate("/login");
                       });
    }

    WidgetPtr build() override
    {
        return Scaffold(
            AppBar("My App"),
            Expanded(Center(Text("Loading..."))),
            nullptr, nullptr);
    }
};

// ============================================================================
// ENTRY POINT
// ============================================================================

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp("My App",
                   Navigator::init({
                                       {"/", []
                                        { return std::make_shared<SplashPage>(); }},
                                       {"/login", []
                                        { return std::make_shared<LoginPage>(); }},
                                       {"/home", []
                                        { return std::make_shared<HomePage>(); }},
                                   },
                                   "/"),
                   AppTheme::light(), false, 900, 700, false, false);
}