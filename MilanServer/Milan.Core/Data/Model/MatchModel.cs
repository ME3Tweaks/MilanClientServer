namespace Milan.Core.Data.Model;

/// <summary>
/// Single game session.
/// </summary>
public sealed class MatchModel : BaseModel
{
    /// <summary>
    /// TLK ID of the map.
    /// </summary>
    public TlkStrRef Map { get; set; }
    /// <summary>
    /// TLK ID of the enemy faction.
    /// </summary>
    public TlkStrRef Faction { get; set; }
    /// <summary>
    /// TLK ID of the difficulty level.
    /// </summary>
    public TlkStrRef Difficulty { get; set; }
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
