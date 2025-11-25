using Microsoft.EntityFrameworkCore.Storage.ValueConversion;
namespace Milan.Core.Data;


/// <summary>
/// Talktable string reference.
/// </summary>
/// <param name="Value">String id.</param>
public record struct TlkStrRef(int Value);

public class TlkStrRefConverter : ValueConverter<TlkStrRef, int>
{
    public TlkStrRefConverter() : base(f => f.Value, t => new TlkStrRef(t)) {}
}
