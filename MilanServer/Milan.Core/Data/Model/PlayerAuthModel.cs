namespace Milan.Core.Data.Model;

/// <summary>
/// Information about a <see cref="PlayerModel"/>'s authentication parameters.
/// </summary>
public sealed class PlayerAuthModel : BaseModel
{
    /// <summary>
    /// Associated <see cref="PlayerModel"/> instance
    /// for which these authentication parameters apply.
    /// </summary>
    public required PlayerModel Player { get; set; }
    /// <summary>
    /// Unique identifier used for authentication.
    /// </summary>
    public required string Login { get; set; }
    /// <summary>
    /// Active password hash.
    /// </summary>
    public byte[] Password { get; set; } = [];
    /// <summary>
    /// Secret for OTP validation.
    /// </summary>
    public byte[] OtpSecret { get; set; } = [];
}
