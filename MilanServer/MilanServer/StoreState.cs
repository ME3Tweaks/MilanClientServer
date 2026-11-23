using System.Text.Json;
using System.Text.Json.Serialization;
using Microsoft.Extensions.Options;

namespace MilanServer;

// Milan:StateFile: the lobbies written on shutdown and read back at the next start, so a short
// restart keeps them. Hosts, members, and joiners carry on with the IDs, keys, and codes they hold.
sealed class StoreStateFile(MatchmakingStore store, IOptions<MilanServerOptions> options, IHostEnvironment environment,
    TimeProvider time, ILogger<StoreStateFile> logger)
{
    readonly string _path = options.Value.StateFile.Length == 0 ? ""
        : Path.GetFullPath(options.Value.StateFile, environment.ContentRootPath);

    public bool Enabled => _path.Length > 0;

    // At startup. The file is deleted once read, so a later crash cannot bring back stale lobbies.
    public void Restore()
    {
        if (!Enabled || !File.Exists(_path))
        {
            return;
        }

        try
        {
            SavedStore saved;
            using (FileStream stream = File.OpenRead(_path))
            {
                saved = JsonSerializer.Deserialize(stream, StoreStateJsonContext.Default.SavedStore)
                    ?? throw new JsonException("The file holds no state.");
            }

            TimeSpan age = time.GetUtcNow() - saved.SavedAt;
            if (saved.Format != SavedStore.CurrentFormat)
            {
                logger.LogWarning("Not restoring lobbies from {Path}: it has format {Format}, and this server reads {Expected}.",
                    _path, saved.Format, SavedStore.CurrentFormat);
            }
            else if (age > TimeSpan.FromSeconds(options.Value.StateMaxAgeSeconds))
            {
                logger.LogWarning("Not restoring lobbies from {Path}: it was saved {Age:0} s ago, more than StateMaxAgeSeconds ({Max} s).",
                    _path, age.TotalSeconds, options.Value.StateMaxAgeSeconds);
            }
            else
            {
                int restored = store.Restore(saved);
                logger.LogInformation("Restored {Restored} of {Saved} lobbies from {Path}, saved {Age:0} s ago.",
                    restored, saved.Lobbies.Count, _path, age.TotalSeconds);
            }
        }
        catch (Exception ex)
        {
            logger.LogWarning(ex, "Not restoring lobbies from {Path}: it could not be read.", _path);
        }

        try
        {
            File.Delete(_path);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            logger.LogWarning(ex, "Could not delete {Path} after reading it.", _path);
        }
    }

    // On shutdown, before the web server stops. Only the server's own user can read the file.
    public void Save()
    {
        if (!Enabled)
        {
            return;
        }

        SavedStore saved = store.StopForRestart();
        string temporary = _path + ".tmp";
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(_path)!);
            File.Delete(temporary);
            FileStreamOptions create = new() { Mode = FileMode.CreateNew, Access = FileAccess.Write };
            if (!OperatingSystem.IsWindows())
            {
                create.UnixCreateMode = UnixFileMode.UserRead | UnixFileMode.UserWrite;
            }

            using (FileStream stream = new(temporary, create))
            {
                JsonSerializer.Serialize(stream, saved, StoreStateJsonContext.Default.SavedStore);
            }

            File.Move(temporary, _path, overwrite: true);
            logger.LogInformation("Saved {Count} lobbies to {Path} for the next start.", saved.Lobbies.Count, _path);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            logger.LogError(ex, "Could not save the lobbies to {Path}; the next start begins with none.", _path);
        }
    }
}

// Keys are kept only as hashes. Invite codes, UDP ticket secrets, and traversal keys are kept as
// they are, because the clients that hold them keep using them.
sealed record SavedStore(int Format, DateTimeOffset SavedAt, long NextLobbySequence, IReadOnlyList<SavedLobby> Lobbies)
{
    // Bump when a saved field changes meaning; a file in another format is ignored.
    public const int CurrentFormat = 1;
}

// Every property is required, so a file missing one is rejected rather than half-restored.
sealed record SavedLobby
{
    public required string Id { get; init; }
    public required long Sequence { get; init; }
    public required byte[] HostKeyHash { get; init; }
    public required IReadOnlyList<byte[]> ReplacedHostKeyHashes { get; init; }
    public required string GameVersion { get; init; }
    public required string SourceKey { get; init; }
    public required string InviteCode { get; init; }
    public required LobbySnapshot State { get; init; }
    public required DateTimeOffset WaitingSince { get; init; }
    public required string? PublicAddress { get; init; }
    public required DateTimeOffset ExpiresAt { get; init; }
    public required SavedUdpTicket? UdpTicket { get; init; }
    public required string? ObservedEndpoint { get; init; }
    public required long EventVersion { get; init; }
    public required DateTimeOffset HostLastSeen { get; init; }
    public required DateTimeOffset? HostConfirmedAliveAt { get; init; }
    public required IReadOnlyDictionary<string, DateTimeOffset> JoinFailures { get; init; }
    public required IReadOnlyDictionary<string, DateTimeOffset> Abandoned { get; init; }
    public required DateTimeOffset? LastJoinedAt { get; init; }
    public required DateTimeOffset LastProbeAt { get; init; }
    public required IReadOnlyList<SavedMember> Members { get; init; }
    public required IReadOnlyList<SavedMember> SettledMembers { get; init; }
    public required IReadOnlyList<SavedReservation> Reservations { get; init; }
}

sealed record SavedMember
{
    public required string Id { get; init; }
    public required byte[] KeyHash { get; init; }
    public required string SourceKey { get; init; }
    public required string? PublicAddress { get; init; }
    public required DateTimeOffset JoinedAt { get; init; }
    public required DateTimeOffset SettledAt { get; init; }
}

sealed record SavedReservation
{
    public required string Id { get; init; }
    public required byte[] JoinKeyHash { get; init; }
    public required string SourceKey { get; init; }
    public required string? PublicAddress { get; init; }
    public required string TraversalKey { get; init; }
    public required DateTimeOffset ExpiresAt { get; init; }
    public required bool Migration { get; init; }
    public required DateTimeOffset? JoinedAt { get; init; }
    public required SavedUdpTicket? UdpTicket { get; init; }
    public required string? ObservedEndpoint { get; init; }
    public required int? GamePort { get; init; }
    public required IReadOnlyList<string> LanAddresses { get; init; }
}

sealed record SavedUdpTicket(UInt128 Id, byte[] Secret, ulong LastSequence);

// Separate from MilanJsonContext: the state file is not part of the HTTP API.
[JsonSourceGenerationOptions(JsonSerializerDefaults.Web, RespectNullableAnnotations = true,
    RespectRequiredConstructorParameters = true)]
[JsonSerializable(typeof(SavedStore))]
sealed partial class StoreStateJsonContext : JsonSerializerContext;
