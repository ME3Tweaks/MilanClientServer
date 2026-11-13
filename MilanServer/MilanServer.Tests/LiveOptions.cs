using Microsoft.Extensions.Options;

namespace MilanServer.Tests;

// Stands in for config/milan.json being edited while the server runs.
public sealed class LiveOptions(MilanLiveOptions value) : IOptionsMonitor<MilanLiveOptions>
{
    Action<MilanLiveOptions, string?>? _listener;

    public MilanLiveOptions CurrentValue { get; private set; } = value;
    public MilanLiveOptions Get(string? name) => CurrentValue;

    public IDisposable OnChange(Action<MilanLiveOptions, string?> listener)
    {
        _listener = listener;
        return new Unsubscribe(() => _listener = null);
    }

    public void Change(MilanLiveOptions value)
    {
        CurrentValue = value;
        _listener?.Invoke(value, null);
    }

    sealed class Unsubscribe(Action dispose) : IDisposable
    {
        public void Dispose() => dispose();
    }
}
