// lib/main.cpp
#include "flux/flux.hpp"
#include "flux/flux_navigator.hpp"

// ============================================================
//  Home page — plain static route, no params
// ============================================================
class HomePage : public Widget
{
public:
    WidgetPtr build() override
    {
        return Flex({
            Text("Home")->setFontWeight(FontWeight::Bold)->setFontSize(24),
            Text("Pick a product:"),
            Button("Product 1", []() { Navigator::navigate("/products/1"); }),
            Button("Product 2", []() { Navigator::navigate("/products/2"); }),
            Button("Go to Settings", []() { Navigator::navigate("/settings"); }),
        })
        ->setDirection(FlexDirection::Column)
        ->setGap(12)
        ->setPadding(24);
    }
};

// ============================================================
//  Product page — parameterized route: /products/:id
//  This is the actual thing Phase 2 adds. Reading the id in the
//  CONSTRUCTOR proves params survive navigate(), browser back/
//  forward, AND a hard page refresh on the deep link.
// ============================================================
class ProductPage : public Widget
{
    std::string productId;
public:
    ProductPage()
    {
        // arguments<T>() must be called in the constructor per the
        // header's own documented contract.
        RouteParams params = Navigator::arguments<RouteParams>();
        auto it = params.find("id");
        productId = (it != params.end()) ? it->second : "(missing)";
    }

    WidgetPtr build() override
    {
        return Flex({
            Text("Product Page")->setFontWeight(FontWeight::Bold)->setFontSize(24),
            Text("id = " + productId),
            Button("Back", []() { Navigator::pop(); }),
            Button("Go Home", []() { Navigator::pushAndRemoveAllNamed("/"); }),
        })
        ->setDirection(FlexDirection::Column)
        ->setGap(12)
        ->setPadding(24);
    }
};

// ============================================================
//  Settings page — second plain static route, to test that pop()
//  and popUntil() still behave with a params-based stack mixed in.
// ============================================================
class SettingsPage : public Widget
{
public:
    WidgetPtr build() override
    {
        return Flex({
            Text("Settings")->setFontWeight(FontWeight::Bold)->setFontSize(24),
            Button("Back", []() { Navigator::maybePop(); }),
        })
        ->setDirection(FlexDirection::Column)
        ->setGap(12)
        ->setPadding(24);
    }
};

// ============================================================
//  Entry point
// ============================================================
WidgetPtr createApp(FluxUI *app)
{
    return FluxApp()
        .setTheme(AppTheme::light())
        .build(Navigator::init({
            {"/",              [] { return std::make_shared<HomePage>();     }},
            {"/products/:id",  [] { return std::make_shared<ProductPage>();  }},
            {"/settings",      [] { return std::make_shared<SettingsPage>(); }},
        }, "/"));
}