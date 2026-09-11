#include <SynoraEngine/project/AssetManager.h>

#include <SynoraEngine/project/importers/AnimationImporter.h>
#include <SynoraEngine/project/importers/ModelImporter.h>
#include <SynoraEngine/project/importers/TextureImporter.h>

namespace SYN {

void AssetManager::init() {
    registerImporter<TextureData, TextureImporter>();
    registerImporter<ModelData, ModelImporter>();
    registerImporter<AnimationClipData, AnimationImporter>();
}

AssetRef AssetManager::acquire(UUID id) {
    if (m_Owner.find(id) == m_Owner.cend())
        return {};
    return AssetRef(this, id);
}

void AssetManager::addRef(UUID id) { ++m_AssetRefs[id]; }
void AssetManager::removeRef(UUID id) {
    if (m_AssetRefs[id] > 0 && --m_AssetRefs[id] == 0) {
        m_PendingDelete.push_back(id);
    }
}

void AssetManager::registerCleanup(CleanupFn fn) {
    m_CleanupObservers.emplace_back(std::move(fn));
}

void AssetManager::resolvePendingDeletions() {
    while (!m_PendingDelete.empty()) {

        std::vector<UUID> toDelete;
        toDelete.swap(m_PendingDelete);

        for (UUID id : toDelete) {
            auto it = m_AssetRefs.find(id);

            if (it == m_AssetRefs.cend() || it->second > 0)
                continue;

            for (auto &fn : m_CleanupObservers)
                fn(id);

            auto poolType = m_Owner.at(id);
            m_Pools.at(poolType)->remove(id);

            m_AssetRefs.erase(id);
            m_Owner.erase(id);

            if (auto pathIt = m_UUIDToKey.find(id);
                pathIt != m_UUIDToKey.cend()) {
                m_KeyToUUID.erase(pathIt->second);
                m_UUIDToKey.erase(pathIt);
            }

            if (auto groupIt = m_UUIDToGroup.find(id);
                groupIt != m_UUIDToGroup.cend()) {
                if (auto keyToGroupIt = m_KeyToGroupUUID.find(groupIt->second);
                    keyToGroupIt != m_KeyToGroupUUID.cend()) {
                    auto &group = keyToGroupIt->second;
                    group.erase(std::remove(group.begin(), group.end(), id),
                                group.end());
                    if (group.empty())
                        m_KeyToGroupUUID.erase(keyToGroupIt);
                }
                m_UUIDToGroup.erase(groupIt);
            }
        }
    }
}

std::optional<UUID> AssetManager::uuidFromKey(std::string_view key) const {
    if (auto it = m_KeyToUUID.find(std::string(key));
        it != m_KeyToUUID.cend()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<std::string> AssetManager::keyFromUUID(UUID id) const {
    if (auto it = m_UUIDToKey.find(id); it != m_UUIDToKey.cend()) {
        return it->second;
    }
    return std::nullopt;
}

} // namespace SYN
