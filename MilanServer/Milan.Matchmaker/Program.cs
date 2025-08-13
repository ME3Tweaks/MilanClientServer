using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Milan.Core.Options;
using Milan.Core.Server;
using Milan.Matchmaker;

HostApplicationBuilderSettings builderSettings = new()
{
    Args = args,
    ApplicationName = "Milan.Matchmaker",
    Configuration = new ConfigurationManager(),
    ContentRootPath = Directory.GetCurrentDirectory(),  // TODO: Pull content root from command line, if available.
};

builderSettings.Configuration.AddIniFile("matchmaker.ini", optional: true, reloadOnChange: true);
#if DEBUG
builderSettings.Configuration.AddIniFile("matchmaker.devel.ini", optional: true, reloadOnChange: true);
#endif // DEBUG
builderSettings.Configuration.AddEnvironmentVariables(prefix: "MILAN");

HostApplicationBuilder appBuilder = Host.CreateApplicationBuilder(builderSettings);

// Configuration options:
appBuilder.Services.Configure<ServerWorkerOptions>(appBuilder.Configuration.GetSection("ServerWorker"));

// Server initialization and loop:
appBuilder.Services.AddHostedService<ServerWorker>();

// Dependent services:
appBuilder.Services.AddSingleton<IHandlerRegistrant, MatchmakerHandlerRegistrant>();

appBuilder.Build().Run();
