#pragma once

#include <spdlog/spdlog.h>

#include <typeindex>

#include "AssetImporter.h"
#include "AssetPool.h"
#include "AssetRef.h"

namespace SYN {
class AssetManager {
  public:
    using CleanupFn = std::function<void(UUID)>;

  public:
    AssetManager() = default;
    ~AssetManager() = default;

    // Register default importers to use here
    void init();

    template <typename PoolT> void registerPoolName(std::string_view poolName) {
        getPool<PoolT>().registerName(poolName);
    }

    template <typename AssetT> const AssetT *get(UUID id) const {
        const AssetPool<AssetT> *pool = findPool<AssetT>();
        if (pool == nullptr)
            return nullptr;
        return pool->getRef(id);
    }

    template <typename AssetT> AssetT *getMut(UUID id) {
        AssetPool<AssetT> *pool = findPool<AssetT>();
        if (pool == nullptr)
            return nullptr;
        return pool->getRefMut(id);
    }

    std::optional<UUID> uuidFromKey(std::string_view key) const;
    std::optional<std::string> keyFromUUID(UUID id) const;

    template <typename AssetT, typename ImporterT> void registerImporter() {
        static_assert(std::derived_from<ImporterT, AssetImporter<AssetT>>);
        m_Importers[std::type_index(typeid(AssetT))] =
            std::make_unique<ImporterT>();
    }

    // Why would I add an asset without a key?
    //
    // The use case could be rare, but you might want
    // some assets to live without a key (i.e. you store a UUID directly
    // into a single named variable for its entire usage).
    template <typename AssetT>
    UUID add(AssetT asset, std::string_view customKey = "") {
        UUID id = generateUUID();
        getPool<AssetT>().add(id, std::move(asset));
        m_Owner.insert_or_assign(id, typeid(AssetT));
        std::string customKeyString = std::string(customKey);

        bool keyLoaded =
            !customKey.empty() && m_KeyToUUID.contains(customKeyString);
        if (!keyLoaded) {
            m_KeyToUUID[customKeyString] = id;
            m_UUIDToKey[id] = customKeyString;
        } else {
            // The asset is intentionally added to the pool even if it's not
            // successfully keyed.
            //
            // Getting here is somewhat rare since you would normally
            // key an asset with `loadWithKey` and that returns an invalid UUID
            // if the key is already linked.
            //
            // If you do get here, then there's no reason to block the program
            // just because you couldn't key it (during development). This would
            // be a bug if you released it which is why you get a warning to fix
            // it.
            spdlog::warn(
                "Cannot link asset to key [{}] because it is already taken.",
                customKeyString);
        }

        return id;
    }

    // Does not cache results.
    template <typename AssetT>
    std::optional<AssetT> loadAsset(std::filesystem::path path) {
        std::string pathString = path.string();

        auto it = m_Importers.find(std::type_index(typeid(AssetT)));
        if (it == m_Importers.cend()) {
            spdlog::error("Unable to find importer for [{}]!", pathString);
            return std::nullopt;
        }

        AssetImporter<AssetT> *importer =
            static_cast<AssetImporter<AssetT> *>(it->second.get());

        AssetT loadedAsset;
        if (!importer->load(path, loadedAsset, this)) {
            spdlog::error("Failed to load [{}]!", pathString);
            return std::nullopt;
        }

        return std::move(loadedAsset);
    }

    template <typename AssetT>
    std::optional<UUID> loadWithKey(std::filesystem::path path,
                                    std::string_view key) {
        std::string keyString(key);

        if (auto it = m_KeyToUUID.find(keyString); it != m_KeyToUUID.cend()) {
            UUID uuid = it->second;

            auto ownerIt = m_Owner.find(uuid);

            if (ownerIt == m_Owner.cend()) {
                m_UUIDToKey.erase(it->second);
                m_KeyToUUID.erase(it);
            } else if (ownerIt->second != std::type_index(typeid(AssetT))) {
                spdlog::error(
                    "[{}] has a UUID associated with it, but the actual asset "
                    "type differs from the intended type!",
                    keyString);
                return std::nullopt;
            } else {
                return uuid;
            }
        }

        std::optional<AssetT> asset = loadAsset<AssetT>(path);
        if (!asset.has_value())
            return std::nullopt;

        return add(std::move(asset.value()), key);
    }

