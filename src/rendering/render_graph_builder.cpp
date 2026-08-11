#include "wisteria/common/pch.hpp"

#include "wisteria/rendering/render_graph.hpp"

#include "wisteria/rendering/render_frame_packet.hpp"
#include "wisteria/rendering/environment.hpp"

#include <optional>
#include <vector>

namespace wisteria
{
RenderGraph BuildCurrentRenderGraph(
    const RenderFramePacket& packet,
    const RenderGraphBuildOptions& options
)
{
    RenderGraph graph;
    graph.AddResource(
        "sceneDepth",
        // SceneFramebuffer uses a combined GL_DEPTH24_STENCIL8 attachment;
        // MMD ground shadow writes the stencil aspect.
        RenderResourceKind::DepthStencil,
        RenderResourceLifetime::External
    );
    graph.AddResource(
        "sceneColor",
        RenderResourceKind::Color,
        RenderResourceLifetime::External
    );

    const bool hasShadow =
        options.shadowsEnabled &&
        !packet.directionalLights.empty() &&
        !packet.opaqueDraws.empty();
    const bool hasGroundReceivers = !packet.opaqueDraws.empty();
    const bool hasMmdGroundShadow =
        hasShadow && options.groundShadowEnabled;
    const bool hasOpaque = !packet.opaqueDraws.empty();
    const bool hasSkybox =
        options.skyboxEnabled &&
        packet.environment != nullptr &&
        packet.environment->ShouldDrawSkybox();
    const bool hasTransparent = !packet.transparentDraws.empty();
    const bool hasOitComposite = hasTransparent && options.oitEnabled;
    const bool hasPhysicsDebug = !packet.debugLines.empty();

    if (hasShadow)
    {
        graph.AddResource(
            "shadowDepth",
            RenderResourceKind::Depth,
            RenderResourceLifetime::Transient
        );
    }
    if (hasOitComposite)
    {
        graph.AddResource(
            "oitAccum",
            RenderResourceKind::Color,
            RenderResourceLifetime::Transient
        );
        graph.AddResource(
            "oitReveal",
            RenderResourceKind::Color,
            RenderResourceLifetime::Transient
        );
    }

    const auto addDependency = [](std::vector<RenderPassId>& deps,
                                  bool enabled,
                                  RenderPassId id)
    {
        if (enabled)
            deps.push_back(id);
    };
    // External SceneColor/DepthStencil attachments are pre-cleared by the
    // caller and partially rewritten by passes; every partial write must
    // declare Load + Store so a future backend does not discard existing
    // attachment content.
    const auto addExternalWrite = [&graph](
        RenderPassId pass,
        std::string resource,
        RenderResourceAspect aspect)
    {
        RenderAccessDesc desc;
        desc.resource = std::move(resource);
        desc.access = RenderResourceAccess::Write;
        desc.aspect = aspect;
        desc.loadOp = RenderLoadOp::Load;
        desc.storeOp = RenderStoreOp::Store;
        graph.AddAccess(pass, desc);
    };
    // Main scene chain: each pass depends on the previous pass that
    // actually exists this frame (sparse frames never dangle).
    std::optional<RenderPassId> lastScenePass;

    if (hasShadow)
    {
        graph.AddPass(RenderPassDescriptor{
            RenderPassId::ShadowDepth, "shadow-depth", {},
        });
        RenderAccessDesc shadowWrite;
        shadowWrite.resource = "shadowDepth";
        shadowWrite.access = RenderResourceAccess::Write;
        shadowWrite.aspect = RenderResourceAspect::Depth;
        // The shadow pass clears each cascade layer before rendering.
        shadowWrite.loadOp = RenderLoadOp::Clear;
        shadowWrite.storeOp = RenderStoreOp::Store;
        shadowWrite.clearValue = {1.0f, 0.0f, 0.0f, 0.0f};
        graph.AddAccess(RenderPassId::ShadowDepth, shadowWrite);
        lastScenePass = RenderPassId::ShadowDepth;
    }

    if (hasGroundReceivers)
    {
        std::vector<RenderPassId> deps;
        if (lastScenePass.has_value())
            deps.push_back(*lastScenePass);
        graph.AddPass(RenderPassDescriptor{
            RenderPassId::GroundReceivers,
            "ground-receivers",
            deps,
        });
        graph.AddAccess(
            RenderPassId::GroundReceivers,
            "sceneDepth",
            RenderResourceAccess::Read
        );
        addExternalWrite(
            RenderPassId::GroundReceivers,
            "sceneDepth",
            RenderResourceAspect::Depth
        );
        addExternalWrite(
            RenderPassId::GroundReceivers,
            "sceneColor",
            RenderResourceAspect::Color
        );
        if (hasShadow)
        {
            graph.AddAccess(
                RenderPassId::GroundReceivers,
                "shadowDepth",
                RenderResourceAccess::Read
            );
        }
        lastScenePass = RenderPassId::GroundReceivers;
    }

    if (hasMmdGroundShadow)
    {
        std::vector<RenderPassId> deps;
        addDependency(deps, hasShadow, RenderPassId::ShadowDepth);
        addDependency(
            deps,
            hasGroundReceivers,
            RenderPassId::GroundReceivers
        );
        graph.AddPass(RenderPassDescriptor{
            RenderPassId::MmdGroundShadow,
            "mmd-ground-shadow",
            deps,
        });
        graph.AddAccess(
            RenderPassId::MmdGroundShadow,
            "sceneDepth",
            RenderResourceAccess::Read
        );
        // Mask pass writes the stencil silhouette; fill pass reads it.
        RenderAccessDesc stencilWrite;
        stencilWrite.resource = "sceneDepth";
        stencilWrite.access = RenderResourceAccess::Write;
        stencilWrite.aspect = RenderResourceAspect::Stencil;
        stencilWrite.loadOp = RenderLoadOp::Load;
        stencilWrite.storeOp = RenderStoreOp::Store;
        graph.AddAccess(RenderPassId::MmdGroundShadow, stencilWrite);
        RenderAccessDesc stencilRead;
        stencilRead.resource = "sceneDepth";
        stencilRead.access = RenderResourceAccess::Read;
        stencilRead.aspect = RenderResourceAspect::Stencil;
        graph.AddAccess(RenderPassId::MmdGroundShadow, stencilRead);
        graph.AddAccess(
            RenderPassId::MmdGroundShadow,
            "sceneColor",
            RenderResourceAccess::Read
        );
        addExternalWrite(
            RenderPassId::MmdGroundShadow,
            "sceneColor",
            RenderResourceAspect::Color
        );
        lastScenePass = RenderPassId::MmdGroundShadow;
    }

    if (hasOpaque)
    {
        std::vector<RenderPassId> deps;
        if (lastScenePass.has_value())
            deps.push_back(*lastScenePass);
        graph.AddPass(RenderPassDescriptor{
            RenderPassId::Opaque, "opaque", deps,
        });
        if (hasShadow)
        {
            graph.AddAccess(
                RenderPassId::Opaque,
                "shadowDepth",
                RenderResourceAccess::Read
            );
        }
        graph.AddAccess(
            RenderPassId::Opaque,
            "sceneDepth",
            RenderResourceAccess::Read
        );
        addExternalWrite(
            RenderPassId::Opaque,
            "sceneDepth",
            RenderResourceAspect::Depth
        );
        addExternalWrite(
            RenderPassId::Opaque,
            "sceneColor",
            RenderResourceAspect::Color
        );
        lastScenePass = RenderPassId::Opaque;
    }

    if (hasSkybox)
    {
        std::vector<RenderPassId> deps;
        if (lastScenePass.has_value())
            deps.push_back(*lastScenePass);
        graph.AddPass(RenderPassDescriptor{
            RenderPassId::Skybox,
            "skybox",
            deps,
        });
        graph.AddAccess(
            RenderPassId::Skybox,
            "sceneDepth",
            RenderResourceAccess::Read
        );
        addExternalWrite(
            RenderPassId::Skybox,
            "sceneColor",
            RenderResourceAspect::Color
        );
        lastScenePass = RenderPassId::Skybox;
    }

    if (hasTransparent)
    {
        std::vector<RenderPassId> deps;
        if (lastScenePass.has_value())
            deps.push_back(*lastScenePass);
        graph.AddPass(RenderPassDescriptor{
            RenderPassId::Transparent, "transparent", deps,
        });
        graph.AddAccess(
            RenderPassId::Transparent,
            "sceneDepth",
            RenderResourceAccess::Read
        );
        if (hasShadow)
        {
            graph.AddAccess(
                RenderPassId::Transparent,
                "shadowDepth",
                RenderResourceAccess::Read
            );
        }
        if (hasOitComposite)
        {
            RenderAccessDesc accumWrite;
            accumWrite.resource = "oitAccum";
            accumWrite.access = RenderResourceAccess::Write;
            accumWrite.aspect = RenderResourceAspect::Color;
            accumWrite.loadOp = RenderLoadOp::Clear;
            accumWrite.storeOp = RenderStoreOp::Store;
            accumWrite.clearValue = {0.0f, 0.0f, 0.0f, 0.0f};
            graph.AddAccess(RenderPassId::Transparent, accumWrite);

            RenderAccessDesc revealWrite;
            revealWrite.resource = "oitReveal";
            revealWrite.access = RenderResourceAccess::Write;
            revealWrite.aspect = RenderResourceAspect::Color;
            revealWrite.loadOp = RenderLoadOp::Clear;
            revealWrite.storeOp = RenderStoreOp::Store;
            revealWrite.clearValue = {1.0f, 1.0f, 1.0f, 1.0f};
            graph.AddAccess(RenderPassId::Transparent, revealWrite);
        }
        else
        {
            graph.AddAccess(
                RenderPassId::Transparent,
                "sceneColor",
                RenderResourceAccess::Read
            );
            addExternalWrite(
                RenderPassId::Transparent,
                "sceneColor",
                RenderResourceAspect::Color
            );
        }
        lastScenePass = RenderPassId::Transparent;
    }

    if (hasOitComposite)
    {
        std::vector<RenderPassId> deps;
        if (lastScenePass.has_value())
            deps.push_back(*lastScenePass);
        graph.AddPass(RenderPassDescriptor{
            RenderPassId::OitComposite,
            "oit-composite",
            deps,
        });
        graph.AddAccess(
            RenderPassId::OitComposite,
            "oitAccum",
            RenderResourceAccess::Read
        );
        graph.AddAccess(
            RenderPassId::OitComposite,
            "oitReveal",
            RenderResourceAccess::Read
        );
        graph.AddAccess(
            RenderPassId::OitComposite,
            "sceneColor",
            RenderResourceAccess::Read
        );
        addExternalWrite(
            RenderPassId::OitComposite,
            "sceneColor",
            RenderResourceAspect::Color
        );
        lastScenePass = RenderPassId::OitComposite;
    }

    if (hasPhysicsDebug)
    {
        std::vector<RenderPassId> deps;
        if (lastScenePass.has_value())
            deps.push_back(*lastScenePass);
        graph.AddPass(RenderPassDescriptor{
            RenderPassId::PhysicsDebug, "physics-debug", deps,
        });
        graph.AddAccess(
            RenderPassId::PhysicsDebug,
            "sceneDepth",
            RenderResourceAccess::Read
        );
        addExternalWrite(
            RenderPassId::PhysicsDebug,
            "sceneColor",
            RenderResourceAspect::Color
        );
    }

    return graph;
}
}  // namespace wisteria
