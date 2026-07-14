// // camera_widget_ssr.cpp
// #ifdef FLUX_SSR

// #include "flux/widgets/camera_widget.hpp"

// void CameraWidget::_platformScheduleOpen()
// {
//     // Never actually opens server-side. The CLIENT's own build
//     // (camera_widget_web.cpp, a separate WASM binary) independently
//     // calls this again during its own computeLayout() and sets
//     // _shouldOpen there — SSR and client are different processes;
//     // nothing here needs to hand anything off.
//     _shouldOpen = false;
// }

// void CameraWidget::_platformOnFlip() { /* nothing cached server-side to invalidate */ }

// bool CameraWidget::_platformRenderPreview(GraphicsContext &, Painter &, FontCache &, int)
// {
//     // Always false — falls through to render()'s "Opening camera..."
//     // placeholder, same text the client shows in the gap between
//     // hydration boot and the user's permission grant.
//     return false;
// }

// void CameraWidget::_platformRenderFlash(GraphicsContext &, Painter &p, int viewH)
// {
//     // Unreachable in practice (_flashAlpha only becomes nonzero via
//     // _triggerCapture(), which nothing calls during a render-only SSR
//     // pass) — implemented only so the symbol resolves.
//     p.fillRect(x, y, width, viewH, colFlash);
// }

// bool CameraWidget::_platformRenderThumb(GraphicsContext &, int, int, int, int)
// {
//     return false; // _lastPhotoPath can never be populated server-side
// }

// void CameraWidget::_platformLoadThumb(const std::string &) { /* unreachable, see above */ }
// void CameraWidget::_platformDestroy() { /* nothing owned server-side */ }

// #endif // FLUX_SSR