    template <typename AssetT>
    std::optional<std::vector<UUID>> loadGroup(std::filesystem::path path) {
        std::string keyString(path.string());
        if (auto it = m_KeyToGroupUUID.find(keyString);
            it != m_KeyToGroupUUID.cend()) {
            const std::vector<UUID> uuids = it->second;

            auto ownerIt = m_Owner.cend();
            if (!uuids.empty()) {
                for (UUID id : uuids) {
                    ownerIt = m_Owner.find(id);
                    if (ownerIt == m_Owner.cend())
                        break;
                }
            }

            if (ownerIt == m_Owner.cend()) {
                m_KeyToGroupUUID.erase(it);
                for (UUID id : uuids) {
                    if (auto it = m_UUIDToKey.find(id);
                        it != m_UUIDToKey.cend()) {
                        m_KeyToUUID.erase(it->second);
                        m_UUIDToKey.erase(id);
                    }
                    m_UUIDToGroup.erase(id);
                }
            } else if (ownerIt->second != std::type_index(typeid(AssetT))) {
                spdlog::error(
                    "[{}] has a UUID associated with it, but the actual asset "
                    "type differs from the intended type!",
                    keyString);
                return std::nullopt;
            } else {
                return uuids;
            }
        }

        auto it = m_Importers.find(std::type_index(typeid(AssetT)));
        if (it == m_Importers.cend()) {
            spdlog::error("Unable to find importer for [{}]!", keyString);
            return std::nullopt;
        }

        AssetImporter<AssetT> *importer =
            static_cast<AssetImporter<AssetT> *>(it->second.get());

        std::vector<std::pair<std::string, AssetT>> loadedAssets;
        if (!importer->loadGroup(path, loadedAssets, this)) {
            spdlog::error("Failed to load [{}]!", keyString);
            return std::nullopt;
        }

        std::vector<UUID> uuids;
        for (auto &[name, asset] : loadedAssets) {
            UUID loaded = add(std::move(asset), name);
            uuids.emplace_back(loaded);
            m_UUIDToGroup[loaded] = keyString;
        }

        if (uuids.empty())
            return std::nullopt;

        m_KeyToGroupUUID[keyString] = uuids;

        return uuids;
    }

    template <typename AssetT>
    std::optional<UUID> load(std::filesystem::path path) {
        return loadWithKey<AssetT>(path, path.string());
    }

    AssetRef acquire(UUID id);

    // TODO: Return a handle to the cleanup function
    //       so observers can unregister.
    //
    // Only necessary for observers that can have a shorter lifespan
    // than the asset manager. The only observers so far would be any renderer,
    // and any renderer should live as long as the application.
    void registerCleanup(CleanupFn fn);

    // Notify observers (e.g. render backends) that asset UUID is no longer
    // used.
    void resolvePendingDeletions();

  private:
    friend class AssetRef;
    void addRef(UUID id);
    void removeRef(UUID id);

  private:
    template <typename PoolT> AssetPool<PoolT> &getPool() {
        auto poolType = std::type_index(typeid(PoolT));
        if (m_Pools.find(poolType) == m_Pools.cend()) {
            m_Pools.insert(
                std::make_pair(poolType, std::make_unique<AssetPool<PoolT>>()));
        }
        return static_cast<AssetPool<PoolT> &>(*m_Pools.at(poolType));
    }

    template <typename PoolT> const AssetPool<PoolT> *findPool() const {
        auto poolType = std::type_index(typeid(PoolT));
        if (m_Pools.find(poolType) == m_Pools.cend())
            return nullptr;
        return static_cast<const AssetPool<PoolT> *>(
            m_Pools.at(poolType).get());
    }

    template <typename PoolT> AssetPool<PoolT> *findPool() {
        auto poolType = std::type_index(typeid(PoolT));
        if (m_Pools.find(poolType) == m_Pools.cend())
            return nullptr;
        return static_cast<AssetPool<PoolT> *>(m_Pools.at(poolType).get());
    }

  private:
    std::unordered_map<UUID, std::type_index> m_Owner;
    std::unordered_map<UUID, size_t> m_AssetRefs;

    std::vector<UUID> m_PendingDelete;
    std::vector<CleanupFn> m_CleanupObservers;

    std::unordered_map<std::type_index, std::unique_ptr<IAssetPool>> m_Pools;

    std::unordered_map<std::type_index, std::unique_ptr<IAssetImporter>>
        m_Importers;

    std::unordered_map<std::string, std::vector<UUID>> m_KeyToGroupUUID;
    std::unordered_map<UUID, std::string> m_UUIDToGroup;

    std::unordered_map<std::string, UUID> m_KeyToUUID;
    std::unordered_map<UUID, std::string> m_UUIDToKey;
};
} // namespace SYN
