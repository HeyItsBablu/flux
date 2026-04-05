#include "flux/flux.hpp"

// ── Submission state machine ───────────────────────────────────────────────

enum class SubmitState { Idle, Sending, Done, Failed };

struct SubmitResult {
  SubmitState state = SubmitState::Idle;
  std::string message;
};

// ============================================================================

class MyApp : public Component {
  // ── Form state ────────────────────────────────────────────────────────
  State<std::string> titleText;
  State<std::string> bodyText;
  State<std::string> selectedCategory;
  State<bool> isPublished;
  State<double> priority;

  // ── Submission state — drives the button + banner ─────────────────────
  State<SubmitResult> submitResult;

public:
  MyApp()
      : titleText("", context), bodyText("", context),
        selectedCategory("bug", context), isPublished(false, context),
        priority(5.0, context), submitResult(SubmitResult{}, context) {}

  WidgetPtr build() override {
    return Scaffold(
        AppBar("Create Post"),
        Container(
            ListView(
                {// ── Title ─────────────────────────────────────────────
                 Text("Title")->setFontWeight(FontWeight::Bold),
                 TextInput("Post title…")->setInputValue(titleText),

                 // ── Body ──────────────────────────────────────────────
                 Text("Body")->setFontWeight(FontWeight::Bold),
                 TextInput("Write something…")->setInputValue(bodyText),

                 // ── Category ──────────────────────────────────────────
                 Text("Category")->setFontWeight(FontWeight::Bold),

                 RadioGroupWithOptions(
                     {
                         RadioOption("bug", "Bug Report"),
                         RadioOption("feature", "Feature Request"),
                         RadioOption("docs", "Documentation"),
                     })
                     ->setHorizontal()
                     ->bindValue(selectedCategory),
                 // ── Priority ──────────────────────────────────────────
                 Row({
                         Text("Priority")->setFontWeight(FontWeight::Bold),
                         Text(priority,
                              [](const double &v) {
                                return std::to_string((int)v) + " / 10";
                              })
                             ->setTextColor(Color::fromRGB(100, 100, 100)),
                     })
                     ->setSpacing(8),
                 Slider(1.0, 10.0, 1.0)
                     ->setValue(priority)
                     ->setTrackFillColor(Color::fromRGB(99, 102, 241)),

                 // ── Publish toggle ────────────────────────────────────
                 Toggle("Publish immediately")
                     ->setValue(isPublished)
                     ->setTrackOnColor(Color::fromRGB(99, 102, 241)),

                 SizedBox(0, 8),

                 // // ── Result banner (only when not idle) ────────────────
                 buildBanner(),

                 // // ── Submit button ─────────────────────────────────────
                 // buildSubmitButton(),
                 Button("Submit",
                        [&]() {
                          submitPost();
                          
                        })})
                ->setSpacing(12))
            ->setPadding(24)
            ->setBackgroundColor(Color::fromRGB(248, 249, 250)));
  }

private:
  // ── Banner ─────────────────────────────────────────────────────────────
  WidgetPtr buildBanner() {
    const auto &r = submitResult.get();

    if (r.state == SubmitState::Done) {
      return Container(Text("✓  Post created with id " + r.message)
                           ->setTextColor(Color::fromRGB(21, 128, 61))
                           ->setFontWeight(FontWeight::Bold))
          ->setPadding(12)
          ->setBackgroundColor(Color::fromRGB(220, 252, 231))
          ->setBorderRadius(6);
    }

    if (r.state == SubmitState::Failed) {
      return Container(Text("✕  " + r.message)
                           ->setTextColor(Color::fromRGB(185, 28, 28)))
          ->setPadding(12)
          ->setBackgroundColor(Color::fromRGB(254, 226, 226))
          ->setBorderRadius(6);
    }

    // Idle or Sending — no banner, zero-size placeholder
    return SizedBox(0, 0);
  }

  // ── Button ─────────────────────────────────────────────────────────────
  WidgetPtr buildSubmitButton() {
    bool sending = (submitResult.get().state == SubmitState::Sending);

    auto btn =
        Container(Center(Text(sending ? "Sending…" : "Submit Post")
                             ->setTextColor(Color::fromRGB(255, 255, 255))
                             ->setFontWeight(FontWeight::Bold)))
            ->setHeight(44)
            ->setBackgroundColor(sending ? Color::fromRGB(148, 163, 184)
                                         : Color::fromRGB(99, 102, 241))
            ->setBorderRadius(8);

    if (!sending)
      btn->onClick = [this]() { submitPost(); };

    return btn;
  }

  // ── POST ───────────────────────────────────────────────────────────────
  void submitPost() {
    if (titleText.get().empty() || bodyText.get().empty())
      return;

    // Snapshot current form values into locals so the lambda is self-contained
    std::string title = titleText.get();
    std::string body = bodyText.get();
    std::string category = selectedCategory.get();
    int pri = (int)priority.get();
    bool pub = isPublished.get();

    std::string jsonBody = "{"
                           "\"title\":" +
                           jsonStr(title) +
                           ","
                           "\"body\":" +
                           jsonStr(body) +
                           ","
                           "\"category\":" +
                           jsonStr(category) +
                           ","
                           "\"priority\":" +
                           std::to_string(pri) +
                           ","
                           "\"published\":" +
                           (pub ? "true" : "false") +
                           ","
                           "\"userId\":1"
                           "}";

    // Flip to Sending — triggers rebuild (button greys out)
    submitResult.set({SubmitState::Sending, ""});
    std::cout << "Submit clicked"<<jsonBody << std::endl;

    HttpRequest req;
    req.url = "http://localhost:3000/users";
    req.method = "POST";
    req.body = jsonBody;
    req.headers["Content-Type"] = "application/json";

    // Capture only the state by pointer — safe because state outlives the
    // lambda
    auto *result = &submitResult;

    FluxHttp::send(req, [result](HttpResult r) {
      if (!r.success) {
        result->set({SubmitState::Failed,
                     r.error.empty() ? "HTTP " + std::to_string(r.statusCode)
                                     : r.error});
        return;
      }

      JsonValue parsed;
      if (!JsonParser::tryParse(r.body, parsed)) {
        result->set({SubmitState::Failed, "JSON parse error"});
        return;
      }

      try {
        std::string id = std::to_string(parsed["id"].getInt());
        result->set({SubmitState::Done, id});
      } catch (const std::exception &e) {
        result->set({SubmitState::Failed, e.what()});
      }
    });
  }

  // ── Minimal JSON string escaper ────────────────────────────────────────
  static std::string jsonStr(const std::string &s) {
    std::string out = "\"";
    for (char c : s) {
      if (c == '"')
        out += "\\\"";
      else if (c == '\\')
        out += "\\\\";
      else if (c == '\n')
        out += "\\n";
      else if (c == '\r')
        out += "\\r";
      else if (c == '\t')
        out += "\\t";
      else
        out += c;
    }
    return out + "\"";
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("FluxUI - Post Example", BuildComponent<MyApp>(),
                 AppTheme::light(), false, 600, 700, false, false);
}