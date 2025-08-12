using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using System.Collections.Immutable;
using System.Text;

namespace Milan.Generators;

[Generator(LanguageNames.CSharp)]
public class ComponentGenerator : IIncrementalGenerator
{
    public void Initialize(IncrementalGeneratorInitializationContext context)
    {
        context.RegisterPostInitializationOutput(ctx => ctx.AddSource("ComponentAttribute.g.cs", $@"
namespace Milan.Generators;

/// <summary>
/// Auto-generates some code for an instanced server component.
/// </summary>
[System.AttributeUsage(System.AttributeTargets.Class)]
public class ComponentAttribute : System.Attribute
{{
    public required byte Id {{ get; init; }}
}}
"));

        context.RegisterPostInitializationOutput(ctx => ctx.AddSource("HandlerAttributes.g.cs", $@"
namespace Milan.Generators;

/// <summary>
/// Marker for component methods which serve as notification handlers.
/// </summary>
[System.AttributeUsage(System.AttributeTargets.Method)]
public class NotificationHandlerAttribute : System.Attribute
{{
    public required byte Id {{ get; init; }}
}}

/// <summary>
/// Marker for component methods which serve as request handlers.
/// </summary>
[System.AttributeUsage(System.AttributeTargets.Method)]
public class RequestHandlerAttribute : System.Attribute
{{
    public required byte Id {{ get; init; }}
}}

/// <summary>
/// Marker for component methods which serve as response handlers.
/// </summary>
[System.AttributeUsage(System.AttributeTargets.Method)]
public class ResponseHandlerAttribute : System.Attribute
{{
    public required byte Id {{ get; init; }}
}}

"));

        var componentClasses = context.SyntaxProvider
            .ForAttributeWithMetadataName("Milan.Generators.ComponentAttribute",
                predicate: static (node, _) => node is ClassDeclarationSyntax,
                transform: static (context, _) => new ComponentClassInfo(ref context)
            )
            .Combine(context.CompilationProvider)
            .WithComparer(new ClassCompilationComparer());

        context.RegisterSourceOutput(componentClasses, static (context, pair) =>
        {
            var (component, compilation) = pair;
            var source = new StringBuilder();

            List<(string, string)> notificationHandlers = [];
            List<(string, string)> requestHandlers = [];
            List<(string, string)> responseHandlers = [];

            foreach (var notificationHandlerName in component.NotificationHandlers)
            {
                foreach (var methodSymbol in compilation.GetSymbolsWithName(notificationHandlerName, SymbolFilter.Member))
                {
                    if (methodSymbol is IMethodSymbol method)
                    {
                        var attribute = method.GetAttributes().Single(a => a.AttributeClass?.Name == "NotificationHandlerAttribute");
                        var command = Helpers.GetAttrNamedArgument(attribute, "Id");
                        notificationHandlers.Add((notificationHandlerName, command));
                    }
                }
            }

            foreach (var requestHandlerName in component.RequestHandlers)
            {
                foreach (var methodSymbol in compilation.GetSymbolsWithName(requestHandlerName, SymbolFilter.Member))
                {
                    if (methodSymbol is IMethodSymbol method)
                    {
                        var attribute = method.GetAttributes().Single(a => a.AttributeClass?.Name == "RequestHandlerAttribute");
                        var command = Helpers.GetAttrNamedArgument(attribute, "Id");
                        requestHandlers.Add((requestHandlerName, command));
                    }
                }
            }

            foreach (var responseHandlerName in component.ResponseHandlers)
            {
                foreach (var methodSymbol in compilation.GetSymbolsWithName(responseHandlerName, SymbolFilter.Member))
                {
                    if (methodSymbol is IMethodSymbol method)
                    {
                        var attribute = method.GetAttributes().Single(a => a.AttributeClass?.Name == "ResponseHandlerAttribute");
                        var command = Helpers.GetAttrNamedArgument(attribute, "Id");
                        responseHandlers.Add((responseHandlerName, command));
                    }
                }
            }

            source.AppendLine($@"
namespace {component.Namespace};

public partial class {component.Class}
{{
    public const byte COMPONENT = {component.Id};

    public override byte Id => {component.Id};
    public override string Name => ""{component.Name}"";
");
            source.Append($@"
    public override void Register(Milan.Core.Network.IHandlerRegistry registry)
    {{
");
            foreach (var (name, id) in notificationHandlers)
                source.AppendLine($@"        registry.SetNotificationHandler((COMPONENT, {id}), {name});");
            foreach (var (name, id) in requestHandlers)
                source.AppendLine($@"        registry.SetRequestHandler((COMPONENT, {id}), {name});");
            foreach (var (name, id) in responseHandlers)
                source.AppendLine($@"        registry.SetResponseHandler((COMPONENT, {id}), {name});");
            source.Append($@"
    }}
");

            source.Append($@"
    public override IEnumerable<Milan.Core.Network.NotificationHandlerDelegate> NotificationHandlers
    {{
        get
        {{
");
            foreach (var (name, _) in notificationHandlers)
                source.AppendLine($@"            yield return {name};");
            source.Append($@"            yield break;
        }}
    }}
");

            source.Append($@"
    public override IEnumerable<Milan.Core.Network.RequestHandlerDelegate> RequestHandlers
    {{
        get
        {{
");
            foreach (var (name, _) in requestHandlers)
                source.AppendLine($@"            yield return {name};");
            source.Append($@"            yield break;
        }}
    }}
");

            source.Append($@"
    public override IEnumerable<Milan.Core.Network.ResponseHandlerDelegate> ResponseHandlers
    {{
        get
        {{
");
            foreach (var (name, _) in responseHandlers)
                source.AppendLine($@"            yield return {name};");
            source.Append($@"            yield break;
        }}
    }}
");

            source.AppendLine($@"}}");

            context.AddSource($"{component.Name}_{component.Id}.g.cs", source.ToString());
        });
    }
}

public record struct ComponentClassInfo
{
    public string Name;
    public string Class;
    public string Namespace;
    public byte Id;

    public ImmutableArray<string> NotificationHandlers;
    public ImmutableArray<string> RequestHandlers;
    public ImmutableArray<string> ResponseHandlers;

    public ComponentClassInfo(ref GeneratorAttributeSyntaxContext context)
    {
        AttributeData attribute = context.Attributes.Single();
        ClassDeclarationSyntax classNode = (context.TargetNode as ClassDeclarationSyntax)!;

        Name = classNode.Identifier.Text;
        Class = classNode.Identifier.Text;
        Namespace = Helpers.GetFullNamespace(classNode);
        Id = Helpers.GetAttrNamedArgument<byte>(attribute, "Id");

        if (Name.EndsWith("Component")) Name.Remove(Name.Length - "Component".Length);

        var notificationHandlersBuilder = ImmutableArray.CreateBuilder<string>();
        var requestHandlersBuilder = ImmutableArray.CreateBuilder<string>();
        var responseHandlersBuilder = ImmutableArray.CreateBuilder<string>();

        foreach (var methodSyntax in classNode.DescendantNodes().OfType<MethodDeclarationSyntax>())
        {
            var methodName = methodSyntax.Identifier.Text;

            foreach (var attributeSyntax in methodSyntax.DescendantNodes().OfType<AttributeSyntax>())
            {
                var attrName = attributeSyntax.Name.GetText().ToString();

                if (attrName.EndsWith("NotificationHandler")) notificationHandlersBuilder.Add(methodName);
                if (attrName.EndsWith("RequestHandler")) requestHandlersBuilder.Add(methodName);
                if (attrName.EndsWith("ResponseHandler")) responseHandlersBuilder.Add(methodName);
            }
        }

        NotificationHandlers = notificationHandlersBuilder.ToImmutable();
        RequestHandlers = requestHandlersBuilder.ToImmutable();
        ResponseHandlers = responseHandlersBuilder.ToImmutable();
    }
}

/// <seealso href="https://www.thinktecture.com/en/net/roslyn-source-generators-performance/"/>
public class ClassCompilationComparer
   : IEqualityComparer<(ComponentClassInfo info, Compilation compilation)>
{
    public bool Equals(
       (ComponentClassInfo info, Compilation compilation) x,
       (ComponentClassInfo info, Compilation compilation) y)
    {
        return x.info.Class.Equals(y.info.Class);
    }

    public int GetHashCode((ComponentClassInfo info, Compilation compilation) obj)
    {
        return obj.info.Class.GetHashCode();
    }
}
