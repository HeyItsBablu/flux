// test_mic.cpp
#include "flux/flux.hpp"
#include "flux/flux_mic_controller.hpp"
#include <cstdio>

// ── A tiny level-meter widget: a colored bar whose width tracks State<float> ──
class LevelMeterWidget : public Widget
{
public:
    LevelMeterWidget()
    {
        autoWidth = false;
        autoHeight = false;
        width = 200;
        height = 16;
        hasBackground = true;
        backgroundColor = Color::fromRGB(40, 40, 40);
        borderRadius = 4;
    }

    void render(GraphicsContext &ctx, FontCache &fontCache) override
    {
        Painter p(ctx);
        p.fillRoundedRect(x, y, width, height, borderRadius, backgroundColor);

        int fillW = (int)(std::min(1.f, _level * 4.f) * width); // scale up, RMS is quiet
        if (fillW > 0)
        {
            Color fill = _level > 0.5f ? Color::fromRGB(220, 80, 80)
                                       : Color::fromRGB(80, 200, 120);
            p.fillRoundedRect(x, y, fillW, height, borderRadius, fill);
        }
        needsPaint = false;
    }

    void setLevel(float l) { _level = l; markNeedsPaint(); }

private:
    float _level = 0.f;
};

using LevelMeterPtr = std::shared_ptr<LevelMeterWidget>;
inline LevelMeterPtr LevelMeter(State<float> &state)
{
    auto w = std::make_shared<LevelMeterWidget>();
    state.bindProperty(w, [](Widget *widget, const float &v)
    {
        static_cast<LevelMeterWidget *>(widget)->setLevel(v);
    }, false);
    return w;
}

// ── App ──────────────────────────────────────────────────────────────────
class MyApp : public Widget
{
public:
    MyApp()
    {
        mic = Mic();

        mic->setOnFrame([](const float *samples, size_t count)
        {
            // Runs on the capture thread — keep it cheap, just logging here.
            float peak = 0.f;
            for (size_t i = 0; i < count; i++)
                peak = std::max(peak, std::fabs(samples[i]));
            // printf from an audio thread is not great practice long-term,
            // but fine for a quick smoke test.
            printf("frame: %zu samples, peak=%.3f\n", count, peak);
        });
    }

    WidgetPtr build() override
    {
        return Flex({
            Text("Mic Test")->setFontSize(20),

            Conditional(mic->isRecording)
                ->Then([mic = mic]() {
                    return Flex({
                        Text("Recording…")->setTextColor(Color::fromRGB(80, 200, 120)),
                        LevelMeter(mic->level),
                        Button("Stop", [mic]() { mic->toggle(); }),
                    })->setDirection(FlexDirection::Column)->setGap(8);
                })
                ->Else([mic = mic]() {
                    return Button("Start Recording", [mic]() { mic->toggle(); });
                }),
        })
        ->setDirection(FlexDirection::Column)
        ->setGap(16)
        ->setPadding(24)
        ->setAlignItems(AlignItems::Stretch)
        ->setWidthMode(SizeMode::Full)
        ->setHeightMode(SizeMode::Full);
    }

private:
    MicControllerPtr mic;
};

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp("Mic Test")
        .setTheme(AppTheme::light())
        .setFullscreenMode(true)
        .build(std::make_shared<MyApp>());
}