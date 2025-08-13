using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Milan.Core.Options;
using Milan.Core.Server;
using Milan.GameServer;


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

appBuilder.Logging
    .ClearProviders()
    .AddSimpleConsole(options =>
    {
        options.IncludeScopes = true;
        options.SingleLine = true;
        options.TimestampFormat = "HH:mm:ss.fff ";
    });

// Configuration options:
appBuilder.Services.Configure<ServerWorkerOptions>(appBuilder.Configuration.GetSection("ServerWorker"));

// Server initialization and loop:
appBuilder.Services.AddHostedService<ServerWorker>();

// Dependent services:
appBuilder.Services.AddSingleton<IHandlerRegistrant, GameHandlerRegistrant>();
appBuilder.Services.AddSingleton<IServerRoleProvider, GameServerRoleProvider>(_ => new(cliServerRole));

appBuilder.Build().Run();
