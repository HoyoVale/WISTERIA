#include "wisteria/common/pch.hpp"
#include "wisteria/runtime/model_instance.hpp"
#include "wisteria/runtime/mmd_runtime_model.hpp"
#include "wisteria/rendering/mesh.hpp"

#include <stdexcept>
#include <unordered_set>

namespace wisteria
{
ModelInstance::ModelInstance(
    const ModelAsset& asset,
    std::unique_ptr<IModelRuntimeDriver> runtime
)
    : asset(&asset), runtime(std::move(runtime))
{
    if (this->runtime == nullptr ||
        !this->runtime->NeedsDynamicVertexUpload())
    {
        return;
    }

    std::unordered_set<const Mesh*> visited;
    for (const RenderPart& part : asset.Parts())
    {
        const Mesh* source = &part.GetMesh();
        if (!visited.emplace(source).second)
            continue;
        std::unique_ptr<Mesh> clone = source->CloneForInstance();
        Mesh* clonePointer = clone.get();
        clonePointer->SetDynamicVertexProvider(
            [this](Mesh& mesh)
            {
                this->UploadDynamicVertices(mesh);
            }
        );
        this->meshMap.emplace(source, clonePointer);
        this->instanceMeshes.emplace_back(std::move(clone));
    }
}

const ModelAsset& ModelInstance::Asset() const noexcept
{
    return *this->asset;
}

bool ModelInstance::HasRuntime() const noexcept
{
    return this->runtime != nullptr;
}

IModelRuntimeDriver* ModelInstance::TryGetRuntime() noexcept
{
    return this->runtime.get();
}

const IModelRuntimeDriver* ModelInstance::TryGetRuntime() const noexcept
{
    return this->runtime.get();
}

MmdRuntimeModel* ModelInstance::TryGetMmdRuntime() noexcept
{
    return dynamic_cast<MmdRuntimeModel*>(this->runtime.get());
}

const MmdRuntimeModel* ModelInstance::TryGetMmdRuntime() const noexcept
{
    return dynamic_cast<const MmdRuntimeModel*>(this->runtime.get());
}

Mesh& ModelInstance::ResolveMesh(const Mesh& assetMesh)
{
    const auto iterator = this->meshMap.find(&assetMesh);
    return iterator == this->meshMap.end()
        ? const_cast<Mesh&>(assetMesh)
        : *iterator->second;
}

const Mesh& ModelInstance::ResolveMesh(const Mesh& assetMesh) const
{
    const auto iterator = this->meshMap.find(&assetMesh);
    return iterator == this->meshMap.end() ? assetMesh : *iterator->second;
}

std::size_t ModelInstance::InstanceMeshCount() const noexcept
{
    return this->instanceMeshes.size();
}

RootMotionDelta ModelInstance::Update(float deltaTime)
{
    if (this->runtime != nullptr)
    {
        this->runtime->Update(deltaTime);
        ++this->updateSerial;
        this->lastView = this->runtime->ProduceFrameView();
        this->lastView.updateSerial = this->updateSerial;
        this->snapshot.metadata.updateSerial = this->updateSerial;
        this->snapshot.metadata.motionFrame =
            this->runtime->MotionFrame();
        this->snapshot.metadata.motionPaused =
            this->runtime->IsMotionPaused();
        this->snapshot.metadata.motionLooping =
            this->runtime->IsMotionLooping();
        this->snapshot.metadata.valid = true;
        this->frameValid = true;
        return this->runtime->ConsumeRootMotion();
    }
    return {};
}

void ModelInstance::Reset()
{
    if (this->runtime != nullptr)
        this->runtime->Reset();
    // The transient view is invalid after Reset; persist nothing that could
    // be mistaken for current state.
    this->lastView = {};
    this->snapshot.metadata.valid = false;
    this->snapshot.metadata.updateSerial = this->updateSerial;
    this->frameValid = false;
    // Invalidate per-channel capture state so a future Runtime that restarts
    // its revision counters cannot reuse Reset-before data. Vectors are left
    // in place; the next valid frame forces a re-capture.
    this->snapshot.pose.captured = false;
    this->snapshot.morphs.captured = false;
    this->snapshot.geometry.captured = false;
}

void ModelInstance::UploadDynamicVertices(Mesh& mesh)
{
    if (this->runtime == nullptr)
        return;
    const ModelVertexFrame frame = this->lastView.geometry;
    // Frozen geometry semantics (R1.5 §5.4): both spans empty means no
    // runtime-owned deformed geometry; one empty span is an invalid frame
    // and must be rejected exactly like CaptureGeometry rejects it.
    if (frame.positions.empty() && frame.normals.empty())
        return;
    if (frame.positions.size() != frame.normals.size())
        throw std::logic_error("Runtime vertex frame is inconsistent");

    const std::span<const std::uint32_t> sourceIndices =
        mesh.SourceVertexIndices();
    if (sourceIndices.empty())
    {
        mesh.UploadDynamicVertices(frame.positions, frame.normals);
        return;
    }

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    positions.reserve(sourceIndices.size());
    normals.reserve(sourceIndices.size());
    for (std::uint32_t index : sourceIndices)
    {
        if (index >= frame.positions.size())
        {
            throw std::out_of_range(
                "Instance mesh references an invalid runtime vertex"
            );
        }
        positions.push_back(frame.positions[index]);
        normals.push_back(frame.normals[index]);
    }
    mesh.UploadDynamicVertices(positions, normals);
}

const ModelFrameView& ModelInstance::LastFrameView() const noexcept
{
    return this->lastView;
}

const ModelFrameSnapshot& ModelInstance::CaptureSnapshot(
    CaptureMask mask
)
{
    this->snapshot.metadata.updateSerial = this->updateSerial;
    // A snapshot is only valid for a frame that was actually updated. After
    // Reset (or before the first Update) no channel may be captured: the
    // transient view is empty and writing it would persist stale/empty state.
    this->snapshot.metadata.valid = this->frameValid;
    if (!this->frameValid)
        return this->snapshot;

    bool anyChanged = false;
    if (HasFlag(mask, CaptureMask::Pose))
        anyChanged = this->CapturePose() || anyChanged;
    if (HasFlag(mask, CaptureMask::Morphs))
        anyChanged = this->CaptureMorphs() || anyChanged;
    if (HasFlag(mask, CaptureMask::Geometry))
        anyChanged = this->CaptureGeometry() || anyChanged;

    // snapshotRevision is the ModelInstance's own monotonic sequence, not a
    // comparison across independent channel counters. It advances whenever
    // any requested channel actually changed.
    if (anyChanged)
        ++this->snapshot.metadata.snapshotRevision;
    return this->snapshot;
}

const ModelFrameSnapshot& ModelInstance::LastSnapshot() const noexcept
{
    return this->snapshot;
}

bool ModelInstance::CapturePose()
{
    if (this->runtime == nullptr)
        return false;
    Pose* posePointer = this->runtime->TryGetPose();
    if (posePointer == nullptr)
    {
        // Optional channel: a runtime without a Pose reports the channel as
        // absent instead of fabricating an empty snapshot.
        this->snapshot.pose.captured = false;
        return false;
    }
    const Pose& pose = *posePointer;
    const std::uint64_t poseRevision = pose.Revision();
    if (poseRevision != this->snapshot.pose.poseRevision ||
        !this->snapshot.pose.captured)
    {
        const std::span<const glm::mat4> local = pose.LocalMatrices();
        const std::span<const glm::mat4> global = pose.GlobalMatrices();
        const std::span<const glm::mat4> skinning = pose.SkinningMatrices();
        this->snapshot.pose.localTransforms.assign(
            local.begin(),
            local.end()
        );
        this->snapshot.pose.globalTransforms.assign(
            global.begin(),
            global.end()
        );
        this->snapshot.pose.skinningTransforms.assign(
            skinning.begin(),
            skinning.end()
        );
        this->snapshot.pose.poseRevision = poseRevision;
        this->snapshot.pose.captured = true;
        return true;
    }
    return false;
}

bool ModelInstance::CaptureMorphs()
{
    if (this->runtime == nullptr)
        return false;
    if (this->runtime->MorphCount() == 0U)
    {
        this->snapshot.morphs.entries.clear();
        this->snapshot.morphs.captured = false;
        return false;
    }
    // Raw weights come from the runtime; WISTERIA never synthesizes
    // effective weights for backend-driven models.
    const std::uint64_t morphRevision = this->runtime->MorphRevision();
    if (morphRevision != this->snapshot.morphs.morphRevision ||
        !this->snapshot.morphs.captured)
    {
        this->snapshot.morphs.entries.clear();
        const std::size_t count = this->runtime->MorphCount();
        this->snapshot.morphs.entries.reserve(count);
        for (std::size_t index = 0U; index < count; ++index)
        {
            MorphDescriptor descriptor;
            if (!this->runtime->DescribeMorph(index, descriptor))
                continue;
            MorphRuntimeState state;
            if (!this->runtime->ReadMorphState(index, state))
                continue;
            this->snapshot.morphs.entries.push_back({
                std::move(descriptor.name),
                descriptor.kind,
                state.rawWeight,
                state.effectiveWeight
            });
        }
        this->snapshot.morphs.morphRevision = morphRevision;
        this->snapshot.morphs.captured = true;
        return true;
    }
    return false;
}

bool ModelInstance::CaptureGeometry()
{
    if (this->runtime == nullptr)
        return false;
    const ModelVertexFrame frame = this->lastView.geometry;
    if (frame.positions.empty() && frame.normals.empty())
    {
        // Frozen zero-value semantics (R1.5 §5.4): empty spans mean no
        // runtime-owned deformed geometry, never a captured empty channel.
        this->snapshot.geometry.captured = false;
        return false;
    }
    if (frame.positions.size() != frame.normals.size())
    {
        throw std::logic_error(
            "Runtime vertex frame is inconsistent"
        );
    }
    if (frame.revision != this->snapshot.geometry.sourceRevision ||
        !this->snapshot.geometry.captured)
    {
        this->snapshot.geometry.positions.assign(
            frame.positions.begin(),
            frame.positions.end()
        );
        this->snapshot.geometry.normals.assign(
            frame.normals.begin(),
            frame.normals.end()
        );
        this->snapshot.geometry.sourceRevision = frame.revision;
        this->snapshot.geometry.captured = true;
        return true;
    }
    return false;
}
}  // namespace wisteria
