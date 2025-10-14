using Milan.Core.Data.Model;

namespace Milan.Core.Data.Repository;

public interface IPlayerAuthRepository : IBaseRepository
{
    PlayerAuthModel? GetByPlayer(PlayerModel player);
}
