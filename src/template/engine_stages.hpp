#pragma once

#include <tuple>
#include <type_traits>
#include <utility>
#include "def.h"
#include "core/server.h"
#include <memory>

struct ServerEntry;

namespace gsync::stages {

enum class Connect { Validate, BeforeEngine, InitEngine, BeforeConnect, Registered, Authenticated, Cleanup };
enum class Server { Validate, BeforeEngine, InitEngine, Reuse, Started, Authenticate, Accepted, Cleanup };

struct ServerStart {
    GSY_BaseServerContext* context;
    std::shared_ptr<ServerEntry> server;
};

template<auto Stage, auto Function> struct Handler {
    static constexpr auto stage = Stage;
    static constexpr auto function = Function;
};
template<class... Handlers> struct StageList {};
template<GSY_EngineId Id, class Option, class Stages> struct Engine {
    static constexpr GSY_EngineId id = Id;
    using option_type = Option;
    using stages = Stages;
};
template<class... Engines> struct Registry {
    static consteval bool unique_ids() {
        constexpr GSY_EngineId ids[] = {0, Engines::id...};
        for (size_t i = 1; i < sizeof...(Engines) + 1; ++i)
            for (size_t j = i + 1; j < sizeof...(Engines) + 1; ++j)
                if (ids[i] == ids[j]) return false;
        return true;
    }
    static_assert(unique_ids(), "engine IDs must be unique");
};

template<GSY_EngineId Id, class Registry> struct Lookup;
template<GSY_EngineId Id> struct Lookup<Id, Registry<>> { using type = Engine<Id, void, StageList<>>; };
template<GSY_EngineId Id, class First, class... Rest>
struct Lookup<Id, Registry<First, Rest...>> {
    using type = std::conditional_t<Id == First::id, First, typename Lookup<Id, Registry<Rest...>>::type>;
};
template<GSY_EngineId Id, class Registry>
using Option = typename Lookup<Id, Registry>::type::option_type;

template<auto Stage, class Frame, class... Handlers>
ErrorCode run(StageList<Handlers...>, Frame& frame) {
    ErrorCode result = EC_Ok;
    auto invoke = [&]<class H>() {
        if constexpr (H::stage == Stage) {
            static_assert(std::is_invocable_r_v<ErrorCode, decltype(H::function), Frame&>, "invalid stage handler signature");
            if (result == EC_Ok) result = H::function(frame);
        }
    };
    (invoke.template operator()<Handlers>(), ...);
    return result;
}
template<auto Stage, GSY_EngineId Id, class Registry, class Frame>
ErrorCode run(Frame& frame) {
    return run<Stage>(typename Lookup<Id, Registry>::type::stages{}, frame);
}
}
