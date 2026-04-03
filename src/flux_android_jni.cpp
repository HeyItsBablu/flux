// flux_android_jni.cpp
#ifdef __ANDROID__
#include "flux_android_jni.hpp"

android_app* FluxJNI::s_app = nullptr;
JavaVM*      FluxJNI::s_vm  = nullptr;
std::unordered_map<int, std::function<void(bool)>> FluxJNI::s_callbacks;

#endif