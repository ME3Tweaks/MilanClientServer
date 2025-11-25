using Milan.Core.Data.Model;

namespace Milan.Core.Data.Repository;

public interface IMatchRepository : IBaseRepository
{
    Task<IEnumerable<MatchModel>> GetAllAsync();
    Task<MatchModel?> GetByIdAsync(int id);
    Task InsertAsync(MatchModel match);
    Task UpdateAsync(MatchModel match);
}
