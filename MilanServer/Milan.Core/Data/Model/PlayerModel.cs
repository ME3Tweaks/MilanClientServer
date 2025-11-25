using Microsoft.EntityFrameworkCore;
using System.ComponentModel.DataAnnotations.Schema;
namespace Milan.Core.Data.Model;

/// <summary>
/// Basic data of a registered player account.
/// </summary>
[Table("Players")]
[PrimaryKey(nameof(PlayerId))]
public sealed class PlayerModel
{
    public int PlayerId { get; set; }

    /// <summary>
    /// Unique identifier used for public display.
    /// </summary>
    public string Username { get; set; } = string.Empty;
    /// <summary>
    /// Reference to the <see cref="MatchModel"/> this player is currently playing.
    /// </summary>
    public MatchModel? CurrentMatch { get; set; }
    /// <summary>
    /// Reference to the <see cref="MatchModel"/> this player is currently hosting, if any.
    /// This field will be either <c>null</c> or equal to <see cref="CurrentMatch"/>.
    /// </summary>
    public MatchModel? CurrentHostedMatch { get; set; }
    /// <summary>
    /// Flag set when the player has completed account creation.
    /// </summary>
    public bool Active { get; set; }
    /// <summary>
    /// Flag set when the player should be unable to create or join public games.
    /// </summary>
    public bool Banned { get; set; }
}
