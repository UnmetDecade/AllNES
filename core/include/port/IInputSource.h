#ifndef ALLNES_PORT_IINPUTSOURCE_H
#define ALLNES_PORT_IINPUTSOURCE_H

#include <cstdint>

namespace allnes
{
// Button bits, in the order the NES shift register reports them.
enum NesButton : std::uint8_t
{
    ButtonA      = 1u << 0,
    ButtonB      = 1u << 1,
    ButtonSelect = 1u << 2,
    ButtonStart  = 1u << 3,
    ButtonUp     = 1u << 4,
    ButtonDown   = 1u << 5,
    ButtonLeft   = 1u << 6,
    ButtonRight  = 1u << 7,

    ButtonCount = 8
};

// Platform-independent input port. player 0 = first controller.
class IInputSource
{
public:
    virtual ~IInputSource() = default;

    virtual std::uint8_t buttonState(int player) const = 0;
};

// No buttons pressed -- useful for headless runs and tests.
class NullInputSource : public IInputSource
{
public:
    std::uint8_t buttonState(int) const override { return 0; }
};

} // namespace allnes

#endif // ALLNES_PORT_IINPUTSOURCE_H
