#include "flux/flux.hpp"
#include "flux/flux_navigator.hpp"
#include "flux/flux_secure_storage.hpp"

// ── Shared storage instance ───────────────────────────────────────────────────

static std::shared_ptr<flux::FluxSecureStorage> gStorage =
    std::make_shared<flux::FluxSecureStorage>();

// ── Forward declarations ──────────────────────────────────────────────────────

class HomePage;
class LoginPage;
class SplashPage;

// ============================================================================
// HOME PAGE
// ============================================================================

class HomePage : public Widget {
public:
  WidgetPtr build() override {
    return Scaffold(
        AppBar("Home"),
        Expanded(Center(
            Column({
                Text("Welcome back!"),
                Button("Logout", [] {
                    gStorage->deleteKey("auth_token",
                        [](bool, flux::SecureStorageError) {
                            Navigator::pushAndRemoveAll(std::static_pointer_cast<Widget>(std::make_shared<LoginPage>()));
                        });
                }),
            })
            ->setSpacing(12)
            ->setCrossAxisAlignment(CrossAxisAlignment::Center)
        )),
        nullptr, nullptr);
  }
};

// ============================================================================
// LOGIN PAGE
// ============================================================================

class LoginPage : public Widget {
public:
  WidgetPtr build() override {
    return Scaffold(
        AppBar("Login"),
        Expanded(Center(
            Button("Login", [] {
                gStorage->write("auth_token", "tok_abc123",
                    [](bool ok, flux::SecureStorageError) {
                        if (ok)
                            Navigator::pushAndRemoveAll(std::make_shared<HomePage>());
                    });
            })
        )),
        nullptr, nullptr);
  }
};

// ============================================================================
// SPLASH PAGE
// ============================================================================

class SplashPage : public Widget {
public:
  void onMount() override {
    gStorage->read("auth_token",
        [](std::optional<std::string> val, flux::SecureStorageError) {
            if (val && !val->empty())
                Navigator::pushAndRemoveAll(std::make_shared<HomePage>());
            else
                Navigator::pushAndRemoveAll(std::static_pointer_cast<Widget>(std::make_shared<LoginPage>()));
        });
  }

  WidgetPtr build() override {
    return Scaffold(
        AppBar("My App"),
        Expanded(Center(Text("Loading..."))),
        nullptr, nullptr);
  }
};

// ============================================================================
// ENTRY POINT
// ============================================================================

WidgetPtr createApp(FluxUI* app) {
  return FluxApp("My App",
      Navigator::init(std::make_shared<SplashPage>()),
      AppTheme::light(), false, 900, 700, false, false);
}