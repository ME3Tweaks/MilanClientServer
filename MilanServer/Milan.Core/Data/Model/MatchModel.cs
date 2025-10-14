namespace Milan.Core.Data.Model;

/// <summary>
/// Single game session.
/// </summary>
public sealed class MatchModel : BaseModel
{
    /// <summary>
    /// TLK ID of the map.
    /// </summary>
    public int MapStrRef { get; set; }
    /// <summary>
    /// TLK ID of the enemy faction.
    /// </summary>
    public int FactionStrRef { get; set; }
    /// <summary>
    /// Pre-defined difficulty level.
    /// </summary>
    public EMatchDifficulty Difficulty { get; set; }
    /// <summary>
    /// Current state of the match's progress.
    /// </summary>
    public EMatchState State { get; set; }
    /// <summary>
    /// Player who created the match.
    /// </summary>
    public PlayerModel? Creator { get; set; }
    /// <summary>
    /// Time at which the match was created.
    /// </summary>
    public DateTime CreatedAt { get; set; }
    /// <summary>
    /// Time at which the match was complete, if any.
    /// </summary>
    public DateTime? CompletedAt { get; set; }
    /// <summary>
    /// Match outcome, if any.
    /// </summary>
    public EMatchResult? Result { get; set; }
}

/// <summary>
/// Match difficulty level, based on the original ME3 options.
/// </summary>
/// <remarks>
/// TODO: Consider using some other indicator for difficulty
/// in case we want to support player-defined difficulty levels,
/// e.g. introduced by second-tier mods.
/// </remarks>
public enum EMatchDifficulty
{
    Bronze = 1,
    Silver = 2,
    Gold = 3,
    Platinum = 4,
}

/// <summary>
/// Match progress state.
/// </summary>
public enum EMatchState
{
    Lobby = 1,
    PreGame = 2,
    Game = 3,
    Complete = 4,
}

/// <summary>
/// Match outcome.
/// </summary>
public enum EMatchResult
{
    Victory = 1,
    DefeatToDeath = 2,
    DefeatToFailure = 3,
}
