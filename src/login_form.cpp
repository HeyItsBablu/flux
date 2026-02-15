#include "flux.hpp"

#include <windows.h>

class LoginFormApp : public StatefulComponent
{
private:
    State<std::string> email;
    State<std::string> password;
    State<bool> rememberMe;
    State<std::string> statusMessage;
    State<bool> isLoading;

    // Refs to text input widgets
    std::shared_ptr<TextInputWidget> emailInput;
    std::shared_ptr<TextInputWidget> passwordInput;

public:
    LoginFormApp()
        : email("", context),
          password("", context),
          rememberMe(true, context),
          statusMessage("", context),
          isLoading(false, context)
    {
    }

    void handleLogin()
    {
        std::cout << "Login Pressed" << rememberMe.get() << std::endl;
        std::string emailVal = email.get();
        std::string passwordVal = password.get();

        // Basic validation
        if (emailVal.empty())
        {
            statusMessage.set("⚠ Email is required");
            return;
        }

        if (passwordVal.empty())
        {
            statusMessage.set("⚠ Password is required");
            return;
        }

        if (emailVal.find('@') == std::string::npos)
        {
            statusMessage.set("⚠ Invalid email format");
            return;
        }

        if (passwordVal.length() < 6)
        {
            statusMessage.set("⚠ Password must be at least 6 characters");
            return;
        }

        // Simulate loading
        isLoading.set(true);
        statusMessage.set("🔄 Logging in...");

        // In a real app, you would make an API call here
        // For demo, we'll just simulate success after validation
        std::cout << "Login attempted:" << std::endl;
        std::cout << "  Email: " << emailVal << std::endl;
        std::cout << "  Password: " << std::string(passwordVal.length(), '*') << std::endl;
        std::cout << "  Remember Me: " << (rememberMe.get() ? "Yes" : "No") << std::endl;

        // Simulate async operation
        statusMessage.set("✓ Login successful!");
        isLoading.set(false);

        // Show success message
        MessageBox(NULL,
                   ("Welcome back!\n\nEmail: " + emailVal).c_str(),
                   "Login Successful",
                   MB_OK | MB_ICONINFORMATION);
    }

    void handleForgotPassword()
    {
        std::string emailVal = email.get();

        if (emailVal.empty())
        {
            statusMessage.set("⚠ Enter your email first");
            return;
        }

        statusMessage.set("📧 Password reset link sent to " + emailVal);

        MessageBox(NULL,
                   ("A password reset link has been sent to:\n" + emailVal).c_str(),
                   "Password Reset",
                   MB_OK | MB_ICONINFORMATION);
    }

