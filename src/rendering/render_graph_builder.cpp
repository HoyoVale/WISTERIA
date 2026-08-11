#include "wisteria/common/pch.hpp"

#include "wisteria/rendering/render_graph.hpp"

#include "wisteria/rendering/render_frame_packet.hpp"
#include "wisteria/rendering/environment.hpp"

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
        "shadowDepth",
        RenderResourceKind::Depth,
        RenderResourceLifetime::Transient
    );
    graph.AddResource(
        "sceneDepth",
        RenderResourceKind::Depth,
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

    if (hasShadow)
    {
        graph.AddPass(RenderPassDescriptor{
            RenderPassId::ShadowDepth, "shadow-depth", {},
        });
        graph.AddAccess(
            RenderPassId::ShadowDepth,
            "shadowDepth",
            RenderResourceAccess::Write
        );
    }

    if (hasGroundReceivers)
    {
        std::vector<RenderPassId> deps;
        addDependency(deps, hasShadow, RenderPassId::ShadowDepth);
        graph.AddPass(RenderPassDescriptor{
            RenderPassId::GroundReceivers,
            "ground-receivers",
            deps,
        });
        graph.AddAccess(
            RenderPassId::GroundReceivers,
            "sceneColor",
            RenderResourceAccess::Write
        );
    }

    if (hasMmdGroundShadow)
    {
        graph.AddPass(RenderPassDescriptor{
            RenderPassId::MmdGroundShadow,
            "mmd-ground-shadow",
            {RenderPassId::ShadowDepth, RenderPassId::GroundReceivers},
        });
        graph.AddAccess(
            RenderPassId::MmdGroundShadow,
            "sceneColor",
            RenderResourceAccess::Write
        );
    }

    if (hasOpaque)
    {
        std::vector<RenderPassId> deps;
        addDependency(deps, hasShadow, RenderPassId::ShadowDepth);
        addDependency(deps, hasMmdGroundShadow, RenderPassId::MmdGroundShadow);
        graph.AddPass(RenderPassDescriptor{
            RenderPassId::Opaque, "opaque", deps,
        });
        graph.AddAccess(
            RenderPassId::Opaque,
            "sceneColor",
            RenderResourceAccess::Write
        );
    }

    if (hasSkybox)
    {
        graph.AddPass(RenderPassDescriptor{
            RenderPassId::Skybox,
            "skybox",
            {RenderPassId::Opaque},
        });
        graph.AddAccess(
            RenderPassId::Skybox,
            "sceneColor",
            RenderResourceAccess::Write
        );
    }

    if (hasTransparent)
    {
        std::vector<RenderPassId> deps = {RenderPassId::Opaque};
        addDependency(deps, hasSkybox, RenderPassId::Skybox);
        graph.AddPass(RenderPassDescriptor{
            RenderPassId::Transparent, "transparent", deps,
        });
        if (hasOitComposite)
        {
            graph.AddAccess(
                RenderPassId::Transparent,
                "oitAccum",
                RenderResourceAccess::Write
            );
            graph.AddAccess(
                RenderPassId::Transparent,
                "oitReveal",
                RenderResourceAccess::Write
            );
        }
        else
        {
            graph.AddAccess(
                RenderPassId::Transparent,
                "sceneColor",
                RenderResourceAccess::Write
            );
        }
    }

    if (hasOitComposite)
    {
        graph.AddPass(RenderPassDescriptor{
            RenderPassId::OitComposite,
            "oit-composite",
            {RenderPassId::Transparent},
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
            RenderResourceAccess::Write
        );
    }

    if (hasPhysicsDebug)
    {
        std::vector<RenderPassId> deps = {RenderPassId::Opaque};
        addDependency(
            deps,
            hasOitComposite,
            RenderPassId::OitComposite
        );
        addDependency(deps, hasTransparent, RenderPassId::Transparent);
        graph.AddPass(RenderPassDescriptor{
            RenderPassId::PhysicsDebug, "physics-debug", deps,
        });
        graph.AddAccess(
            RenderPassId::PhysicsDebug,
            "sceneColor",
            RenderResourceAccess::Write
        );
    }

    return graph;
}
}  // namespace wisteria
