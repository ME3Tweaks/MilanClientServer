using Milan.Core.Data.Model;

namespace Milan.Core.Data.Repository;

public interface IPlayerRepository : IBaseRepository
{
    Task<IEnumerable<PlayerModel>> GetAllAsync();
    Task<IEnumerable<PlayerModel>> GetAllInMatchAsync(MatchModel match);
    Task<PlayerModel?> GetByIdAsync(int id);
    Task<PlayerModel?> GetByUsernameAsync(string userName);
    Task InsertAsync(PlayerModel player);
    Task RemoveAsync(PlayerModel player);
    Task UpdateAsync(PlayerModel player);
}
