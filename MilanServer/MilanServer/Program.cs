using System.Globalization;
using System.Text.Json.Serialization;
using System.Threading.RateLimiting;
using Microsoft.AspNetCore.HttpOverrides;
using Microsoft.AspNetCore.RateLimiting;
using Microsoft.Extensions.Options;
using MilanServer;

WebApplicationBuilder builder = WebApplication.CreateBuilder(args);

builder.Services.AddOptions<MilanServerOptions>()
    .Bind(builder.Configuration.GetSection(MilanServerOptions.SectionName))
    .ValidateOnStart();
builder.Services.AddSingleton<IValidateOptions<MilanServerOptions>, MilanServerOptionsValidator>();
builder.Services.AddSingleton(TimeProvider.System);
builder.Services.AddSingleton<MatchmakingStore>();
builder.Services.AddSingleton<RequestBudget>();
builder.Services.AddHostedService<ExpirySweeper>();
builder.Services.AddSingleton<DiscoveryEndpoint>();
builder.Services.AddHostedService<UdpDiscoveryService>();

builder.WebHost.ConfigureKestrel(kestrel =>
{
    kestrel.AddServerHeader = false;
    kestrel.Limits.MaxRequestBodySize = 16 * 1024;
    kestrel.Limits.MaxConcurrentConnections = builder.Configuration.GetValue("Milan:MaxConcurrentConnections", 4096);
});

builder.Services.ConfigureHttpJsonOptions(json =>
{
    json.SerializerOptions.TypeInfoResolverChain.Insert(0, MilanJsonContext.Default);
    json.SerializerOptions.RespectNullableAnnotations = true;
    json.SerializerOptions.RespectRequiredConstructorParameters = true;
    json.SerializerOptions.DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull;
});
// Surface malformed bodies as BadHttpRequestException so they get a ServerError body.
builder.Services.Configure<RouteHandlerOptions>(routes => routes.ThrowOnBadRequest = true);

builder.Services.AddOptions<ForwardedHeadersOptions>()
    .Configure<IOptions<MilanServerOptions>>((forwarded, milan) =>
    {
        forwarded.ForwardedHeaders = ForwardedHeaders.XForwardedFor | ForwardedHeaders.XForwardedProto;
        foreach (string proxy in milan.Value.TrustedProxies)
            ProxyTrust.Add(forwarded, proxy);
    });

builder.Services.AddRateLimiter(_ => { });
builder.Services.AddOptions<RateLimiterOptions>()
    .Configure<IOptions<MilanServerOptions>>((limiter, milan) =>
    {
        MilanServerOptions o = milan.Value;

        static RateLimitPartition<string> PerAddress(HttpContext http, int permitsPerMinute) =>
            RateLimitPartition.GetFixedWindowLimiter(
                NetworkAddresses.AccountingKey(http.Connection.RemoteIpAddress),
                _ => new FixedWindowRateLimiterOptions { PermitLimit = permitsPerMinute, Window = TimeSpan.FromMinutes(1) });

        limiter.GlobalLimiter = PartitionedRateLimiter.CreateChained(
            PartitionedRateLimiter.Create<HttpContext, string>(_ => RateLimitPartition.GetTokenBucketLimiter("server",
                _ => new TokenBucketRateLimiterOptions
                {
                    TokenLimit = o.RequestBurst, TokensPerPeriod = o.TotalRequestsPerSecond,
                    ReplenishmentPeriod = TimeSpan.FromSeconds(1), AutoReplenishment = true, QueueLimit = 0
                })),
            PartitionedRateLimiter.Create<HttpContext, string>(http => PerAddress(http, o.RequestsPerMinute)));
        limiter.AddPolicy(Api.CreatePolicy, http => PerAddress(http, o.CreatesPerMinute));
        limiter.AddPolicy(Api.RedeemPolicy, http => PerAddress(http, o.InviteRedeemsPerMinute));
        limiter.OnRejected = async (context, cancellationToken) =>
        {
            HttpResponse response = context.HttpContext.Response;
            response.StatusCode = StatusCodes.Status429TooManyRequests;
            if (context.Lease.TryGetMetadata(MetadataName.RetryAfter, out TimeSpan retryAfter))
                response.Headers.RetryAfter = ((int)Math.Ceiling(retryAfter.TotalSeconds)).ToString(CultureInfo.InvariantCulture);
            await response.WriteAsJsonAsync(new ServerError("rate_limited", "Too many requests. Retry after a short delay."),
                MilanJsonContext.Default.ServerError, cancellationToken: cancellationToken);
        };
    });

WebApplication app = builder.Build();

app.UseUntrustedProxyWarning();
app.UseForwardedHeaders();
app.Use(async (http, next) =>
{
    try
    {
        await next(http);
    }
    catch (BadHttpRequestException ex) when (!http.Response.HasStarted)
    {
        http.Response.StatusCode = ex.StatusCode;
        await http.Response.WriteAsJsonAsync(new ServerError("invalid_request", ex.Message), MilanJsonContext.Default.ServerError);
    }
});
app.UseRateLimiter();
app.Use(async (http, next) =>
{
    // Prevent storing capabilities in HTTP caches; stats supplies its own cache policy.
    if (http.Request.Path.StartsWithSegments("/v1") && http.Request.Path != "/v1/stats")
        http.Response.Headers.CacheControl = "no-store";
    RequestBudget budget = http.RequestServices.GetRequiredService<RequestBudget>();
    string source = NetworkAddresses.AccountingKey(http.Connection.RemoteIpAddress);
    bool longPoll = http.GetEndpoint()?.Metadata.GetMetadata<LongPollEndpoint>() is not null;
    if (!budget.TryEnter(source, longPoll))
    {
        http.Response.Headers.RetryAfter = "1";
        await Api.Error(Failures.RequestLimit).ExecuteAsync(http);
        return;
    }
    try { await next(http); }
    finally { budget.Exit(source, longPoll); }
});
app.UseMilanAccessKey();
app.MapMilanApi();

MilanServerOptions settings = app.Services.GetRequiredService<IOptions<MilanServerOptions>>().Value;
app.Logger.LogInformation("Milan control protocol {Protocol}; expected game version {ExpectedGameVersion}; server password {PasswordMode}; active request limits {Total}/{Address} per address; UDP work limits {UdpTotal}/{UdpAddress} per second.",
    Api.ProtocolVersion, settings.ExpectedGameVersion.Length > 0 ? settings.ExpectedGameVersion : "any",
    settings.AccessKey.Length > 0 ? "requires HTTPS" : "disabled (HTTP permitted)",
    settings.MaxConcurrentRequests, settings.MaxConcurrentRequestsPerAddress,
    settings.DiscoveryPacketsPerSecond, settings.DiscoveryPacketsPerSecondPerAddress);

app.Run();

public partial class Program;

sealed class ExpirySweeper(MatchmakingStore store, TimeProvider time) : BackgroundService
{
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        using PeriodicTimer timer = new(TimeSpan.FromSeconds(5), time);
        try
        {
            while (await timer.WaitForNextTickAsync(stoppingToken))
                store.Sweep();
        }
        catch (OperationCanceledException)
        {
        }
    }
}
