using NetworkProfiler.Core;
using NetworkProfiler.Dumper;


if (args.Length != 1)
{
    Console.Error.WriteLine($"must have one argument, had {args.Length}");
    Environment.Exit(1);
}

Console.WriteLine($"opening file at {args[0]}");
using var inputStream = File.OpenRead(args[0]);

NetworkCapture capture = new(inputStream);
LoggingVisitor visitor = new(capture);
capture.AcceptVisitor(visitor);
