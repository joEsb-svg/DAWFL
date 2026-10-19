#pragma once

#include <JuceHeader.h>

#if JUCE_ANDROID
 #include <jni.h>
 namespace juce { extern JNIEnv* getEnv() noexcept; }
#endif

//==============================================================================
/**
    Vibrazione leggera al tocco (solo Android; sugli altri sistemi non fa nulla).
    Usa il "feedback aptico" della vista, quello delle tastiere: non serve nessun permesso.
*/
namespace Haptics
{
    inline bool& enabled()
    {
        static bool value = true;
        return value;
    }

    inline void tick (juce::Component* component)
    {
       #if JUCE_ANDROID
        if (! enabled() || component == nullptr)
            return;

        if (auto* peer = component->getPeer())
        {
            if (auto view = (jobject) peer->getNativeHandle())
            {
                auto* env = juce::getEnv();
                auto viewClass = env->GetObjectClass (view);
                static jmethodID method = env->GetMethodID (viewClass, "performHapticFeedback", "(II)Z");

                constexpr jint clockTick = 4;           // HapticFeedbackConstants.CLOCK_TICK: un "tic" leggero
                constexpr jint ignoreViewSetting = 1;   // FLAG_IGNORE_VIEW_SETTING
                if (method != nullptr)
                    env->CallBooleanMethod (view, method, clockTick, ignoreViewSetting);

                env->DeleteLocalRef (viewClass);
            }
        }
       #else
        juce::ignoreUnused (component);
       #endif
    }
}
