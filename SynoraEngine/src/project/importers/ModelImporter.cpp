#include <assimp/scene.h>

#include <SynoraEngine/project/importers/ModelImporter.h>

#include <spdlog/spdlog.h>

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>

#include <stb_image.h>

#include <SynoraEngine/project/AssetManager.h>
#include <SynoraEngine/project/assets/MaterialData.h>
#include <SynoraEngine/project/assets/TextureData.h>

namespace SYN {

namespace {
glm::mat4 toGlmMatrix(const aiMatrix4x4 &m) {
    return {m.a1, m.b1, m.c1, m.d1, m.a2, m.b2, m.c2, m.d2,
            m.a3, m.b3, m.c3, m.d3, m.a4, m.b4, m.c4, m.d4};
}
bool albedoNeedsAlphaMask(const TextureData *albedo) {
    uint32_t channels = albedo->channelCount;

    // Only RGBA has an alpha channel
    if (channels < 4) {
        return false;
    }

    constexpr uint8_t kCutoffAlpha = 128;
    const size_t pixelCount =
        static_cast<size_t>(albedo->width) * albedo->height;

    for (size_t i = 0; i < pixelCount; ++i) {
        if (albedo->data[i * channels + (channels - 1)] < kCutoffAlpha) {
            return true;
        }
    }
    return false;
}

std::string makeTextureCacheKey(const std::filesystem::path &modelPath,
                                const std::string &texturePath) {
    if (!texturePath.empty() && texturePath.front() == '*') {
        return modelPath.string() + "|" + texturePath;
    }

    std::filesystem::path resolvedPath =
        (modelPath.parent_path() / texturePath).lexically_normal();
    return resolvedPath.string();
}

uint8_t sampleChannelNearest(const TextureData *tex, uint32_t tx, uint32_t ty,
                             uint32_t tw, uint32_t th) {
    int channels = tex->channelCount;
    uint32_t sx = tw > 1 ? (tx * (tex->width - 1)) / (tw - 1) : 0;
    uint32_t sy = th > 1 ? (ty * (tex->height - 1)) / (th - 1) : 0;
    uint8_t ch = std::max(0, channels - 1);
    return tex
        ->data[(static_cast<size_t>(sy) * tex->width + sx) * channels + ch];
}

AssetRef loadEmbeddedCompressedTexture(const aiTexture *texture,
                                       const std::string &cacheKey,
                                       AssetManager *manager) {
    std::optional<UUID> uuid = manager->uuidFromKey(cacheKey);
    if (uuid.has_value())
        return manager->acquire(uuid.value());

    int width{};
    int height{};
    int channels{};
    stbi_uc *pixels = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc *>(texture->pcData),
        static_cast<int>(texture->mWidth), &width, &height, &channels, 0);
    if (pixels == nullptr) {
        spdlog::warn("Could not decode embedded texture {}", cacheKey);
        return {};
    }

    TextureData textureData;
    textureData.width = width;
    textureData.height = height;
    textureData.channelCount = channels;
    textureData.sourcePath = cacheKey;

    auto &storage = textureData.data;
    storage.assign(pixels, pixels + (width * height * channels));
    stbi_image_free(pixels);

    return manager->acquire(manager->add(textureData, cacheKey));
}

AssetRef loadEmbeddedTexture(const aiTexture *texture,
                             const std::string &cacheKey,
                             AssetManager *manager) {
    if (texture == nullptr) {
        return {};
    }

    if (texture->mHeight == 0) {
        return loadEmbeddedCompressedTexture(texture, cacheKey, manager);
    }

    std::optional<UUID> uuid = manager->uuidFromKey(cacheKey);
    if (uuid.has_value())
        return manager->acquire(uuid.value());

    TextureData textureData;
    textureData.width = texture->mWidth;
    textureData.height = texture->mHeight;
    textureData.channelCount = 4;
    textureData.sourcePath = cacheKey;

    auto &storage = textureData.data;
    storage.resize(texture->mWidth * texture->mHeight * 4);

    const aiTexel *pixels = reinterpret_cast<const aiTexel *>(texture->pcData);
    for (uint32_t i = 0; i < texture->mWidth * texture->mHeight; ++i) {
        storage[i * 4 + 0] = pixels[i].r;
        storage[i * 4 + 1] = pixels[i].g;
        storage[i * 4 + 2] = pixels[i].b;
        storage[i * 4 + 3] = pixels[i].a;
    }

    return manager->acquire(manager->add(textureData, cacheKey));
}

AssetRef packMetallicRoughness(const TextureData *metalTex,
                               const TextureData *roughTex,
                               AssetManager *manager) {
    if (!metalTex && !roughTex) {
        return {};
    }

    std::string cacheKey =
        "packedMR|" + (metalTex ? metalTex->sourcePath : std::string("none")) +
        "|" + (roughTex ? roughTex->sourcePath : std::string("none"));

    std::optional<UUID> uuid = manager->uuidFromKey(cacheKey);
    if (uuid.has_value())
        return manager->acquire(uuid.value());

    uint32_t width = std::max(metalTex ? metalTex->width : 1u,
                              roughTex ? roughTex->width : 1u);
    uint32_t height = std::max(metalTex ? metalTex->height : 1u,
                               roughTex ? roughTex->height : 1u);

    TextureData metallicRoughnessTex = {
        .width = width, .height = height, .sourcePath = cacheKey};
    metallicRoughnessTex.channelCount = 4;

    auto &storage = metallicRoughnessTex.data;
    storage.resize(static_cast<size_t>(width) * height * 4);

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint8_t roughness =
                roughTex ? sampleChannelNearest(roughTex, x, y, width, height)
                         : 255;
            uint8_t metallic =
                metalTex ? sampleChannelNearest(metalTex, x, y, width, height)
                         : 255;
            size_t dst = (static_cast<size_t>(y) * width + x) * 4;
            storage[dst + 0] = 0;
            storage[dst + 1] = roughness;
            storage[dst + 2] = metallic;
            storage[dst + 3] = 255;
        }
    }

    return manager->acquire(
        manager->add<TextureData>(metallicRoughnessTex, cacheKey));
}

