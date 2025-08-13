using Milan.Core.Server;
using System.CommandLine;
using System.CommandLine.Help;
using System.CommandLine.Invocation;
using System.CommandLine.Parsing;

namespace Milan.GameServer;

public static class Cli
{
    static ParseResult InitParse(string[] args)
    {
        RootCommand rootCommand = new(description: "Project Milan Game Server");

        Option<ServerRole> serverRoleOption = new("--role", "-r")
        {
            Description = "Subset of components this server instance should provide.",
        };
        Option<DirectoryInfo> contentDirectoryOption = new("--dir", "-d")
        {
            Description = "Directory with configuration files, logs, etc.",
            DefaultValueFactory = _ => new DirectoryInfo(Directory.GetCurrentDirectory()),
        };

        rootCommand.Add(serverRoleOption);
        rootCommand.Add(contentDirectoryOption);

        for (int i = 0; i < rootCommand.Options.Count; i++)
        {
            if (rootCommand.Options[i] is HelpOption defaultHelpOption)
            {
                defaultHelpOption.Action = new CustomHelpAction(defaultHelpOption.Action as HelpAction);
                continue;
            }

            if (rootCommand.Options[i] is VersionOption defaultVersionOption)
            {
                defaultVersionOption.Action = new CustomVersionAction(defaultVersionOption.Action);
                continue;
            }
        }

        return rootCommand.Parse(args);
    }

    /// <summary>
    /// Wraps <see cref="Parse(string[])"/> and handles parsing errors.
    /// </summary>
    /// <param name="args">Command-line arguments.</param>
    /// <returns>Valid accessor for parsed arguments.</returns>
    public static ParseResult ParseChecked(string[] args)
    {
        ParseResult parseResult = InitParse(args);
        bool parseOkay = true;

        foreach (ParseError error in parseResult.Errors)
        {
            Console.Error.WriteLine(error.Message);
            parseOkay = false;
        }

        if (!parseOkay)
        {
            Console.Error.WriteLine("Application terminating...");
            Environment.Exit(1);
        }

        return parseResult;
    }
}

internal sealed class CustomHelpAction(HelpAction? action) : SynchronousCommandLineAction
{
    readonly HelpAction? _defaultAction = action;
    public override int Invoke(ParseResult parseResult)
    {
        int result = _defaultAction?.Invoke(parseResult) ?? 0;
        if (result == 0) Environment.Exit(0);
        return result;
    }
}

internal sealed class CustomVersionAction(CommandLineAction? action) : SynchronousCommandLineAction
{
    readonly CommandLineAction? _defaultAction = action;
    public override int Invoke(ParseResult parseResult)
    {
        int result = (_defaultAction as SynchronousCommandLineAction)?.Invoke(parseResult) ?? 0;
        if (result == 0) Environment.Exit(0);
        return result;
    }
}
