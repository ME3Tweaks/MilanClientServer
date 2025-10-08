using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Milan.Core.Options;
using Milan.Core.Server;
using Milan.GameServer;
using Serilog;
using Serilog.Events;
using Serilog.Sinks.SystemConsole.Themes;
using System.Threading.Channels;


var cliResult = Cli.ParseChecked(args);
if (cliResult.Invoke() != 0)
{
    Environment.Exit(1);
    return;
}

var cliServerRole = cliResult.GetValue<EServerRole>("--role");
if (!Enum.IsDefined(cliServerRole))
{
    Console.Error.WriteLine("Failed to provide '--role' option.");
    Environment.Exit(1);
    return;
}

HostApplicationBuilderSettings builderSettings = new()
{
    Args = args,
    ApplicationName = "Milan.GameServer",
    Configuration = new ConfigurationManager(),
    ContentRootPath = cliResult.GetRequiredValue<DirectoryInfo>("--dir").FullName,
};

builderSettings.Configuration.AddIniFile("milan.ini", optional: true, reloadOnChange: true);
#if DEBUG
builderSettings.Configuration.AddIniFile("milan.devel.ini", optional: true, reloadOnChange: true);
#endif // DEBUG
builderSettings.Configuration.AddEnvironmentVariables(prefix: "MILAN");

HostApplicationBuilder appBuilder = Host.CreateApplicationBuilder(builderSettings);

#if DEBUG
LogEventLevel logMinimumLevel = LogEventLevel.Verbose;
#else // DEBUG
LogEventLevel logMinimumLevel = LogEventLevel.Information;
#endif // DEBUG

string logFilePath = cliServerRole switch
{
    EServerRole.Redirector => "./Milan-Redirector-.log",
    EServerRole.Matchmaking => "./Milan-Matchmaking-.log",
    _ => throw new NotImplementedException(),
};

appBuilder.Logging.ClearProviders();
appBuilder.Services.AddSerilog((services, loggerConfiguraton) => loggerConfiguraton
    .MinimumLevel.Is(logMinimumLevel)
    .WriteTo.Console(
        theme: SystemConsoleTheme.Literate,
        outputTemplate: "[{Timestamp:HH:mm:ss.fff} {Level:u3}] {Message:lj}{NewLine}{Exception}")
    .WriteTo.File(
        path: logFilePath,
        rollingInterval: RollingInterval.Day,
        outputTemplate: "[{Timestamp:HH:mm:ss.fff} {Level:u3}] {Message:lj}{NewLine}{Exception}"));

UnboundedChannelOptions clientStateChannelOptions = new() { SingleReader = true, SingleWriter = false };
Channel<ClientStateMessage> clientStateChannel = Channel.CreateUnbounded<ClientStateMessage>(clientStateChannelOptions);

// Configuration options:
appBuilder.Services.Configure<ServerWorkerOptions>(appBuilder.Configuration.GetSection("ServerWorker"));
appBuilder.Services.Configure<RedirectorOptions>(appBuilder.Configuration.GetSection("Redirector"));

// Dependent services:
appBuilder.Services.AddSingleton<IHandlerRegistrant, GameHandlerRegistrant>();
appBuilder.Services.AddSingleton<IHandlerRegistry, TableHandlerRegistry>();
appBuilder.Services.AddSingleton<IServerRoleProvider>(_ => new StaticRoleProvider(cliServerRole));
appBuilder.Services.AddSingleton<ChannelReader<ClientStateMessage>>(_ => clientStateChannel.Reader);
appBuilder.Services.AddSingleton<ChannelWriter<ClientStateMessage>>(_ => clientStateChannel.Writer);

// Server initialization and loops:
appBuilder.Services.AddHostedService<ServerAcceptWorker>();
appBuilder.Services.AddHostedService<ServerTickWorker>();

appBuilder.Build().Run();
