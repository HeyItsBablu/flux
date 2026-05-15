#include "flux/flux.hpp"

class MyApp : public Widget {
public:
    WidgetPtr build() override {

        return Scaffold(
                AppBar("Flux App"),
                Expanded(
                        Center(VideoPlayer("screenshots/sample.mp4")
                                       ->setWidth(380)
                                       ->setHeight(270)    // 16:9
                                       ->setAutoPlay(false))),
                nullptr, nullptr);
    }
};

WidgetPtr createApp(FluxUI *app) {
    return FluxApp("FluxUI - App", std::make_shared<MyApp>(), AppTheme::light(),
                   false, 900, 700, false, false);
}


// #include "flux/flux.hpp"

// class MyApp : public Widget {
// public:
//     WidgetPtr build() override {

//         return Scaffold(
//                 AppBar("Flux App"),
//                 Expanded(
//                         Center(VideoPlayerFromUrl("https://avtshare01.rz.tu-ilmenau.de/avt-vqdb-uhd-1/test_1/segments/water_netflix_750kbps_360p_59.94fps_hevc.mp4")
//                                        ->setWidth(380)
//                                        ->setHeight(270)    // 16:9
//                                        ->setAutoPlay(false))),
//                 nullptr, nullptr);
//     }
// };

// WidgetPtr createApp(FluxUI *app) {
//     return FluxApp("FluxUI - App", std::make_shared<MyApp>(), AppTheme::light(),
//                    false, 900, 700, false, false);
// }