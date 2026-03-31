#include "flux/flux.hpp"


class MyApp : public Component {
  State<std::string> statusText;

public:
  MyApp() : statusText("No row selected", context) {}

  WidgetPtr build() override {

    // ── Columns ───────────────────────────────────────────────────────────
    std::vector<DataColumn> columns = {
        DataColumn("name",   "Name",   180),
        DataColumn("role",   "Role",   140),
        DataColumn("dept",   "Dept",   120),
        DataColumn("age",    "Age",     60).setAlign(ColumnAlign::Right),
        DataColumn("salary", "Salary", 100)
            .setAlign(ColumnAlign::Right)
            .setFormatter([](const std::string &v) { return "$" + v + "k"; }),
    };

    // ── Rows ──────────────────────────────────────────────────────────────
    std::vector<DataRow> rows = {
        DataRow("1").set("name","Alice Martin")  .set("role","Engineer")    .set("dept","Platform").set("age","29").set("salary","120"),
        DataRow("2").set("name","Bob Chen")       .set("role","Designer")    .set("dept","Product") .set("age","34").set("salary","95"),
        DataRow("3").set("name","Carol Davis")    .set("role","PM")          .set("dept","Product") .set("age","31").set("salary","110"),
        DataRow("4").set("name","David Kim")      .set("role","Engineer")    .set("dept","Infra")   .set("age","27").set("salary","115"),
        DataRow("5").set("name","Eva Rossi")      .set("role","QA")          .set("dept","Platform").set("age","26").set("salary","88"),
        DataRow("6").set("name","Frank Müller")   .set("role","Team Lead")   .set("dept","Infra")   .set("age","38").set("salary","145"),
        DataRow("7").set("name","Grace Liu")      .set("role","Designer")    .set("dept","Product") .set("age","30").set("salary","98"),
        DataRow("8").set("name","Henry Okafor")   .set("role","Engineer")    .set("dept","Platform").set("age","33").set("salary","125"),
        DataRow("9").set("name","Irina Popova")   .set("role","Analyst")     .set("dept","Finance") .set("age","28").set("salary","92"),
        DataRow("10").set("name","Jake Torres")   .set("role","Engineer")    .set("dept","Infra")   .set("age","25").set("salary","108"),
        DataRow("11").set("name","Karen Smith")   .set("role","HR Manager")  .set("dept","HR")      .set("age","41").set("salary","102"),
        DataRow("12").set("name","Liam Brown")    .set("role","Intern")      .set("dept","Platform").set("age","22").set("salary","45"),
    };

    // ── Layout ────────────────────────────────────────────────────────────
    return Scaffold(
        AppBar("DataTable Demo"),
        Column({

            // ── Table ─────────────────────────────────────────────────────
            Expanded(
                Container(
                    DataTable(columns, rows)
                        ->setAlternateRows(true)
                        ->setShowColumnDividers(false)
                        ->setAccentColor(Color::fromRGB(33, 150, 243))
                        ->setOnRowSelected([this](int idx, const DataRow &row) {
                            statusText.set(
                                "Selected: " + row.get("name") +
                                " — " + row.get("role") +
                                " (" + row.get("dept") + ")"
                            );
                        })
                        ->setOnRowDoubleClicked([this](int /*idx*/, const DataRow &row) {
                            statusText.set(
                                "Opened: " + row.get("name") +
                                ", Age " + row.get("age") +
                                ", Salary $" + row.get("salary") + "k"
                            );
                        })
                        ->setFlex(1)
                )
                ->setPadding(16)
            ),

            // ── Status bar ────────────────────────────────────────────────
            Container(
                Text(statusText)
                    ->setFontSize(12)
                    ->setTextColor(Color::fromRGB(80, 80, 80))
            )
            ->setBackgroundColor(Color::fromRGB(245, 246, 248))
            ->setBorderColor(Color::fromRGB(220, 220, 222))
            ->setBorderWidth(1)
            ->setPaddingAll(12, 8, 12, 8),

        })
        ->setSpacing(0)
    );
  }
};

WidgetPtr createApp(FluxUI* app) {
    return FluxApp(
        "FluxUI - Paint",
        BuildComponent<MyApp>(),
        AppTheme::dark(),
        false,   // debugShowWidgetBounds
        900,     // width
        700,     // height
        false,   // maximize
        true     // fullscreen
    );
}