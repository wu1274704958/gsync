#pragma once
#include <type_traits>
#include "easylogging++.h"
#include "mqas/core/engine_driver.h"

template<size_t I, typename TU>
requires std::is_base_of_v<mqas::core::engine_base_interface, std::tuple_element_t<I, TU> >
void destroy_engine_internal(mqas::core::engine_base_interface* engine_base) {
    using EngineType = std::tuple_element_t<I, TU>;
    auto* engine = dynamic_cast<EngineType*>(engine_base);
    if (engine != nullptr) {
        delete engine;
    } else {
        if constexpr (I + 1 >= std::tuple_size_v<TU>) {
            LOG(WARNING) << "Can not destroy engine, reason: not find type " << typeid(TU).name();
        } else {
            destroy_engine_internal<I + 1, TU>(engine_base);
        }
    }
}

template<size_t I, typename TU>
    requires std::is_base_of_v<mqas::core::engine_base_interface, std::tuple_element_t<I, TU> >
bool destroy_engine_if_empty_internal(mqas::core::engine_base_interface* engine_base) {
    using EngineType = std::tuple_element_t<I, TU>;
    auto* engine = dynamic_cast<EngineType*>(engine_base);
    if (engine != nullptr) {
        if (engine->get_real_engine()->connect_count() <= 0) {
            delete engine;
            return true;
        }
        return false;
    } else {
        if constexpr (I + 1 >= std::tuple_size_v<TU>) {
            LOG(WARNING) << "Can not destroy engine, reason: not find type " << typeid(TU).name();
            return false;
        } else {
            return destroy_engine_internal<I + 1, TU>(engine_base);
        }
    }
}

extern void destroy_engine(mqas::core::engine_base_interface*);
extern bool destroy_engine_if_empty(mqas::core::engine_base_interface*);

#define GSY_TEMPLATE_BY_ENGINE_TYPE_FOR_ENGINE(ET)                              \
void destroy_engine(mqas::core::engine_base_interface* e)                   \
{                                                                           \
    if (!e)                                                                 \
        return;                                                             \
    destroy_engine_internal<0,ET>(e);                                       \
}                                                                           \
bool destroy_engine_if_empty(mqas::core::engine_base_interface* e)          \
{                                                                           \
    if (!e)                                                                 \
        return false;                                                       \
    return destroy_engine_if_empty_internal<0,ET>(e);                       \
}                                                                           \
