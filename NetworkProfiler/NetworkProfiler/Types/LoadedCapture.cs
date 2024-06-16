using CommunityToolkit.Mvvm.ComponentModel;
using NetworkProfiler.Analysis;
using NetworkProfiler.Core;
using System.Diagnostics;
using System.Text.RegularExpressions;

namespace NetworkProfiler.Types;


/// <summary>
/// Parsed network capture ready for providing general overview.
/// </summary>
public class LoadedCapture : IDisposable
{
    private NetworkCapture capture;

    public LoadedCapture(NetworkCapture source)
    {
        capture = source;

        //InitialAnalysisVisitor visitor = new() { CaptureRef = this };
        //capture.AcceptVisitor(visitor);
    }

    #region IDisposable implementation.

    private bool disposedValue;
    protected virtual void Dispose(bool disposing)
    {
        if (!disposedValue)
        {
            if (disposing)
            {
                capture.Dispose();
            }

            disposedValue = true;
        }
    }
    public void Dispose()
    {
        Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    #endregion
}

public class LoadedCaptureViewModel(LoadedCapture model) : ObservableObject, IDisposable
{
    private readonly LoadedCapture capture = model;

    #region IDisposable implementation.

    private bool disposedValue;
    protected virtual void Dispose(bool disposing)
    {
        if (!disposedValue)
        {
            if (disposing)
            {
                capture.Dispose();
            }

            disposedValue = true;
        }
    }
    public void Dispose()
    {
        Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    #endregion
}
