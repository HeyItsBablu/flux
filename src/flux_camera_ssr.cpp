// src/flux_camera_ssr.cpp
#ifdef FLUX_SSR

#include "flux/flux_camera.hpp"

FluxCamera &FluxCamera::get()
{
    static FluxCamera instance;
    return instance;
}

bool FluxCamera::isFrontCamera() const { return false; }

bool FluxCamera::open(bool /*useFront*/) { return false; }
void FluxCamera::flipCamera() {}
bool FluxCamera::updateFrame() { return false; }
void FluxCamera::capturePhoto() {}
void FluxCamera::close() {}

void FluxCamera::setFlash(bool on) { _flashOn = on; }
void FluxCamera::_applyFlash() {}

FluxCamera::FrameLock FluxCamera::lockFrame()
{
    return FrameLock{}; // unlocked, data=nullptr — same idiom as flux_video_ssr.cpp
}

#endif // FLUX_SSR