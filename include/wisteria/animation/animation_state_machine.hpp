#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace wisteria
{
class AnimationClip;
class Animator;

struct AnimationState
{
    std::string name;
    const AnimationClip* clip = nullptr;
    float speed = 1.0f;
    bool looping = true;
};

struct AnimationTransitionRule
{
    // Use AnimationStateMachine::AnyState as the source for a global rule.
    std::string sourceState;
    std::string destinationState;
    float crossFadeDuration = 0.2f;
    std::function<bool(const Animator&)> condition;
};

// First-version, single-layer animation state machine. Rules are evaluated in
// insertion order and at most one transition can fire per update.
class AnimationStateMachine
{
public:
    static constexpr std::string_view AnyState = "*";

    void AddState(AnimationState state);
    void AddTransition(AnimationTransitionRule transition);
    void SetState(std::string_view name);
    void Update(Animator& animator);
    void Clear() noexcept;

    bool HasState(std::string_view name) const;
    const AnimationState* CurrentState() const noexcept;
    std::size_t StateCount() const noexcept;
    std::size_t TransitionCount() const noexcept;

private:
    const AnimationState& StateAt(std::string_view name) const;
    void ApplyInitialState(Animator& animator);

    std::vector<AnimationState> states;
    std::vector<AnimationTransitionRule> transitions;
    std::unordered_map<std::string, std::size_t> stateLookup;
    std::optional<std::size_t> currentStateIndex;
    bool stateNeedsApply = false;
};
}  // namespace wisteria
