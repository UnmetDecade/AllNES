#include "Mapper.h"
#include "MapperAxROM.h"
#include "MapperCNROM.h"
#include "MapperColorDreams.h"
#include "MapperGxROM.h"
#include "MapperMMC3.h"
#include "MapperNROM.h"
#include "MapperSxROM.h"
#include "MapperUxROM.h"

namespace allnes
{
MirroringMode Mapper::mirroringMode()
{
    return static_cast<MirroringMode>(m_cartridge.mirroringMode());
}

std::unique_ptr<Mapper> Mapper::createMapper(Board                     board,
                                             Cartridge&                cartridge,
                                             InterruptLine&            irq,
                                             std::function<void(void)> mirroringChanged)
{
    if (board == BoardNrom)
        return std::make_unique<MapperNROM>(cartridge);

    if (board == BoardUxrom)
        return std::make_unique<MapperUxROM>(cartridge);

    if (board == BoardCnrom)
        return std::make_unique<MapperCNROM>(cartridge);

    if (board == BoardSxrom)
        return std::make_unique<MapperSxROM>(cartridge, std::move(mirroringChanged));

    if (board == BoardMmc3)
        return std::make_unique<MapperMMC3>(cartridge, irq, std::move(mirroringChanged));

    if (board == BoardMmc3Bootleg245)
        return std::make_unique<MapperMMC3_245>(cartridge, irq, std::move(mirroringChanged));

    if (board == BoardAxrom)
        return std::make_unique<MapperAxROM>(cartridge, std::move(mirroringChanged));

    if (board == BoardColorDreams)
        return std::make_unique<MapperColorDreams>(cartridge, std::move(mirroringChanged));

    if (board == BoardGxrom)
        return std::make_unique<MapperGxROM>(cartridge, std::move(mirroringChanged));

    return nullptr;
}

} // namespace allnes
