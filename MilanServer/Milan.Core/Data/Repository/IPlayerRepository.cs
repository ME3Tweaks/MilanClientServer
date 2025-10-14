using Milan.Core.Data.Model;

namespace Milan.Core.Data.Repository;

public interface IPlayerRepository : IBaseRepository
{
    IEnumerable<PlayerModel> GetAll();
    IEnumerable<PlayerModel> GetAllInMatch(MatchModel match);
    PlayerModel? GetById(int id);
    PlayerModel? GetByUsername(string userName);
    void Insert(PlayerModel player);
    void Remove(PlayerModel player);
    void Update(PlayerModel player);
}