AssetRef loadExternalTexture(const std::filesystem::path &texturePath,
                             const std::string &cacheKey,
                             AssetManager *manager) {
    std::optional<UUID> uuid =
        manager->loadWithKey<TextureData>(texturePath, cacheKey);
    if (!uuid.has_value()) {
        return {};
    }
    return manager->acquire(uuid.value());
}

void addBoneData(Vertex &vertex, int boneIndex, float weight) {
    for (int i = 0; i < 4; ++i) {
        if (vertex.boneWeights[i] == 0.0f) {
            vertex.boneIndices[i] = boneIndex;
            vertex.boneWeights[i] = weight;
            return;
        }
    }

    int smallestWeightIndex = 0;
    for (int i = 1; i < 4; ++i) {
        if (vertex.boneWeights[i] < vertex.boneWeights[smallestWeightIndex]) {
            smallestWeightIndex = i;
        }
    }

    if (weight > vertex.boneWeights[smallestWeightIndex]) {
        vertex.boneIndices[smallestWeightIndex] = boneIndex;
        vertex.boneWeights[smallestWeightIndex] = weight;
    }
}

void normalizeBoneWeights(Vertex &vertex) {
    float totalWeight = vertex.boneWeights.x + vertex.boneWeights.y +
                        vertex.boneWeights.z + vertex.boneWeights.w;
    if (totalWeight <= 0.0f) {
        return;
    }

    vertex.boneWeights /= totalWeight;
}

} // namespace

bool ModelImporter::load(std::filesystem::path filepath, ModelData &asset,
                         class AssetManager *assetManager) {
    if (assetManager == nullptr) {
        spdlog::error("Asset manager is NULL in ModelImporter");
        return false;
    }

    Assimp::Importer importer;
    m_Scene = importer.ReadFile(
        filepath.string(),
        aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
            aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace |
            aiProcess_FlipUVs | aiProcess_LimitBoneWeights |
            aiProcess_GenBoundingBoxes);

    if (m_Scene == nullptr || m_Scene->mRootNode == nullptr) {
        spdlog::error("Could not load [{}]: {}", filepath.string(),
                      importer.GetErrorString());
        return false;
    }

    m_AssetManager = assetManager;

    asset.skeleton.inverseRoot =
        glm::inverse(toGlmMatrix(m_Scene->mRootNode->mTransformation));
    processNode(m_Scene->mRootNode, -1, glm::mat4(1.0f), asset.skeleton);

    for (uint32_t i = 0; i < m_Scene->mNumMeshes; ++i) {
        const aiMesh *mesh = m_Scene->mMeshes[i];
        glm::mat4 globalTransform = m_MeshGlobalTransform.at(i);
        asset.meshes.emplace_back(
            processMesh(mesh, globalTransform, filepath, asset.skeleton));
    }

    m_Scene = nullptr;
    m_AssetManager = nullptr;

    m_BoneIndexMap.clear();
    m_NodeNameToIndex.clear();
    m_MeshGlobalTransform.clear();
    m_NextBoneIndex = 0;

    return true;
}

