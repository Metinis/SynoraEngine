#include <SynoraEngine/scene/Scene.h>
#include <SynoraEngine/scene/components/Components.h>
#include <SynoraEngine/scene/view/RenderView3D.h>

namespace SYN {
RenderView3D RenderView3D::fromScene(Scene *scene) {
    RenderView3D renderView;

    scene->forEach<ModelComponent, BoundsComponent, TransformComponent>(
        [&](Entity entity, ModelComponent &model, BoundsComponent &bounds,
            TransformComponent &transform) {
            renderView.models.push_back(model.model.uuid());
            renderView.bounds.emplace_back(bounds.meshBounds);

            uint32_t modelIndex = renderView.models.size() - 1;

            glm::mat4 worldTransform = scene->getWorldTransformOf(entity);
            renderView.transforms.push_back(worldTransform);
            if (entity.hasComponent<MaterialComponent>()) {
                const MaterialComponent &material =
                    entity.getComponent<MaterialComponent>();
                for (const MaterialComponent::Submesh &submesh :
                     material.submeshes) {
                    renderView.materials.emplace_back(
                        modelIndex, submesh.meshIndex, submesh.material.uuid());
                }
            }
            if (entity.hasComponent<SkeletalAnimationComponent>()) {
                const SkeletalAnimationComponent &animation =
                    entity.getComponent<SkeletalAnimationComponent>();
                renderView.animations.emplace_back(
                    modelIndex, animation.player.getOutput());
            }
        });

    return renderView;
}
} // namespace SYN
