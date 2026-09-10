#pragma once

#include <SynoraEngine/project/AssetRef.h>
#include <SynoraEngine/project/UUID.h>
#include <SynoraEngine/project/assets/AnimationClipData.h>

namespace SYN {

class AnimationPlayer {
  public:
    AnimationPlayer() = default;
    AnimationPlayer(class AssetManager *assetManager);

    void setClip(AssetRef clip);
    void setTargetClip(AssetRef target);
    void setBlendWeight(float blendWeight);
    void setLoop(bool loop);

    // If time is nullopt then resume from current internal time, otherwise,
    // start playing at specified time
    void play(std::optional<float> time = std::nullopt);

    // Blends between current clip to target clip in time specified by duration
    void crossfadeTo(AssetRef target, float duration);

    // Blends between current clip to 'to' clip (with blendIn time) and from
    // 'to' to 'returnTo' (with blendOut time).
    void playOneShot(AssetRef to, AssetRef returnTo, float blendIn,
                     float blendOut);

    // Pause playback
    void stop();

    const std::vector<glm::mat4> &getOutput() const;
    float getCurrentTime() const;
    bool isLooping() const;
    bool isPlaying() const;
    float getDuration() const;

    void update(UUID model, float dt);

  private:
    class AssetManager *m_AssetManager;

    AssetRef m_Clip;
    float m_CurrentTime = 0.0f;
    float m_TargetTime = 0.0f;
    float m_LastPlayedTime = 0.0f;

    AssetRef m_TargetClip;
    float m_BlendWeight = 0.0f;

    float m_CrossfadeDuration = 0.0f;
    float m_CrossfadeTime = 0.0f;
    bool m_IsCrossfading = false;

    bool m_IsPlayingOneshot = false;
    AssetRef m_ReturnTo;
    float m_BlendOut = 0.0f;

    bool m_IsLooping = false;
    bool m_IsPlaying = false;

    bool m_DefaultPose = true;

    std::vector<glm::mat4> m_BoneMatrices;
};

} // namespace SYN
