#include "wisteria/common/pch.hpp"
#include "wisteria/animation/animation_state_machine.hpp"
#include "wisteria/animation/animator.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

void AnimationStateMachine::AddState(AnimationState state)
{
    if (state.name.empty())
        throw std::invalid_argument("Animation state name must not be empty");
    if (state.name == AnyState)
        throw std::invalid_argument("Animation state name '*' is reserved");
    if (state.clip == nullptr)
        throw std::invalid_argument("Animation state must reference a clip");
    if (!std::isfinite(state.speed) || state.speed < 0.0f)
    {
        throw std::invalid_argument(
            "Animation state speed must be finite and non-negative"
        );
    }
    if (this->stateLookup.contains(state.name))
    {
        throw std::invalid_argument(
            "Animation state name already exists: " + state.name
        );
    }

    const std::size_t index = this->states.size();
    this->states.push_back(std::move(state));
    this->stateLookup.emplace(this->states.back().name, index);
}

void AnimationStateMachine::AddTransition(
    AnimationTransitionRule transition
)
{
    if (transition.sourceState.empty() ||
        transition.destinationState.empty())
    {
        throw std::invalid_argument(
            "Animation transition state names must not be empty"
        );
    }
    if (transition.sourceState != AnyState &&
        !this->HasState(transition.sourceState))
    {
        throw std::invalid_argument(
            "Animation transition source state does not exist: " +
            transition.sourceState
        );
    }
    if (!this->HasState(transition.destinationState))
    {
        throw std::invalid_argument(
            "Animation transition destination state does not exist: " +
            transition.destinationState
        );
    }
    if (!std::isfinite(transition.crossFadeDuration) ||
        transition.crossFadeDuration < 0.0f)
    {
        throw std::invalid_argument(
            "Animation transition duration must be finite and non-negative"
        );
    }
    if (!transition.condition)
        throw std::invalid_argument("Animation transition requires a condition");

    this->transitions.push_back(std::move(transition));
}

void AnimationStateMachine::SetState(std::string_view name)
{
    const auto iterator = this->stateLookup.find(std::string(name));
    if (iterator == this->stateLookup.end())
    {
        throw std::invalid_argument(
            "Animation state does not exist: " + std::string(name)
        );
    }
    this->currentStateIndex = iterator->second;
    this->stateNeedsApply = true;
}

void AnimationStateMachine::Update(Animator& animator)
{
    if (!this->currentStateIndex.has_value())
        return;
    if (this->stateNeedsApply)
        this->ApplyInitialState(animator);

    const AnimationState& current = this->states[*this->currentStateIndex];
    for (const AnimationTransitionRule& transition : this->transitions)
    {
        if (transition.sourceState != AnyState &&
            transition.sourceState != current.name)
        {
            continue;
        }
        const AnimationState& destination =
            this->StateAt(transition.destinationState);
        if (destination.name == current.name)
            continue;
        if (!transition.condition(animator))
            continue;

        // CrossFade captures the old state's playback settings as its source;
        // destination settings therefore have to be assigned afterwards.
        animator.CrossFade(
            *destination.clip,
            transition.crossFadeDuration
        );
        animator.SetSpeed(destination.speed);
        animator.SetLooping(destination.looping);
        this->currentStateIndex = this->stateLookup.at(destination.name);
        return;
    }
}

void AnimationStateMachine::Clear() noexcept
{
    this->states.clear();
    this->transitions.clear();
    this->stateLookup.clear();
    this->currentStateIndex.reset();
    this->stateNeedsApply = false;
}

bool AnimationStateMachine::HasState(std::string_view name) const
{
    return this->stateLookup.contains(std::string(name));
}

const AnimationState* AnimationStateMachine::CurrentState() const noexcept
{
    return this->currentStateIndex.has_value()
        ? &this->states[*this->currentStateIndex]
        : nullptr;
}

std::size_t AnimationStateMachine::StateCount() const noexcept
{
    return this->states.size();
}

std::size_t AnimationStateMachine::TransitionCount() const noexcept
{
    return this->transitions.size();
}

const AnimationState& AnimationStateMachine::StateAt(
    std::string_view name
) const
{
    const auto iterator = this->stateLookup.find(std::string(name));
    if (iterator == this->stateLookup.end())
    {
        throw std::logic_error(
            "Animation transition references a missing state: " +
            std::string(name)
        );
    }
    return this->states[iterator->second];
}

void AnimationStateMachine::ApplyInitialState(Animator& animator)
{
    const AnimationState& state = this->states[*this->currentStateIndex];
    animator.SetSpeed(state.speed);
    animator.SetLooping(state.looping);
    animator.Play(*state.clip);
    this->stateNeedsApply = false;
}
