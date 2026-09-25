using System.Globalization;
using System.Net;
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
builder.Services.AddHostedService<ExpirySweeper>();
builder.Services.AddSingleton<DiscoveryEndpoint>();
builder.Services.AddHostedService<UdpDiscoveryService>();

builder.WebHost.ConfigureKestrel(kestrel =>
{
    kestrel.AddServerHeader = false;
    kestrel.Limits.MaxRequestBodySize = 16 * 1024;
});

builder.Services.ConfigureHttpJsonOptions(json =>
{
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
            forwarded.KnownProxies.Add(IPAddress.Parse(proxy));
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

        limiter.GlobalLimiter = PartitionedRateLimiter.Create<HttpContext, string>(http => PerAddress(http, o.RequestsPerMinute));
        limiter.AddPolicy(Api.CreatePolicy, http => PerAddress(http, o.CreatesPerMinute));
        limiter.AddPolicy(Api.RedeemPolicy, http => PerAddress(http, o.InviteRedeemsPerMinute));
        limiter.OnRejected = async (context, cancellationToken) =>
        {
            HttpResponse response = context.HttpContext.Response;
            response.StatusCode = StatusCodes.Status429TooManyRequests;
            if (context.Lease.TryGetMetadata(MetadataName.RetryAfter, out TimeSpan retryAfter))
                response.Headers.RetryAfter = ((int)Math.Ceiling(retryAfter.TotalSeconds)).ToString(CultureInfo.InvariantCulture);
            await response.WriteAsJsonAsync(new ServerError("rate_limited", "Too many requests from this network address."), cancellationToken);
        };
    });

WebApplication app = builder.Build();

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
        await http.Response.WriteAsJsonAsync(new ServerError("invalid_request", ex.Message));
    }
});
app.UseRateLimiter();
app.MapMilanApi();

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
