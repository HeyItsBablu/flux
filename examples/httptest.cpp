#include "flux/flux.hpp"
#include "flux/flux_http.hpp"
#include "flux/flux_json.hpp"

struct Post
{
  std::string id;
  std::string title;
  std::string body;
};

class CrudApp : public Widget
{

  State<std::vector<Post>> posts{{}};
  State<std::string> titleInput{""};
  State<std::string> bodyInput{""};
  State<std::string> status{""};
  State<bool> loading{true};

  const std::string BASE_URL = "https://www.exampleapi.com/post";

  // ── Helpers ────────────────────────────────────────────────────────────────

  HttpRequest makeRequest(const std::string &url,
                          const std::string &method = "GET")
  {
    HttpRequest req;
    req.url = url;
    req.method = method;
    req.verifySsl = false;
    req.headers["Content-Type"] = "application/json";
    return req;
  }

  std::vector<Post> parsePosts(const std::string &json)
  {
    JsonValue root;
    if (!JsonParser::tryParse(json, root) || !root.isArray())
      return {};

    std::vector<Post> result;
    for (const auto &item : root.asArray())
    {
      Post p;
      p.id = item["_id"].getString();
      p.title = item["title"].getString();
      p.body = item["body"].getString();
      result.push_back(p);
    }
    return result;
  }

  // ── CRUD Operations ────────────────────────────────────────────────────────

  void fetchPosts()
  {
    loading.set(true);
    status.set("Loading...");

    auto req = makeRequest(BASE_URL);
    FluxHttp::send(req, [this](HttpResult res)
                   {
      loading.set(false);
      if (!res.success) {
        status.set("Fetch error: " + res.error);
        return;
      }
      posts.set(parsePosts(res.body)); // full replace — fine for initial load
      status.set("Loaded " + std::to_string(posts.size()) + " posts"); });
  }

  void createPost()
  {
    if (titleInput.get().empty())
    {
      status.set("Title is required");
      return;
    }

    std::string payload = R"({"title":")" + titleInput.get() + R"(","body":")" +
                          bodyInput.get() + R"("})";
    status.set("Creating...");

    auto req = makeRequest(BASE_URL, "POST");
    req.body = payload;

    FluxHttp::send(req, [this](HttpResult res)
                   {
      if (!res.success) {
        status.set("Create failed: " + res.error);
        return;
      }

      JsonValue json;
      if (!JsonParser::tryParse(res.body, json)) {
        status.set("Create failed: bad response");
        return;
      }

      Post newPost;
      newPost.id = json["_id"].getString();
      newPost.title = json["title"].getString();
      newPost.body = json["body"].getString();
      std::cout << "New post title :: " << newPost.title << std::endl;

      posts.push_front(newPost);

      for (const auto &post : posts.get()) {
        std::cout << post.id << " " << post.title << std::endl;
      }

      titleInput.set("");
      bodyInput.set("");
      status.set("Created: " + newPost.title); });
  }

  void deletePost(const std::string &id)
  {
    status.set("Deleting...");

    auto req = makeRequest(BASE_URL + "/" + id, "DELETE");
    FluxHttp::send(req, [this, id](HttpResult res)
                   {
      if (!res.success) {
        status.set("Delete failed: " + res.error);
        return;
      }

      posts.remove_if([&id](const Post &p) { return p.id == id; });

      status.set("Deleted successfully"); });
  }

public:
  void onMount() override { fetchPosts(); }

  WidgetPtr build() override
  {
    return Flex(
               {

                   // ── Status bar ───────────────────────────────────────────
                   Flex({Text(status)->setTextColor(
                            Color::fromRGB(255, 255, 255))})
                       ->setBackgroundColor(Color::fromRGB(33, 150, 243))
                       ->setPadding(8)
                       ->setHeight(50),

                   // ── Create form ──────────────────────────────────────────

                   Flex(
                       {
                           Text("New Post")
                               ->setFontSize(16)
                               ->setFontWeight(FontWeight::Bold),
                           TextInput("Title")->setInputValue(titleInput),
                           TextInput("Body")->setInputValue(bodyInput),
                           Button("Create Post", [this]
                                  { createPost(); })
                               ->setBackgroundColor(
                                   Color::fromRGB(33, 150, 243)),
                       })
                       ->setDirection(FlexDirection::Column)
                       ->setPadding(16)
                       ->setBackgroundColor(Color::fromRGB(245, 245, 245))
                       ->setHeight(200),

                   // ── Posts list ───────────────────────────────────────────
                   Expanded(
                       ListView(posts)
                           ->setKeyFn([](const Post &p) -> uintptr_t
                                      { return std::hash<std::string>{}(p.id); })
                           ->itemBuilder([this](int,
                                                const Post &p) -> WidgetPtr
                                         { return Container(
                                                      Row({
                                                              Expanded(
                                                                  Column(
                                                                      {
                                                                          Text(p.title)
                                                                              ->setFontWeight(
                                                                                  FontWeight::
                                                                                      Bold),
                                                                          Text(p.body)
                                                                              ->setTextColor(
                                                                                  Color::
                                                                                      fromRGB(
                                                                                          100,
                                                                                          100,
                                                                                          100))
                                                                              ->setFontSize(
                                                                                  13),
                                                                          Text("id: " + p.id)
                                                                              ->setTextColor(
                                                                                  Color::
                                                                                      fromRGB(
                                                                                          180,
                                                                                          180,
                                                                                          180))
                                                                              ->setFontSize(
                                                                                  11),
                                                                      })
                                                                      ->setSpacing(4)),
                                                              Button("Delete",
                                                                     [this, id = p.id]
                                                                     {
                                                                       deletePost(id);
                                                                     })
                                                                  ->setBackgroundColor(
                                                                      Color::fromRGB(244, 67,
                                                                                     54))
                                                                  ->setPadding(8),
                                                          })
                                                          ->setSpacing(12)
                                                          ->setCrossAxisAlignment(
                                                              CrossAxisAlignment::Center))
                                               ->setPadding(14)
                                               ->setBackgroundColor(
                                                   Color::fromRGB(255, 255, 255))
                                               ->setBorderRadius(6)
                                               ->setHeight(100); })
                           ->setSpacing(8))})
        ->setBackgroundColor(Color::fromRGB(280, 180, 180))
        ->setScrollable(false)
        ->setDirection(FlexDirection::Column) // base (mobile): stacked
        ->setGap(8)
        ->setPadding(16)
        ->setAlignItems(AlignItems::Stretch)
        ->setWidthMode(SizeMode::Full)
        ->setHeightMode(SizeMode::Full);
  }
};

WidgetPtr createApp(FluxUI *app)
{
  return FluxApp("CRUD App", std::make_shared<CrudApp>(), AppTheme::light(),
                 false, 900, 700, false, false);
}