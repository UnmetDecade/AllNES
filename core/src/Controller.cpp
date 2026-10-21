#include "Controller.h"

namespace allnes
{
void Controller::connect(const IInputSource* source, int player)
{
    m_source = source;
    m_player = player;
}

void Controller::strobe(Byte value)
{
    m_strobe = (value & 1) != 0;
    if (!m_strobe)
    {
        // A 1->0 transition latches the current button state.
        m_shift = m_source ? m_source->buttonState(m_player) : 0;
    }
}

Byte Controller::read()
{
    Byte value = 0;

    if (m_strobe)
    {
        // While strobed the register keeps reloading button A.
        value = (m_source ? m_source->buttonState(m_player) : 0) & ButtonA;
    }
    else
    {
        value   = m_shift & 1;
        m_shift >>= 1;
    }

    // Bits 5-7 float high on an unplugged NES controller.
    return value | 0x40;
}

} // namespace allnes
