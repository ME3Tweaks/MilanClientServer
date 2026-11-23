using Microsoft.Extensions.Options;

namespace MilanServer;

// Counts active requests, not arrivals. Source entries disappear with the last request;
// the total cap bounds the table even when an attacker rotates addresses.
public sealed class RequestBudget(IOptions<MilanServerOptions> options)
{
    readonly Lock _lock = new();
    readonly Dictionary<string, int> _sources = new(StringComparer.Ordinal);
    readonly Dictionary<string, int> _pollSources = new(StringComparer.Ordinal);
    int _active;
    int _polls;

    public bool TryEnter(string source, bool longPoll = false)
    {
        lock (_lock)
        {
            int count = _sources.GetValueOrDefault(source);
            if (_active >= options.Value.MaxConcurrentRequests || count >= options.Value.MaxConcurrentRequestsPerAddress)
            {
                return false;
            }

            if (longPoll)
            {
                int polls = _pollSources.GetValueOrDefault(source);
                if (_polls >= options.Value.MaxConcurrentLongPolls || polls >= options.Value.MaxConcurrentLongPollsPerAddress)
                {
                    return false;
                }

                _pollSources[source] = polls + 1;
                _polls++;
            }
            _active++;
            _sources[source] = count + 1;
            return true;
        }
    }

    public void Exit(string source, bool longPoll = false)
    {
        lock (_lock)
        {
            int count = _sources[source] - 1;
            if (count == 0)
            {
                _sources.Remove(source);
            }
            else
            {
                _sources[source] = count;
            }

            _active--;
            if (longPoll)
            {
                int polls = _pollSources[source] - 1;
                if (polls == 0)
                {
                    _pollSources.Remove(source);
                }
                else
                {
                    _pollSources[source] = polls;
                }

                _polls--;
            }
        }
    }
}

sealed class LongPollEndpoint;

// Independent from the store lock. An address can consume only its budget and the
// total budget bounds both work and memory. Windows last one second, including drops.
sealed class DiscoveryWorkBudget(MilanServerOptions options, TimeProvider time)
{
    readonly Lock _lock = new();
    readonly Dictionary<string, int> _sources = new(StringComparer.Ordinal);
    long _window = time.GetTimestamp();
    int _packets;

    public bool TryEnter(string source)
    {
        lock (_lock)
        {
            long now = time.GetTimestamp();
            if (time.GetElapsedTime(_window, now) >= TimeSpan.FromSeconds(1))
            {
                _window = now;
                _packets = 0;
                _sources.Clear();
            }
            if (_packets >= options.DiscoveryPacketsPerSecond)
            {
                return false;
            }

            int count = _sources.GetValueOrDefault(source);
            if (count >= options.DiscoveryPacketsPerSecondPerAddress)
            {
                return false;
            }

            _packets++;
            _sources[source] = count + 1;
            return true;
        }
    }
}
