using Milan.Core.Data.Model;

namespace Milan.Core.Data.Repository;

public interface IMatchRepository : IBaseRepository
{
    IEnumerable<MatchModel> GetAll();
    MatchModel? GetById(int id);
    void Insert(MatchModel match);
    void Update(MatchModel match);
}