void ModelImporter::processNode(const aiNode *current, int32_t parentIndex,
                                const glm::mat4 &parentTransform,
                                Skeleton &skeleton) {
    glm::mat4 globalTransform =
        parentTransform * toGlmMatrix(current->mTransformation);

    for (uint32_t i = 0; i < current->mNumMeshes; ++i) {
        m_MeshGlobalTransform[current->mMeshes[i]] = globalTransform;
    }

    aiVector3D position, scale;
    aiQuaternion rotation;
    current->mTransformation.Decompose(scale, rotation, position);

    skeleton.nodes.emplace_back(
        current->mName.C_Str(),
        parentIndex < 0 ? std::nullopt : std::optional<uint32_t>{parentIndex},
        glm::vec3(position.x, position.y, position.z),
        glm::quat(rotation.w, rotation.x, rotation.y, rotation.z),
        glm::vec3(scale.x, scale.y, scale.z));

    m_NodeNameToIndex[current->mName.C_Str()] = skeleton.nodes.size() - 1;
    parentIndex = skeleton.nodes.size() - 1;

    for (unsigned int i = 0; i < current->mNumChildren; ++i) {
        processNode(current->mChildren[i], parentIndex, globalTransform,
                    skeleton);
    }
}

AssetRef ModelImporter::loadTextureForMaterial(std::filesystem::path path,
                                               const class aiMaterial *material,
                                               uint32_t type) {
    aiTextureType textureType = (aiTextureType)type;
    aiString relPath{};
    if (material->GetTexture(textureType, 0, &relPath) != AI_SUCCESS) {
        return {};
    }

    std::string texturePath = relPath.C_Str();
    std::string cacheKey = makeTextureCacheKey(path, texturePath);

    bool isTextureAlbedo =
        type == aiTextureType_BASE_COLOR || type == aiTextureType_DIFFUSE;

    if (!texturePath.empty() && texturePath.front() == '*') {
        const aiTexture *embeddedTexture =
            m_Scene->GetEmbeddedTexture(texturePath.c_str());
        return loadEmbeddedTexture(embeddedTexture, cacheKey, m_AssetManager);
    }

    std::filesystem::path resolvedPath =
        (path.parent_path() / texturePath).lexically_normal();
    return loadExternalTexture(resolvedPath, cacheKey, m_AssetManager);
}

AssetRef ModelImporter::processMaterial(const aiMaterial *material,
                                        const std::filesystem::path &path,
                                        uint32_t materialIndex) {
    std::string name;

    aiString aiMatName;
    if (material->Get(AI_MATKEY_NAME, aiMatName) != AI_SUCCESS) {
        name = path.stem().string() + std::format(".Default{}", materialIndex);
    } else {
        name = path.stem().string() + "." + aiMatName.C_Str();
    }

    std::optional<UUID> uuid = m_AssetManager->uuidFromKey(name);
    if (uuid.has_value()) {
        return m_AssetManager->acquire(uuid.value());
    }

    MaterialData result{};
    result.name = name;

    aiColor4D color{};
    if (material->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
        result.tint = {color.r, color.g, color.b, color.a};
    }

    float metallic{};
    if (material->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS) {
        result.metallic = metallic;
    }

    float roughness{};
    if (material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS) {
        result.roughness = roughness;
    }

    result.albedoData =
        loadTextureForMaterial(path, material, aiTextureType_BASE_COLOR);
    if (!result.albedoData.valid()) {
        result.albedoData =
            loadTextureForMaterial(path, material, aiTextureType_DIFFUSE);
    }

    float alphaCutoff{};
    if (result.albedoData.valid()) {
        const TextureData *albedoData =
            m_AssetManager->get<TextureData>(result.albedoData.uuid());
        result.alphaCutoff = albedoNeedsAlphaMask(albedoData) ? 0.5f : 1.0f;
    }
    if (material->Get("$mat.gltf.alphaCutoff", 0, 0, alphaCutoff) ==
            AI_SUCCESS &&
        (result.alphaCutoff < 1.0f)) {
        result.alphaCutoff = alphaCutoff;
    }

    result.normalData =
        loadTextureForMaterial(path, material, aiTextureType_NORMALS);
    if (!result.normalData.valid()) {
        result.normalData =
            loadTextureForMaterial(path, material, aiTextureType_HEIGHT);
    }

    // Metallic/roughness can be packed into one image or delivered as two
    // separate grayscale maps. Load both assimp slots and normalize to a single
    // packed texture so the renderer and shader only ever handle one
    // metallicRoughnessMap.
    AssetRef metalDataRef =
        loadTextureForMaterial(path, material, aiTextureType_METALNESS);
    AssetRef roughDataRef =
        loadTextureForMaterial(path, material, aiTextureType_DIFFUSE_ROUGHNESS);

    const TextureData *metalData =
        m_AssetManager->get<TextureData>(metalDataRef.uuid());

    const TextureData *roughData =
        m_AssetManager->get<TextureData>(roughDataRef.uuid());

    bool sameImage = metalDataRef.valid() && roughDataRef.valid() &&
                     metalData->sourcePath == roughData->sourcePath;
    bool metalLooksPacked = metalData && metalData->channelCount >= 3;
    bool roughLooksPacked = roughData && roughData->channelCount >= 3;

    if (sameImage) {
        result.metallicRoughnessData = metalDataRef;
    } else if (metalData && !roughData && metalLooksPacked) {
        result.metallicRoughnessData = metalDataRef;
    } else if (roughData && !metalData && roughLooksPacked) {
        result.metallicRoughnessData = roughDataRef;
    } else {
        result.metallicRoughnessData =
            packMetallicRoughness(metalData, roughData, m_AssetManager);
    }

    return m_AssetManager->acquire(
        m_AssetManager->add<MaterialData>(result, name));
}

