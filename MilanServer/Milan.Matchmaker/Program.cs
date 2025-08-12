using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Milan.Core.Server;
using Milan.Matchmaker;

HostApplicationBuilder hostAppBuilder = Host.CreateApplicationBuilder(args);
hostAppBuilder.Services.AddHostedService<ServerWorker>();

// Services:
hostAppBuilder.Services.AddSingleton<IHandlerRegistrant, MatchmakerHandlerRegistrant>();

hostAppBuilder.Build().Run();
