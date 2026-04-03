#include "flux/flux.hpp"
#include <string>
#include <vector>

// ============================================================================
// DATA TYPES
// ============================================================================

struct Contact {
    int         id;
    std::string name;
    std::string email;
    std::string role;
};

struct Product {
    int         id;
    std::string name;
    std::string category;
    std::string price;
    std::string badge;  // e.g. "NEW", "SALE", ""
};

// ============================================================================
// APP
// ============================================================================

class MyApp : public Component {

    // ── ListViewBuilder state ─────────────────────────────────────────────
    State<std::vector<Contact>> contacts;

    // ── GridViewBuilder state ─────────────────────────────────────────────
    State<std::vector<Product>> products;

public:
    MyApp()
        : contacts(std::vector<Contact>{
              {1,  "Alice Johnson",   "alice@example.com",   "Engineer"},
              {2,  "Bob Smith",       "bob@example.com",     "Designer"},
              {3,  "Carol White",     "carol@example.com",   "Manager"},
              {4,  "David Brown",     "david@example.com",   "Engineer"},
              {5,  "Eva Martinez",    "eva@example.com",     "Designer"},
              {6,  "Frank Lee",       "frank@example.com",   "Engineer"},
              {7,  "Grace Kim",       "grace@example.com",   "Manager"},
              {8,  "Henry Davis",     "henry@example.com",   "Engineer"},
              {9,  "Iris Chen",       "iris@example.com",    "Designer"},
              {10, "Jack Wilson",     "jack@example.com",    "Manager"},
              {11, "Karen Taylor",   "karen@example.com",   "Engineer"},
              {12, "Liam Anderson",  "liam@example.com",    "Designer"},
          }, context),
          products(std::vector<Product>{
              {1,  "Wireless Headphones", "Audio",       "$89.99",  "NEW"},
              {2,  "Mechanical Keyboard", "Peripherals", "$129.99", ""},
              {3,  "USB-C Hub",           "Accessories", "$49.99",  "SALE"},
              {4,  "4K Webcam",           "Video",       "$199.99", "NEW"},
              {5,  "Desk Lamp",           "Lighting",    "$39.99",  ""},
              {6,  "Mouse Pad XL",        "Accessories", "$24.99",  "SALE"},
              {7,  "Standing Desk Mat",   "Ergonomics",  "$59.99",  ""},
              {8,  "Monitor Arm",         "Ergonomics",  "$79.99",  "NEW"},
              {9,  "Cable Organizer",     "Accessories", "$19.99",  ""},
              {10, "Laptop Stand",        "Ergonomics",  "$44.99",  "SALE"},
              {11, "Smart Plug",          "Smart Home",  "$29.99",  ""},
              {12, "LED Strip",           "Lighting",    "$34.99",  "NEW"},
              {13, "Noise Machine",       "Audio",       "$54.99",  ""},
              {14, "Phone Stand",         "Accessories", "$14.99",  "SALE"},
              {15, "Wireless Charger",    "Charging",    "$35.99",  "NEW"},
              {16, "Screen Cleaner Kit",  "Accessories", "$12.99",  ""},
          }, context)
    {}

