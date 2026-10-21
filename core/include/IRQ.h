#pragma once

namespace allnes
{
// A single wired-OR interrupt input. Devices that can assert the CPU's IRQ
// pin implement this interface; the CPU owns the handler objects.
class InterruptLine
{
public:
    virtual ~InterruptLine() = default;

    virtual void pull()    = 0;
    virtual void release() = 0;
};
} // namespace allnes
