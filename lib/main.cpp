#include "flux/flux.hpp"

// ============================================================================
// Data struct for TypedJsonBuilder
// ============================================================================

struct Post
{
    int id;
    int userId;
    std::string title;
    std::string body;
};

Post postFromJson(const JsonValue &j)
{
    return Post{j["id"].getInt(), j["userId"].getInt(), j["title"].getString(),
                j["body"].getString()};
}

// ============================================================================
// Reusable card Flex
// ============================================================================

static WidgetPtr SectionLabel(const std::string &text)
{
    return Text(text)
        ->setFontWeight(FontWeight::Bold)
        ->setFontSize(11)
        ->setTextColor(Color::fromRGB(120, 120, 120));
}

// ============================================================================
// Shared loading / error helpers
// ============================================================================

static WidgetPtr LoadingWidget(const std::string &label)
{
    return Flex({
        Text(label)->setTextColor(Color::fromRGB(140, 140, 140))->setFontSize(13),
    });
}

static WidgetPtr ErrorWidget(const std::string &err)
{
    return Flex({
               Text("Error: " + err)->setTextColor(Color::fromRGB(220, 53, 69))})
        ->setPadding(8)
        ->setBackgroundColor(Color::fromRGB(255, 235, 238))
        ->setBorderRadius(6);
}

// ============================================================================
// App component
// ============================================================================

class MyApp : public Widget
{
public:
    WidgetPtr build() override
    {
        return Flex(
                   {

                       SectionLabel("TypedJsonBuilder — deserialized struct"),
                       Flex({
                           TypedJsonBuilder<Post>(
                               "https://jsonplaceholder.typicode.com/posts/1",
                               postFromJson,
                               [](const AsyncSnapshot<Post> &snap) -> WidgetPtr
                               {
                                   if (snap.isLoading())
                                       return LoadingWidget("Fetching post…");
                                   if (snap.hasError())
                                       return ErrorWidget(snap.error);

                                   const Post &p = snap.data;
                                   return Flex(
                                              {
                                                  Flex({
                                                      Text("Post " +
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
                                       ->setGap(8)->setDirection(FlexDirection::Column);
                               })})
                           ->setHeight(200),

                       SizedBox(0, 16),

                   })

            ->setGap(8)
            ->setScrollable(true)
            ->setDirection(FlexDirection::Column)
            ->setWidthMode(SizeMode::Full)
            ->setHeightMode(SizeMode::Full);
    }
};

// ============================================================================
WidgetPtr createApp(FluxUI *app)
{
  return FluxApp()
      .setTheme(AppTheme::light())
      .build(std::make_shared<MyApp>());
}