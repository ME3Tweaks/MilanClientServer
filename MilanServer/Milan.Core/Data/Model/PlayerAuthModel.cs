using Microsoft.EntityFrameworkCore;
using System.ComponentModel.DataAnnotations.Schema;
namespace Milan.Core.Data.Model;

/// <summary>
/// Information about a <see cref="PlayerModel"/>'s authentication parameters.
/// </summary>
[Table("PlayerAuths")]
[PrimaryKey(nameof(PlayerAuthId))]
public sealed class PlayerAuthModel
{
    public int PlayerAuthId { get; set; }

    /// <summary>
    /// Associated <see cref="PlayerModel"/> instance
    /// for which these authentication parameters apply.
    /// </summary>
    public PlayerModel? Player { get; set; }
    /// <summary>
    /// Unique identifier used for authentication.
    /// </summary>
    public string Login { get; set; } = string.Empty;
    /// <summary>
    /// Active password hash.
    /// </summary>
    public byte[] Password { get; set; } = [];
    /// <summary>
    /// Secret for OTP validation.
    /// </summary>
    public byte[] OtpSecret { get; set; } = [];
}