    WidgetPtr build() override
    {
        // Create text inputs with state binding
        emailInput = std::make_shared<TextInputWidget>();
        emailInput->setPlaceholder("Enter your email")
            ->setValue(email.get())
            ->setOnChanged([this](const std::string &text)
                           {
                               email.set(text);
                               statusMessage.set(""); // Clear status on input change
                           })
            ->setWidth(350);

        passwordInput = std::make_shared<TextInputWidget>();
        passwordInput->setPlaceholder("Enter your password")
            ->setObscureText(true)
            ->setValue(password.get())
            ->setOnChanged([this](const std::string &text)
                           {
                               password.set(text);
                               statusMessage.set(""); // Clear status on input change
                           })
            ->setOnSubmitted([this](const std::string &text)
                             {
                                 handleLogin(); // Submit on Enter key
                             })
            ->setWidth(350);

        // Status message widget
        WidgetPtr statusWidget = nullptr;
        if (!statusMessage.get().empty())
        {
            COLORREF statusColor = RGB(100, 100, 100);
            if (statusMessage.get().find("⚠") != std::string::npos)
                statusColor = RGB(244, 67, 54); // Red for errors
            else if (statusMessage.get().find("✓") != std::string::npos)
                statusColor = RGB(76, 175, 80); // Green for success
            else if (statusMessage.get().find("🔄") != std::string::npos)
                statusColor = RGB(33, 150, 243); // Blue for loading

            statusWidget = Container(
                               Text(statusMessage.get())
                                   ->setTextColor(statusColor)
                                   ->setFontSize(13))
                               ->setPadding(12)
                               ->setBackgroundColor(RGB(250, 250, 250))
                               ->setBorderRadius(6);
        }

        return Scaffold(
            AppBar("Login From"),

            // Center the login form
            Center(
                Container(
                    Column(

                        // Logo/Title section
                        Container(
                            Column(
                                Text("🔐")
                                    ->setFontSize(48),
                                SizedBox(0, 8),
                                Text("Welcome Back")
                                    ->setFontSize(28)
                                    ->setFontWeight(FontWeight::Bold)
                                    ->setTextColor(RGB(63, 81, 181)),
                                SizedBox(0, 4),
                                Text("Sign in to continue")
                                    ->setFontSize(14)
                                    ->setTextColor(RGB(120, 120, 120)))
                                ->setSpacing(0)
                                ->setCrossAlignment(Alignment::Center))
                            ->setPadding(20),

                        SizedBox(0, 20),

                        // Email input
                        Container(
                            Column(
                                Text("Email")
                                    ->setFontSize(13)
                                    ->setFontWeight(FontWeight::Bold)
                                    ->setTextColor(RGB(80, 80, 80)),
                                SizedBox(0, 6),
                                emailInput)
                                ->setSpacing(0)
                                ->setCrossAlignment(Alignment::Start))
                            ->setPaddingAll(20, 0, 20, 0),

                        SizedBox(0, 16),

                        // Password input
                        Container(
                            Column(
                                Text("Password")
                                    ->setFontSize(13)
                                    ->setFontWeight(FontWeight::Bold)
                                    ->setTextColor(RGB(80, 80, 80)),
                                SizedBox(0, 6),
                                passwordInput)
                                ->setSpacing(0)
                                ->setCrossAlignment(Alignment::Start))
                            ->setPaddingAll(20, 0, 20, 0),

                        SizedBox(0, 12),

                        // Remember me and Forgot password row
                        Container(
                            Row(
                                // Remember me checkbox (simulated with button)
                                Button(
                                    rememberMe.get() ? "Remember me" : "Remembered",
                                    [this]()
                                    {
                                        rememberMe.set(!rememberMe.get());
                                    })
                                    ->setBackgroundColor(RGB(255, 255, 255))
                                    ->setTextColor(RGB(80, 80, 80))
                                    ->setBorderColor(RGB(200, 200, 200))
                                    ->setBorderWidth(1)
                                    ->setPadding(8)
                                    ->setFontSize(12)
                                    ->setFlex(1),

                                Button("Forgot Password?", [this]()
                                       { handleForgotPassword(); })
                                    ->setBackgroundColor(RGB(255, 255, 255))
                                    ->setTextColor(RGB(33, 150, 243))
                                    ->setHoverTextColor(RGB(25, 118, 210))
                                    ->setBorderWidth(0)
                                    ->setPadding(8)
                                    ->setFontSize(12))
                                ->setSpacing(8))
                            ->setPaddingAll(20, 0, 20, 0),

                        SizedBox(0, 20),

                        // Status message
                        statusWidget ? Container(statusWidget)->setPaddingAll(20, 0, 20, 0) : SizedBox(0, 0),
                        statusWidget ? SizedBox(0, 12) : SizedBox(0, 0),

                        // Login button
                        Container(
                            Button(
                                isLoading.get() ? "Logging in..." : "Login",
                                [this]()
                                {
                                    if (!isLoading.get())
                                    {
                                        handleLogin();
                                    }
                                })
                                ->setBackgroundColor(RGB(63, 81, 181))
                                ->setHoverBackgroundColor(RGB(48, 63, 159))
                                ->setWidth(350)
                                ->setPaddingAll(16, 14, 16, 14)
                                ->setFontSize(15)
                                ->setFontWeight(FontWeight::Bold)
                                ->setBorderRadius(6))
                            ->setPaddingAll(20, 0, 20, 0),

                        SizedBox(0, 20))
                        ->setSpacing(0))
                    ->setBackgroundColor(RGB(255, 255, 255))
                    ->setBorderRadius(12)
                    ->setMinWidth(450)
                    ->setMaxWidth(450)
                    ->setPadding(0)
                    ->setBorderColor(RGB(230, 230, 230))
                    ->setBorderWidth(1))
                ->setBackgroundColor(RGB(245, 247, 250)));
    }
};

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp(
        "Login Form",
        BuildComponent<LoginFormApp>(),
        AppTheme::materialGreen());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    AllocConsole();
    FILE *fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);

    std::cout << "=== Login Form Demo ===" << std::endl;
    std::cout << "• Enter email and password" << std::endl;
    std::cout << "• Press Enter in password field to submit" << std::endl;
    std::cout << "• Test validation by entering invalid data" << std::endl;
    std::cout << "• Email: test@example.com" << std::endl;
    std::cout << "• Password: (min 6 characters)" << std::endl;

    FluxUI app(hInstance);
    app.build([&]()
              { return createApp(&app); });
    app.createWindow("FluxUI - Login Form", 800, 700);

    return app.run();
}