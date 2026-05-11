#include "flux/flux.hpp"

// ============================================================================
// Data struct for TypedJsonBuilder
// ============================================================================

struct Post {
  int id;
  int userId;
  std::string title;
  std::string body;
};

Post postFromJson(const JsonValue &j) {
  return Post{j["id"].getInt(), j["userId"].getInt(), j["title"].getString(),
              j["body"].getString()};
}

// ============================================================================
// Reusable card container
// ============================================================================

static WidgetPtr SectionLabel(const std::string &text) {
  return Text(text)
      ->setFontWeight(FontWeight::Bold)
      ->setFontSize(11)
      ->setTextColor(Color::fromRGB(120, 120, 120));
}

// ============================================================================
// Shared loading / error helpers
// ============================================================================

static WidgetPtr LoadingWidget(const std::string &label) {
  return Row({
      Text(label)->setTextColor(Color::fromRGB(140, 140, 140))->setFontSize(13),
  });
}

static WidgetPtr ErrorWidget(const std::string &err) {
  return Container(
             Text("Error: " + err)->setTextColor(Color::fromRGB(220, 53, 69)))
      ->setPadding(8)
      ->setBackgroundColor(Color::fromRGB(255, 235, 238))
      ->setBorderRadius(6);
}

// ============================================================================
// App component
// ============================================================================

class MyApp : public Widget {
public:
  WidgetPtr build() override {
    return Scaffold(
        AppBar("FutureBuilder — All Three Types"),
        Expanded(
            ListView(
                {
                    // ── 4. NetworkImage — URL image fetch
                    // ─────────────────────
                    SectionLabel("4 · NetworkImage — image from URL"),
                    Container(Column({
                                         Text("Cover photo (picsum.photos):")
                                             ->setFontSize(12)
                                             ->setTextColor(
                                                 Color::fromRGB(100, 100, 100)),
                                         SizedBox(0, 8),
                                         Container(NetworkImage(
                                                       "https://picsum.photos/"
                                                       "seed/fluxui/600/200")
                                                       ->setFit(ImageFit::Cover)

                                                       ->setBorderRadius(6))
                                             ->setWidth(600)
                                             ->setHeight(200),
                                     })
                                  ->setSpacing(0))
                        ->setHeight(400),
                    // ── 1. FetchBuilder — raw HTTP string
                    // ─────────────────────
                    SectionLabel("1 · FetchBuilder — raw response string"),
                    Container(
                        FetchBuilder(
                            "https://httpbin.org/get",
                            [](const AsyncSnapshot<std::string> &snap)
                                -> WidgetPtr {
                              if (snap.isLoading())
                                return LoadingWidget("Fetching raw response…");
                              if (snap.hasError())
                                return ErrorWidget(snap.error);

                              // Just show a trimmed slice of the raw JSON text
                              std::string preview =
                                  snap.data.substr(0, 120) + "…";
                              return Column(
                                         {
                                             Text("Raw bytes received:")
                                                 ->setFontWeight(
                                                     FontWeight::Bold),
                                             Text(preview)
                                                 ->setFontSize(12)
                                                 ->setTextColor(Color::fromRGB(
                                                     80, 80, 80)),
                                             Text(std::to_string(
                                                      snap.data.size()) +
                                                  " bytes total")
                                                 ->setFontSize(11)
                                                 ->setTextColor(Color::fromRGB(
                                                     140, 140, 140)),
                                         })
                                  ->setSpacing(6);
                            }))
                        ->setHeight(200),

                    SizedBox(0, 16),

                    // ── 2. JsonBuilder — parsed JsonValue tree
                    // ────────────────
                    SectionLabel("2 · JsonBuilder — parsed JsonValue"),
                    Container(
                        JsonBuilder(
                            "https://jsonplaceholder.typicode.com/users/1",
                            [](const AsyncSnapshot<JsonValue> &snap)
                                -> WidgetPtr {
                              if (snap.isLoading())
                                return LoadingWidget("Fetching user…");
                              if (snap.hasError())
                                return ErrorWidget(snap.error);

                              const auto &j = snap.data;

                              // Safely dig into nested objects
                              std::string city =
                                  j.has("address")
                                      ? j["address"]["city"].getString()
                                      : "—";
                              std::string company =
                                  j.has("company")
                                      ? j["company"]["name"].getString()
                                      : "—";

                              return Column(
                                         {
                                             Row({
                                                 Text(j["name"].getString())
                                                     ->setFontWeight(
                                                         FontWeight::Bold)
                                                     ->setFontSize(15),
                                                 Text(" · " +
                                                      j["email"].getString())
                                                     ->setFontSize(13)
                                                     ->setTextColor(
                                                         Color::fromRGB(
                                                             100, 100, 100)),
                                             }),
                                             Row({
                                                 Text("City: ")
                                                     ->setFontSize(12)
                                                     ->setTextColor(
                                                         Color::fromRGB(
                                                             120, 120, 120)),
                                                 Text(city)->setFontSize(12),
                                             }),
                                             Row({
                                                 Text("Company: ")
                                                     ->setFontSize(12)
                                                     ->setTextColor(
                                                         Color::fromRGB(
                                                             120, 120, 120)),
                                                 Text(company)->setFontSize(12),
                                             }),
                                         })
                                  ->setSpacing(6);
                            }))
                        ->setHeight(200),

                    SizedBox(0, 16),

                    // ── 3. TypedJsonBuilder — deserialized Post struct
                    // ────────
                    SectionLabel("3 · TypedJsonBuilder — deserialized struct"),
                    Container(
                        TypedJsonBuilder<Post>(
                            "https://jsonplaceholder.typicode.com/posts/1",
                            postFromJson,
                            [](const AsyncSnapshot<Post> &snap) -> WidgetPtr {
                              if (snap.isLoading())
                                return LoadingWidget("Fetching post…");
                              if (snap.hasError())
                                return ErrorWidget(snap.error);

                              const Post &p = snap.data;
                              return Column(
                                         {
                                             Row({
                                                 Text("Post #" +
                                                      std::to_string(p.id))
                                                     ->setFontWeight(
                                                         FontWeight::Bold),
                                                 Text("  by user " +
                                                      std::to_string(p.userId))
                                                     ->setFontSize(12)
                                                     ->setTextColor(
                                                         Color::fromRGB(
                                                             120, 120, 120)),
                                             }),
                                             Text(p.title)
                                                 ->setFontSize(14)
                                                 ->setFontWeight(
                                                     FontWeight::Bold)
                                                 ->setTextColor(Color::fromRGB(
                                                     30, 30, 30)),
                                             Text(p.body)
                                                 ->setFontSize(12)
                                                 ->setTextColor(Color::fromRGB(
                                                     80, 80, 80)),
                                         })
                                  ->setSpacing(8);
                            }))
                        ->setHeight(200),

                    SizedBox(0, 16),

                })
                ->setSpacing(8))
            ->setPadding(24)
            ->setBackgroundColor(Color::fromRGB(245, 246, 248)),
        nullptr, nullptr);
  }
};

// ============================================================================

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("FluxUI - App", std::make_shared<MyApp>(), AppTheme::light(),
                 false, 900, 700, false, false);
}