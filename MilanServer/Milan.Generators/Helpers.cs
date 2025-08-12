using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace Milan.Generators;

/// <summary>
/// Static utilities.
/// </summary>
public static class Helpers
{
    #region Attribute argument retrieval.

    public static string GetAttrNamedArgument(AttributeData attribute, string name)
    {
        return attribute.NamedArguments.Single(pair => pair.Key == name).Value.Value!.ToString();
    }

    /// <summary>
    /// Retrieves value of a <c>name</c>d argument to an <c>attribute</c>.
    /// </summary>
    /// <param name="attribute">Parsed attribute.</param>
    /// <param name="name">Argument name.</param>
    /// <returns>Value of the named argument.</returns>
    public static T GetAttrNamedArgument<T>(AttributeData attribute, string name)
    {
        return (T)attribute.NamedArguments.Single(pair => pair.Key == name).Value.Value!;
    }

    /// <summary>
    /// Retrieves value of an optional <c>name</c>d argument to an <c>attribute</c>.
    /// </summary>
    /// <param name="attribute">Parsed attribute.</param>
    /// <param name="name">Argument name.</param>
    /// 
    public static bool TryGetAttrNamedArgument<T>(AttributeData attribute, string name, out T? value)
    {
        // Fuck whoever designed nullable types in .net, preferably with a rusty chainsaw.

        if (!attribute.NamedArguments.Any(pair => pair.Key == name))
        {
            value = default;
            return false;
        }

        value = attribute.NamedArguments.Where(pair => pair.Key == name)
            .Select(static pair => (T?)pair.Value.Value)
            .First();
        return true;
    }

    #endregion

    #region Namespace retrieval.

    // Implementation below is based on code from dotnet/runtime (MIT License):
    // https://github.com/dotnet/runtime/blob/9208fb63af0eb96aa7d3670b9b8f5cd2fb2097e7/src/libraries/Microsoft.Extensions.Logging.Abstractions/gen/LoggerMessageGenerator.Parser.cs#L499

    private static SyntaxNode? GetPotentialNamespaceParent(SyntaxNode syntaxNode)
    {
        SyntaxNode? potentialNamespaceParent = syntaxNode.Parent;
        while (potentialNamespaceParent is not null
            && potentialNamespaceParent is not NamespaceDeclarationSyntax
            && potentialNamespaceParent is not FileScopedNamespaceDeclarationSyntax)
        {
            potentialNamespaceParent = potentialNamespaceParent.Parent;
        }

        return potentialNamespaceParent;
    }

    private static string GetFullNamespace(BaseNamespaceDeclarationSyntax potentialNamespaceParent)
    {
        BaseNamespaceDeclarationSyntax? namespaceParent = potentialNamespaceParent;
        string @namespace = namespaceParent!.Name.ToString();

        while (true)
        {
            namespaceParent = namespaceParent.Parent as NamespaceDeclarationSyntax;
            if (namespaceParent is null) break;
            @namespace = $"{namespaceParent.Name}.{@namespace}";
        }

        return @namespace;
    }

    /// <summary>
    /// Builds a string for the full namespace of a type.
    /// </summary>
    /// <param name="syntaxNode">Syntax for a declaration of a class or struct or enum</param>
    /// <returns>Full namespace separated with dots.</returns>
    public static string GetFullNamespace(SyntaxNode syntaxNode)
    {
        SyntaxNode? potentialNamespaceParent = GetPotentialNamespaceParent(syntaxNode);
        if (potentialNamespaceParent is BaseNamespaceDeclarationSyntax namespaceParent)
            return GetFullNamespace(namespaceParent);
        return string.Empty;
    }

    /// <summary>
    /// Builds a string for the most-nested namespace name of a type.
    /// </summary>
    /// <param name="syntaxNode">Syntax for a declaration of a class or struct or enum</param>
    /// <returns>Single namespace name (without separators) or <c>null</c>.</returns>
    public static string? GetSingleNamespace(SyntaxNode syntaxNode)
    {
        SyntaxNode? potentialNamespaceParent = GetPotentialNamespaceParent(syntaxNode);
        if (potentialNamespaceParent is BaseNamespaceDeclarationSyntax namespaceParent)
            return namespaceParent.Name.ToString().Split(['.']).Last();
        return null;
    }

    /// <summary>
    /// Combines <see cref="GetFullNamespace(BaseTypeDeclarationSyntax)"/> and <see cref="GetSingleNamespace(BaseTypeDeclarationSyntax)"/>
    /// in a single call without iterating through parent namespaces twice.
    /// </summary>
    public static (string, string) GetFullAndSingleNamespace(SyntaxNode syntaxNode)
    {
        SyntaxNode? potentialNamespaceParent = GetPotentialNamespaceParent(syntaxNode);
        if (potentialNamespaceParent is BaseNamespaceDeclarationSyntax namespaceParent)
        {
            string fullNamespace = GetFullNamespace(namespaceParent);
            string singleNamespace = namespaceParent.Name.ToString().Split(['.']).Last();
            return (fullNamespace, singleNamespace);
        }
        throw new NotImplementedException();
    }

    #endregion
}
