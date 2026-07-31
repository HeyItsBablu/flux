#include "flux/flux.hpp"

#include <cctype>

class MyApp : public Widget
{
    State<std::string> statusText{"Fill in the fields below — borders turn red on invalid input."};

public:
    WidgetPtr build() override
    {
        auto row = [](const std::string &label, WidgetPtr input)
        {
            return Box({
                       Text(label)->setFontSize(13)->setTextColor(Color::fromRGB(90, 90, 90)),
                       input,
                   })
                ->setDisplay(Display::Flex)
                ->setDirection(FlexDirection::Column)
                ->setGap(4);
        };

        // Plain text — no filtering, no built-in validation.
        auto textInput = TextInput("Just plain text")->setWidth(320);

        // Email — built-in regex validation, flags after first blur/edit.
        auto emailInput = EmailInput("you@example.com")->setWidth(320);
        emailInput->setOnValidationChanged([this](bool ok)
                                            { statusText.set(ok ? "Email looks valid" : "Email looks invalid"); });

        // Password — masked with bullets, no built-in validation.
        auto passwordInput = PasswordInput("Password")->setWidth(320);

        // Number — keystrokes filtered to digits/one '.'/leading '-'.
        auto numberInput = NumberTextInput("Age")->setWidth(320);

        // Tel — no built-in check, so a custom validator is supplied here
        // (require at least 7 digits) to show setValidator() in action.
        auto telInput = TextInput("Phone")->setType(InputType::Tel)->setWidth(320);
        telInput->setValidator([](const std::string &s)
                               {
            if (s.empty()) return true;
            int digits = 0;
            for (char c : s)
                if (std::isdigit((unsigned char)c)) digits++;
            return digits >= 7; });

        // Url — built-in scheme:// check.
        auto urlInput = TextInput("https://example.com")->setType(InputType::Url)->setWidth(320);

        // Search — no built-in validation, just the DOM search keyboard hint.
        auto searchInput = TextInput("Search...")->setType(InputType::Search)->setWidth(320);

        return Box({
                       Text("Input Types Demo")
                           ->setFontSize(22)
                           ->setFontWeight(FontWeight::Bold),

                       row("Text", textInput),
                       row("Email (built-in validation)", emailInput),
                       row("Password (masked)", passwordInput),
                       row("Number (filtered keystrokes)", numberInput),
                       row("Tel (custom validator: 7+ digits)", telInput),
                       row("Url (built-in validation)", urlInput),
                       row("Search", searchInput),

                       Text(statusText)
                           ->setFontSize(13)
                           ->setTextColor(Color::fromRGB(33, 150, 243)),
                   })
            ->setDisplay(Display::Flex)
            ->setDirection(FlexDirection::Column)
            ->setGap(16)
            ->setPadding(24)
            ->setWidthMode(SizeMode::Full)
            ->setHeightMode(SizeMode::Full)
            ->setBackgroundColor(Color::fromRGB(250, 250, 252));
    }
};

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp()
        .setTheme(AppTheme::light())
        .build(std::make_shared<MyApp>());
}