using Milan.Core.Data.Model;

namespace Milan.Core.Data.Repository;

public interface IPlayerAuthRepository : IBaseRepository
{
    PlayerAuthModel? GetByPlayer(PlayerModel player);
    void Insert(PlayerAuthModel player);
    void Remove(PlayerAuthModel player);
    void Update(PlayerAuthModel player);
}
