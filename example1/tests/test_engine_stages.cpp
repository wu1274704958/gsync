#include "../../src/core/template/engine_stages.hpp"
#include <cassert>

using namespace gsync::stages;
struct Frame { int value = 0; };
ErrorCode first(Frame& frame) { frame.value = frame.value * 10 + 1; return EC_Ok; }
ErrorCode fail(Frame& frame) { frame.value = frame.value * 10 + 2; return EC_Fail; }
ErrorCode last(Frame& frame) { frame.value = frame.value * 10 + 3; return EC_Ok; }
using TestRegistry = Registry<Engine<7, int, StageList<Handler<Connect::Validate, first>,
    Handler<Connect::Validate, fail>, Handler<Connect::Validate, last>>>>;
static_assert(std::is_same_v<Option<7, TestRegistry>, int>);
static_assert(std::is_void_v<Option<8, TestRegistry>>);
int main() {
    Frame frame;
    if (run<Connect::Validate, 7, TestRegistry>(frame) != EC_Fail || frame.value != 12) return 1;
    if (run<Connect::Cleanup, 7, TestRegistry>(frame) != EC_Ok) return 2;
    if (run<Connect::Validate, 8, TestRegistry>(frame) != EC_Ok) return 3;
    return 0;
}
