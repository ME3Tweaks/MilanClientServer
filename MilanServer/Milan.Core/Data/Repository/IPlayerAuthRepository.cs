using Milan.Core.Data.Model;

namespace Milan.Core.Data.Repository;

public interface IPlayerAuthRepository : IBaseRepository
{
    Task<PlayerAuthModel?> GetByPlayerAsync(PlayerModel player);
    Task InsertAsync(PlayerAuthModel player);
    Task RemoveAsync(PlayerAuthModel player);
    Task UpdateAsync(PlayerAuthModel player);
}
