using System.Buffers.Binary;
using System.Buffers.Text;
using System.Security.Cryptography;
using System.Text;

namespace MilanServer;

// Accountless capabilities. Host, search, and join keys are 256-bit bearer secrets;
// the server keeps only their SHA-256 hashes. Invite codes are shorter so players
// can type them, and rely on rate limiting plus a 50-bit space against guessing.
internal static class Secrets
{
    // Crockford base32: no I, L, O, or U.
    const string InviteAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    public const int InviteCodeLength = 10;

    public static string NewId() => Base64Url.EncodeToString(RandomNumberGenerator.GetBytes(16));

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
            return false;

        return CryptographicOperations.FixedTimeEquals(expectedHash, Hash(presentedKey));
    }

    // Returns the canonical 10-character form; display it with FormatInviteCode.
    public static string NewInviteCode() => RandomNumberGenerator.GetString(InviteAlphabet, InviteCodeLength);

    public static string FormatInviteCode(string code) => $"{code[..5]}-{code[5..]}";

    public static bool TryNormalizeInviteCode(string? input, out string code)
    {
        code = string.Empty;
        if (input is null || input.Length > 32)
            return false;

        StringBuilder builder = new(InviteCodeLength);
        foreach (char raw in input)
        {
            if (raw is '-' or ' ')
                continue;

            char c = char.ToUpperInvariant(raw) switch
            {
                'O' => '0',
                'I' or 'L' => '1',
                var other => other,
            };
            if (!InviteAlphabet.Contains(c) || builder.Length == InviteCodeLength)
                return false;

            builder.Append(c);
        }

        if (builder.Length != InviteCodeLength)
            return false;

        code = builder.ToString();
        return true;
    }
}
