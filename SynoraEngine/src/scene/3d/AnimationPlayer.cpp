
#include <SynoraEngine/project/AssetManager.h>
#include <SynoraEngine/project/assets/AnimationClipData.h>
#include <SynoraEngine/project/assets/ModelData.h>

#include <SynoraEngine/scene/3d/AnimationPlayer.h>

namespace SYN {
namespace {
bool isAssetManagerNULL(AssetManager *assetManager, const std::string &msg) {
    if (assetManager == nullptr) {
        spdlog::error(msg);
        return true;
    }
    return false;
}
} // namespace

// AnimationPlayer
// Supports playing single animation clips, or blending between 2 clips.
// Create an AnimationPlayer per instance and update in the layer's update
// function. Get the output bone matrices and submit as part of the draw command
// to animate a model.

AnimationPlayer::AnimationPlayer(AssetManager *assetManager) {
    m_AssetManager = assetManager;
}

void AnimationPlayer::setClip(AssetRef clip) {
    if (isAssetManagerNULL(m_AssetManager,
                           "Cannot set clip because asset manager is NULL."))
        return;

    m_Clip = clip;
    m_DefaultPose = true;
}

void AnimationPlayer::setTargetClip(AssetRef target) {
    if (isAssetManagerNULL(
            m_AssetManager,
            "Cannot set target clip because asset manager is NULL."))
        return;

    m_TargetClip = target;
    m_TargetTime = 0.0f;
}

void AnimationPlayer::setBlendWeight(float blendWeight) {
    m_BlendWeight = glm::clamp(blendWeight, 0.0f, 1.0f);
}

void AnimationPlayer::setLoop(bool loop) { m_IsLooping = loop; }

const std::vector<glm::mat4> &AnimationPlayer::getOutput() const {
    return m_BoneMatrices;
}

void AnimationPlayer::play(std::optional<float> time) {
    if (isAssetManagerNULL(m_AssetManager,
                           "Cannot play clip because asset manager is NULL."))
        return;

    if (!m_Clip.valid()) {
        spdlog::error(
            "Unable to begin playback because specified clip is invalid.");
        return;
    }
    const AnimationClipData *clip =
        m_AssetManager->get<AnimationClipData>(m_Clip.uuid());
    m_IsPlaying = true;
    float playTime = time.value_or(m_CurrentTime);
    if (!time.has_value() && m_CurrentTime == clip->duration) {
        playTime = 0.0f;
    }

    m_CurrentTime = glm::clamp(playTime, 0.0f, clip->duration);
}
void AnimationPlayer::stop() { m_IsPlaying = false; }

void AnimationPlayer::crossfadeTo(AssetRef target, float duration) {
    if (isAssetManagerNULL(
            m_AssetManager,
            "Cannot crossfade to target clip because asset manager is NULL."))
        return;

    if (!m_Clip.valid()) {
        spdlog::error("Cannot crossfade from NULL clip");
        return;
    }

    const AnimationClipData *targetClip =
        m_AssetManager->get<AnimationClipData>(target.uuid());

    if (targetClip == nullptr) {
        spdlog::error("Cannot crossfade to NULL clip");
        return;
    }

    m_IsPlaying = true;
    setTargetClip(target);
    m_CrossfadeDuration = duration;
    m_CrossfadeTime = 0.0f;
    m_IsCrossfading = true;
    m_BlendWeight = 0.0f;
}

void AnimationPlayer::playOneShot(AssetRef to, AssetRef returnTo, float blendIn,
                                  float blendOut) {
    if (isAssetManagerNULL(
            m_AssetManager,
            "Cannot play one shot because asset manager is NULL."))
        return;

    const AnimationClipData *returnToClip =
        m_AssetManager->get<AnimationClipData>(returnTo.uuid());
    if (returnToClip == nullptr) {
        spdlog::error("Cannot return to NULL clip");
        return;
    }

    m_IsPlayingOneshot = true;
    m_ReturnTo = returnTo;
    m_BlendOut = blendOut;

    crossfadeTo(to, blendIn);
}

void AnimationPlayer::update(UUID model, float dt) {
    if (m_AssetManager == nullptr) {
        spdlog::error("Cannot update animation player because asset manager "
                      "reference is NULL.");
        return;
    }

    const ModelData *modelData = m_AssetManager->get<ModelData>(model);

    if (modelData == nullptr) {
        spdlog::error("Model handle passed to animation player is invalid.");
        return;
    }

    if (modelData->skeleton.boneInfo.empty()) {
        spdlog::error("Model does not have valid skeleton.");
        return;
    }

    if (!m_Clip.valid())
        return;

    const AnimationClipData *mainClip =
        m_AssetManager->get<AnimationClipData>(m_Clip.uuid());

    if (mainClip->fps == 0.0f) {
        spdlog::error("Current clip {} has fps of 0.0 which is invalid.",
                      mainClip->name);
        return;
    }

    if (m_DefaultPose) {
        m_BoneMatrices =
            std::vector<glm::mat4>(modelData->skeleton.boneInfo.size(),
                                   modelData->skeleton.inverseRoot);
        m_DefaultPose = false;
    }

    m_LastPlayedTime = m_CurrentTime;

    auto advanceTime = [&](const AnimationClipData *clip, float time) {
        time += clip->fps * dt;
        if (m_IsLooping && !m_IsPlayingOneshot) {
            time = std::fmod(time, clip->duration);
        } else {
            time = glm::clamp(time, 0.0f, clip->duration);
        }

        return time;
    };

    const AnimationClipData *targetClip = nullptr;
    if (m_TargetClip.valid()) {
        targetClip =
            m_AssetManager->get<AnimationClipData>(m_TargetClip.uuid());
    }

    if (m_IsPlaying) {
        m_CurrentTime = advanceTime(mainClip, m_CurrentTime);
        if (targetClip) {
            m_TargetTime = advanceTime(targetClip, m_TargetTime);
        }
    } else {
        return;
    }

    if (m_IsPlayingOneshot && !m_IsCrossfading) {
        if (m_CurrentTime >= mainClip->duration - m_BlendOut) {
            crossfadeTo(m_ReturnTo, m_BlendOut);
            m_BlendOut = 0.0f;
            m_IsPlayingOneshot = false;
        }
    }

    if (m_IsCrossfading) {
        m_CrossfadeTime += dt;
        float t = 1.0f;
        if (m_CrossfadeDuration > 0.0f) {
            t = glm::clamp(m_CrossfadeTime / m_CrossfadeDuration, 0.0f, 1.0f);
        }
        m_BlendWeight = glm::smoothstep(0.0f, 1.0f, t);
        if (m_CrossfadeTime >= m_CrossfadeDuration) {
            m_IsCrossfading = false;

            m_Clip = m_TargetClip;
            mainClip = m_AssetManager->get<AnimationClipData>(m_Clip.uuid());

            m_TargetClip = {};
            targetClip = nullptr;

            m_CurrentTime = m_TargetTime;

            m_BlendWeight = 0.0f;
            if (!m_IsPlayingOneshot && m_ReturnTo.valid()) {
                m_ReturnTo = {};
            }
        }
    }

    if (!m_IsLooping) {
        if (m_CurrentTime >= mainClip->duration &&
            m_LastPlayedTime == m_CurrentTime && !m_IsCrossfading) {
            m_IsPlaying = false;
            return;
        }
    }

    struct Pose {
        glm::vec3 position;
        glm::quat rotation;
        glm::vec3 scale;
    };

    auto sampleVectorKey = [](const std::vector<VectorKey> &keys, float time) {
        if (keys.empty())
            return glm::vec3(0.0f);
        if (keys.size() == 1) {
            return keys[0].value;
        }
        auto nextKey = std::upper_bound(keys.cbegin(), keys.cend(), time,
                                        [](float time, const VectorKey &key) {
                                            return time < key.timestamp;
                                        });
        if (nextKey == keys.cbegin())
            return nextKey->value;
        if (nextKey == keys.cend())
            return keys.back().value;

        auto lastKey = nextKey - 1;

        float t = (time - lastKey->timestamp) /
                  (nextKey->timestamp - lastKey->timestamp);
        return glm::mix(lastKey->value, nextKey->value, t);
    };

    auto sampleQuatKey = [](const std::vector<QuatKey> &keys, float time) {
        if (keys.empty()) {
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }

        if (keys.size() == 1) {
            return keys[0].value;
        }
        auto nextKey = std::upper_bound(keys.cbegin(), keys.cend(), time,
                                        [](float time, const QuatKey &key) {
                                            return time < key.timestamp;
                                        });
        if (nextKey == keys.cbegin())
            return nextKey->value;
        if (nextKey == keys.cend())
            return keys.back().value;

        auto lastKey = nextKey - 1;

        float t = (time - lastKey->timestamp) /
                  (nextKey->timestamp - lastKey->timestamp);
        return glm::slerp(lastKey->value, nextKey->value, t);
    };

    auto samplePose = [&](const AnimationClipData *target,
                          const Skeleton::Node &node, float time) -> Pose {
        auto channels = target->channels.find(node.name);
        Pose output{node.position, node.rotation, node.scale};
        if (channels != target->channels.cend()) {
            glm::vec3 position =
                sampleVectorKey(channels->second.translation, time);

            glm::vec3 scale = sampleVectorKey(channels->second.scale, time);
            if (channels->second.scale.empty())
                scale = glm::vec3(1.0f);

            glm::quat rotation = sampleQuatKey(channels->second.rotation, time);

            output.position = position;
            output.scale = scale;
            output.rotation = rotation;
        }
        return output;
    };

    uint32_t nodeCount = modelData->skeleton.nodes.size();
    std::vector<glm::mat4> globalTransforms(nodeCount, glm::mat4(1.0f));
    for (uint32_t i = 0; i < nodeCount; ++i) {
        const Skeleton::Node &node = modelData->skeleton.nodes.at(i);
        Pose poseA = samplePose(mainClip, node, m_CurrentTime);
        Pose local = poseA;
        if (targetClip && m_BlendWeight > 0.0f) {
            Pose poseB = samplePose(targetClip, node, m_TargetTime);
            local.position =
                glm::mix(poseA.position, poseB.position, m_BlendWeight);
            local.scale = glm::mix(poseA.scale, poseB.scale, m_BlendWeight);
            local.rotation =
                glm::slerp(poseA.rotation, poseB.rotation, m_BlendWeight);
        }

        glm::mat4 identity(1.0f);
        glm::mat4 localTransform = glm::translate(identity, local.position) *
                                   glm::mat4(glm::normalize(local.rotation)) *
                                   glm::scale(identity, local.scale);

        if (node.parentIndex.has_value()) {
            uint32_t parentIndex = node.parentIndex.value();
            globalTransforms[i] =
                globalTransforms[parentIndex] * localTransform;
        } else {
            globalTransforms[i] = localTransform;
        }
    }

    for (uint32_t i = 0; i < m_BoneMatrices.size(); ++i) {
        const Skeleton::BoneInfo &info = modelData->skeleton.boneInfo.at(i);
        m_BoneMatrices[i] = modelData->skeleton.inverseRoot *
                            globalTransforms[info.nodeIndex] *
                            info.inverseBindMatrix;
    }
}

float AnimationPlayer::getCurrentTime() const { return m_CurrentTime; }

bool AnimationPlayer::isLooping() const { return m_IsLooping; }

bool AnimationPlayer::isPlaying() const { return m_IsPlaying; }

float AnimationPlayer::getDuration() const {
    const AnimationClipData *mainClip =
        m_AssetManager->get<AnimationClipData>(m_Clip.uuid());
    if (mainClip == nullptr)
        return 0.0f;
    return mainClip->duration;
}
} // namespace SYN
