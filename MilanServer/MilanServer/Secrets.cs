using System.Buffers.Binary;
using System.Buffers.Text;
using System.Security.Cryptography;
using System.Text;

namespace MilanServer;

// Accountless capabilities. Host, search, and join keys are 256-bit bearer secrets;
// the server keeps only their SHA-256 hashes. Invite codes are shorter so players
// can type them, and rely on rate limiting plus 5 bits per character against guessing.
internal static class Secrets
{
    // Crockford base32: no I, L, O, or U.
    const string InviteAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    public const int MinInviteCodeLength = 3;
    public const int MaxInviteCodeLength = 10;

    public static string NewId() => Base64Url.EncodeToString(RandomNumberGenerator.GetBytes(16));
    public static bool IsId(string? id) => id is { Length: 22 }
        && id.All(c => char.IsAsciiLetterOrDigit(c) || c is '-' or '_');

    public static string NewKey() => Base64Url.EncodeToString(RandomNumberGenerator.GetBytes(32));

    public static (UInt128 Id, byte[] Secret) NewUdpTicket()
    {
        Span<byte> id = stackalloc byte[16];
        RandomNumberGenerator.Fill(id);
        return (BinaryPrimitives.ReadUInt128BigEndian(id), RandomNumberGenerator.GetBytes(32));
    }

    public static UdpTicket ToContract(UInt128 id, byte[] secret)
    {
        Span<byte> idBytes = stackalloc byte[16];
        BinaryPrimitives.WriteUInt128BigEndian(idBytes, id);
        return new UdpTicket(Base64Url.EncodeToString(idBytes), Base64Url.EncodeToString(secret));
    }

    public static byte[] Hash(string key) => SHA256.HashData(Encoding.UTF8.GetBytes(key));

    public static bool Matches(byte[] expectedHash, string? presentedKey)
    {
        if (string.IsNullOrEmpty(presentedKey))
        {
            return false;
        }

        return CryptographicOperations.FixedTimeEquals(expectedHash, Hash(presentedKey));
    }

    /// <summary>
    /// Generates a new random invite code with the given length
    /// </summary>
    /// <param name="length"></param>
    /// <returns></returns>
    public static string NewInviteCode(int length) => RandomNumberGenerator.GetString(InviteAlphabet, length);

    public static long InviteCodeCount(int length) => 1L << (5 * length);

    /// <summary>
    /// Formats an invite code to make it easier to read
    /// </summary>
    /// <param name="code"></param>
    /// <returns></returns>
    public static string FormatInviteCode(string code)
    {
        if (code.Length < 6)
        {
            return code;
        }

        int half = (code.Length + 1) / 2;
        return $"{code[..half]}-{code[half..]}";
    }

    // Accepts any valid length, so codes kept across a restart work after InviteCodeLength changes.
    public static bool TryNormalizeInviteCode(string? input, out string code)
    {
        code = string.Empty;
        if (input is null || input.Length > 32)
        {
            return false;
        }

        StringBuilder builder = new(MaxInviteCodeLength);
        foreach (char raw in input)
        {
            if (raw is '-' or ' ')
            {
                continue;
            }

            char c = char.ToUpperInvariant(raw) switch
            {
                'O' => '0',
                'I' or 'L' => '1',
                var other => other,
            };
            if (!InviteAlphabet.Contains(c) || builder.Length == MaxInviteCodeLength)
            {
                return false;
            }

            builder.Append(c);
        }

        if (builder.Length < MinInviteCodeLength)
        {
            return false;
        }

        code = builder.ToString();
        return true;
    }
}
