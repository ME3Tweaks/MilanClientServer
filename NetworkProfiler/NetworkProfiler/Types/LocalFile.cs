using CommunityToolkit.Mvvm.ComponentModel;

namespace NetworkProfiler.Models;


/// <summary>
/// Description of a file auto-found at preferred game directory.
/// </summary>
public struct LocalFile
{
    public string Filename;
    public DateTime Timestamp;
    public long Size;
}

public class LocalFileViewModel(LocalFile model) : ObservableObject
{
    private LocalFile instance = model;

    public string Filename => instance.Filename;
    public DateTime Timestamp => instance.Timestamp;
    public long Size => instance.Size;
}