    WidgetPtr build() override {

        // ── ListViewBuilder ───────────────────────────────────────────────
        //
        // Key by Contact::id so add/remove only rebuilds affected rows.
        // Each row: avatar circle | name + email | role badge

        auto contactList = ListView(contacts)
            ->setKeyFn([](const Contact &c) -> uintptr_t {
                return static_cast<uintptr_t>(c.id);
            })
            ->itemBuilder([this](int /*i*/, const Contact &c) -> WidgetPtr {

                // Role badge color
                Color badgeColor = (c.role == "Engineer")
                    ? Color::fromRGB(59, 130, 246)   // blue
                    : (c.role == "Designer")
                        ? Color::fromRGB(168, 85, 247) // purple
                        : Color::fromRGB(34, 197, 94); // green (Manager)

                // Avatar circle initial
                auto avatar = Text(c.name.substr(0, 1))
                    ->setFontSize(16)
                    ->setFontWeight(FontWeight::Bold)
                    ->setTextColor(Color::fromRGB(255, 255, 255))
                    ->setPadding(10)
                    ->setBackgroundColor(badgeColor)
                    ->setBorderRadius(24)
                    ->setWidth(44)
                    ->setHeight(44);

                // Name + email stacked
                auto nameText = Text(c.name)
                    ->setFontSize(14)
                    ->setFontWeight(FontWeight::Bold)
                    ->setTextColor(Color::fromRGB(30, 30, 30));

                auto emailText = Text(c.email)
                    ->setFontSize(12)
                    ->setTextColor(Color::fromRGB(120, 120, 120));

                auto nameCol = Column({nameText, emailText})
                    ->setSpacing(2)
                    ->setFlex(1);

                // Role pill
                auto roleBadge = Text(c.role)
                    ->setFontSize(11)
                    ->setFontWeight(FontWeight::Bold)
                    ->setTextColor(Color::fromRGB(255, 255, 255))
                    ->setBackgroundColor(badgeColor)
                    ->setBorderRadius(12);

                // Full row
                return Row({avatar, nameCol, roleBadge})
                    ->setSpacing(12)
                    ->setPadding(12)
                    ->setCrossAxisAlignment(CrossAxisAlignment::Center)
                    ->setBackgroundColor(Color::fromRGB(255, 255, 255));
            })
            ->setSpacing(0);

        // ── Add / remove buttons to demo reactivity ───────────────────────

        auto addBtn = Button("+ Add Contact", [this]() {
            auto list = contacts.get();
            int nextId = list.empty() ? 1 : list.back().id + 1;
            list.push_back({
                nextId,
                "New Person " + std::to_string(nextId),
                "new" + std::to_string(nextId) + "@example.com",
                "Engineer"
            });
            contacts.set(list);
        });

        auto removeBtn = Button("- Remove Last", [this]() {
            auto list = contacts.get();
            if (!list.empty()) {
                list.pop_back();
                contacts.set(list);
            }
        });

        auto listControls = Row({addBtn, removeBtn})
            ->setSpacing(8)
            ->setPadding(8);

        auto listPanel = Column({
            Text("Contacts")
                ->setFontSize(16)
                ->setFontWeight(FontWeight::Bold)
                ->setTextColor(Color::fromRGB(30, 30, 30))
                ->setPadding(12),
            listControls,
            contactList
        })
        ->setFlex(1)
        ->setBackgroundColor(Color::fromRGB(250, 250, 250))
        ->setBorderRadius(8);

        // ── GridViewBuilder ───────────────────────────────────────────────
        //
        // 3 columns, fixed spacing.
        // Each cell: product card with badge, name, category, price.

        auto productGrid = GridView(products)
            ->setKeyFn([](const Product &p) -> uintptr_t {
                return static_cast<uintptr_t>(p.id);
            })
            ->itemBuilder([](int /*i*/, const Product &p) -> WidgetPtr {

                // Optional badge overlay
                WidgetPtr badgeWidget = p.badge.empty()
                    ? SizedBox(0, 0)
                    : Text(p.badge)
                        ->setFontSize(10)
                        ->setFontWeight(FontWeight::Bold)
                        ->setTextColor(Color::fromRGB(255, 255, 255))
                        ->setBackgroundColor(
                            p.badge == "NEW"
                                ? Color::fromRGB(34, 197, 94)
                                : Color::fromRGB(239, 68, 68))
                        ->setBorderRadius(6);

                auto topRow = Row({
                    Text(p.category)
                        ->setFontSize(11)
                        ->setTextColor(Color::fromRGB(140, 140, 140)),
                    badgeWidget
                })
                ->setCrossAxisAlignment(CrossAxisAlignment::Center);

                auto nameText = Text(p.name)
                    ->setFontSize(13)
                    ->setFontWeight(FontWeight::Bold)
                    ->setTextColor(Color::fromRGB(30, 30, 30));

                auto priceText = Text(p.price)
                    ->setFontSize(15)
                    ->setFontWeight(FontWeight::Bold)
                    ->setTextColor(Color::fromRGB(59, 130, 246));

                auto addToCartBtn = Button("Add to Cart", [id = p.id]() {
                    std::cout << "Added product " << id << " to cart\n";
                });

                return Column({topRow, nameText, priceText, addToCartBtn})
                    ->setSpacing(6)
                    ->setPadding(12)
                    ->setBackgroundColor(Color::fromRGB(255, 255, 255))
                    ->setBorderRadius(10);
            })
            ->columns(3)
            ->setSpacing(10);

        // ── Add / remove to demo grid reactivity ──────────────────────────

        auto gridAddBtn = Button("+ Add Product", [this]() {
            auto list = products.get();
            int nextId = list.empty() ? 1 : list.back().id + 1;
            list.push_back({
                nextId,
                "Product " + std::to_string(nextId),
                "New",
                "$9.99",
                "NEW"
            });
            products.set(list);
        });

        auto gridRemoveBtn = Button("- Remove Last", [this]() {
            auto list = products.get();
            if (!list.empty()) {
                list.pop_back();
                products.set(list);
            }
        });

        auto gridControls = Row({gridAddBtn, gridRemoveBtn})
            ->setSpacing(8)
            ->setPadding(8);

        auto gridPanel = Column({
            Text("Products")
                ->setFontSize(16)
                ->setFontWeight(FontWeight::Bold)
                ->setTextColor(Color::fromRGB(30, 30, 30))
                ->setPadding(12),
            gridControls,
            productGrid
        })
        ->setFlex(1)
        ->setBackgroundColor(Color::fromRGB(250, 250, 250))
        ->setBorderRadius(8);

        // ── Root layout: two panels side by side ──────────────────────────

        return Scaffold(
            AppBar("ListView & GridView Demo"),
            Row({ gridPanel})
                ->setSpacing(16)
                ->setPadding(16)
                ->setCrossAxisAlignment(CrossAxisAlignment::Stretch)
        );
    }
};

WidgetPtr createApp(FluxUI *app) {
    return FluxApp(
        "FluxUI - Collections Demo",
        BuildComponent<MyApp>(),
        AppTheme::light(),
        false,  // debugShowWidgetBounds
        1100,   // width — wide enough for two panels
        700,    // height
        false,  // maximize
        false   // fullscreen
    );
}