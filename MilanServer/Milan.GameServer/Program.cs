using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Milan.Core.Data;
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

builderSettings.Configuration.AddIniFile("milan.ini", optional: false, reloadOnChange: false);
#if DEBUG
builderSettings.Configuration.AddIniFile("milan.devel.ini", optional: true, reloadOnChange: false);
#endif // DEBUG
builderSettings.Configuration.AddEnvironmentVariables(prefix: "MILAN");

HostApplicationBuilder appBuilder = Host.CreateApplicationBuilder(builderSettings);

#if DEBUG
LogEventLevel logMinimumLevel = LogEventLevel.Verbose;
LogEventLevel efCoreMinimumLevel = LogEventLevel.Warning;
#else // DEBUG
LogEventLevel logMinimumLevel = LogEventLevel.Information;
LogEventLevel efCoreMinimumLevel = LogEventLevel.Warning;
#endif // DEBUG

if (cliResult.GetValue<bool>("--verbose"))
{
    logMinimumLevel = LogEventLevel.Verbose;
    efCoreMinimumLevel = LogEventLevel.Debug;
}

string logFilePath = cliServerRole switch
{
    EServerRole.Redirector => "./Milan-Redirector-.log",
    EServerRole.Matchmaking => "./Milan-Matchmaking-.log",
    _ => throw new NotImplementedException(),
};

appBuilder.Logging.ClearProviders();
appBuilder.Services.AddSerilog((services, loggerConfiguraton) => loggerConfiguraton
    .MinimumLevel.Is(logMinimumLevel)
    .MinimumLevel.Override("Microsoft.EntityFrameworkCore", efCoreMinimumLevel)
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
appBuilder.Services.Configure<CommonDatabaseOptions>(appBuilder.Configuration.GetSection("Common:Database"));
appBuilder.Services.Configure<ServerAcceptWorkerOptions>(appBuilder.Configuration.GetSection("GameServer:AcceptWorker"));
appBuilder.Services.Configure<RedirectorComponentOptions>(appBuilder.Configuration.GetSection("GameServer:RedirectorComponent"));

// Dependent services:
appBuilder.Services.AddSingleton<IHandlerRegistrant, RoleAwareHandlerRegistrant>();
appBuilder.Services.AddSingleton<IHandlerRegistry, TableHandlerRegistry>();
appBuilder.Services.AddSingleton<IServerRoleProvider>(_ => new StaticRoleProvider(cliServerRole));
appBuilder.Services.AddSingleton<ChannelReader<ClientStateMessage>>(_ => clientStateChannel.Reader);
appBuilder.Services.AddSingleton<ChannelWriter<ClientStateMessage>>(_ => clientStateChannel.Writer);
appBuilder.Services.AddPooledDbContextFactory<MilanDatabaseContext>((serviceProvider, optionsBuilder) =>
{
    // Temporary use of SQLite provider.
    // If for some reason we stick with SQLite,
    // the data source obviously needs to be configured
    // from milan.ini and overrides.
    optionsBuilder.UseSqlite("Data Source=./milan.db");
});

// Server initialization and loops:
appBuilder.Services.AddHostedService<ServerAcceptWorker>();
appBuilder.Services.AddHostedService<ServerTickWorker>();

appBuilder.Build().Run();
