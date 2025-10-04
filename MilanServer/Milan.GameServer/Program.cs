using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Milan.Core.Network;
using Milan.Core.Options;
using Milan.Core.Server;
using Milan.GameServer;
using Serilog;
using Serilog.Sinks.SystemConsole.Themes;


var cliResult = Cli.ParseChecked(args);
if (cliResult.Invoke() != 0)
{
    Environment.Exit(1);
    return;
}

var cliServerRole = cliResult.GetValue<ServerRole>("--role");
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

appBuilder.Logging.ClearProviders();
appBuilder.Services.AddSerilog((services, loggerConfiguraton) => loggerConfiguraton
#if DEBUG
    .MinimumLevel.Verbose()
#else
    .MinimumLevel.Information()
#endif
    .WriteTo.Console(
        theme: SystemConsoleTheme.Literate,
        outputTemplate: "[{Timestamp:HH:mm:ss.fff} {Level:u3}] {Message:lj}{NewLine}{Exception}")
    .WriteTo.File(
        path: "./Milan.GameServer-.log",
        rollingInterval: RollingInterval.Day,
        outputTemplate: "[{Timestamp:HH:mm:ss.fff} {Level:u3}] {Message:lj}{NewLine}{Exception}"));

// Configuration options:
appBuilder.Services.Configure<ServerWorkerOptions>(appBuilder.Configuration.GetSection("ServerWorker"));
appBuilder.Services.Configure<RedirectorOptions>(appBuilder.Configuration.GetSection("Redirector"));

// Server initialization and loop:
appBuilder.Services.AddHostedService<ServerWorker>();

// Dependent services:
// TODO: See this should be a server-local class possibly deriving from Milan.Network.ClientTickHandler.
appBuilder.Services.AddSingleton<IAsyncTickHandler<ClientConnection>, ClientTickHandler>();
appBuilder.Services.AddSingleton<IHandlerRegistrant, GameHandlerRegistrant>();
appBuilder.Services.AddSingleton<IServerRoleProvider, GameServerRoleProvider>(_ => new(cliServerRole));

appBuilder.Build().Run();