void ModelImporter::applyBonesToMesh(MeshData &meshData,
                                     const class aiMesh *mesh,
                                     Skeleton &skeleton) {
    if (!mesh->HasBones()) {
        meshData.hasSkin = false;
        return;
    }

    meshData.hasSkin = true;

    for (unsigned int boneIdx = 0; boneIdx < mesh->mNumBones; ++boneIdx) {
        const aiBone *bone = mesh->mBones[boneIdx];
        const std::string boneName = bone->mName.C_Str();
        uint32_t mappedBoneIndex{};
        auto [it, inserted] = m_BoneIndexMap.emplace(boneName, m_NextBoneIndex);
        if (inserted) {
            mappedBoneIndex = m_NextBoneIndex;
            skeleton.boneInfo.emplace_back(m_NodeNameToIndex.at(boneName),
                                           toGlmMatrix(bone->mOffsetMatrix));
            ++m_NextBoneIndex;
        } else {
            mappedBoneIndex = it->second;
        }

        for (unsigned int weightIdx = 0; weightIdx < bone->mNumWeights;
             ++weightIdx) {
            const aiVertexWeight &weight = bone->mWeights[weightIdx];
            if (weight.mVertexId >= meshData.vertices.size()) {
                continue;
            }
            addBoneData(meshData.vertices[weight.mVertexId], mappedBoneIndex,
                        weight.mWeight);
        }
    }

    for (Vertex &vertex : meshData.vertices) {
        normalizeBoneWeights(vertex);
    }
}

MeshData ModelImporter::processMesh(const aiMesh *mesh,
                                    const glm::mat4 &localTransform,
                                    const std::filesystem::path &path,
                                    Skeleton &skeleton) {
    MeshData result{};
    result.localTransform = localTransform;

    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        Vertex vertex{};
        vertex.position = {mesh->mVertices[i].x, mesh->mVertices[i].y,
                           mesh->mVertices[i].z};

        if (mesh->HasNormals()) {
            vertex.normal = {mesh->mNormals[i].x, mesh->mNormals[i].y,
                             mesh->mNormals[i].z};
        }

        if (mesh->HasTextureCoords(0)) {
            vertex.uv = {mesh->mTextureCoords[0][i].x,
                         mesh->mTextureCoords[0][i].y};
        }

        if (mesh->HasTangentsAndBitangents()) {
            glm::vec3 bitangent = {mesh->mBitangents[i].x,
                                   mesh->mBitangents[i].y,
                                   mesh->mBitangents[i].z};
            glm::vec3 tangent = {mesh->mTangents[i].x, mesh->mTangents[i].y,
                                 mesh->mTangents[i].z};
            glm::vec3 computedBitangent = glm::cross(vertex.normal, tangent);
            float handedness =
                glm::dot(bitangent, computedBitangent) < 0.0f ? -1.0f : 1.0f;
            vertex.tangent = glm::vec4(tangent, handedness);
        }

        result.vertices.emplace_back(vertex);
    }

    aiAABB aabb = mesh->mAABB;

    glm::vec3 min = glm::vec3(aabb.mMin.x, aabb.mMin.y, aabb.mMin.z);
    glm::vec3 max = glm::vec3(aabb.mMax.x, aabb.mMax.y, aabb.mMax.z);

    result.aabb = {min, max};

    for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
        const aiFace &face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; ++j) {
            result.indices.emplace_back(face.mIndices[j]);
        }
    }

    if (mesh->mMaterialIndex < m_Scene->mNumMaterials) {
        result.material =
            processMaterial(m_Scene->mMaterials[mesh->mMaterialIndex], path,
                            mesh->mMaterialIndex);
    }

    applyBonesToMesh(result, mesh, skeleton);
    return result;
}
} // namespace SYN
