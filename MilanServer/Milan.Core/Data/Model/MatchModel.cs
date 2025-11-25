using System.ComponentModel.DataAnnotations.Schema;
using Microsoft.EntityFrameworkCore;
using Microsoft.EntityFrameworkCore.Storage.ValueConversion;

namespace Milan.Core.Data.Model;


/// <summary>
/// Single game session.
/// </summary>
[Table("Matches")]
[PrimaryKey(nameof(MatchId))]
public sealed class MatchModel
{
    public int MatchId { get; set; }

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
    public EMatchResult Result { get; set; }
}

/// <summary>
/// Match progress state.
/// </summary>
public enum EMatchState
{
    Unknown = 0,
    Lobby = 1,
    PreGame = 2,
    Game = 3,
    Complete = 4,
}

public class EMatchStateConverter : ValueConverter<EMatchState, int>
{
    // TODO: Use checked int->enum conversion.
    public EMatchStateConverter() : base(f => (int)f, t => (EMatchState)t) {}
}

/// <summary>
/// Match outcome.
/// </summary>
public enum EMatchResult
{
    Unknown = 0,
    Victory = 1,
    DefeatToDeath = 2,
    DefeatToFailure = 3,
}

public class EMatchResultConverter : ValueConverter<EMatchResult, int>
{
    // TODO: Use checked int->enum conversion.
    public EMatchResultConverter() : base(f => (int)f, t => (EMatchResult)t) { }
}
