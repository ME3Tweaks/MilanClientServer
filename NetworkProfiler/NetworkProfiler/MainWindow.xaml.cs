using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.CompilerServices;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;
using System.Windows.Threading;
using CommunityToolkit.Mvvm;
using CommunityToolkit.Mvvm.Input;
using Microsoft.Win32;
using NetworkProfiler.Core;
using NetworkProfiler.Models;
using NetworkProfiler.Types;
using ScottPlot;
using ScottPlot.AxisPanels;
using ScottPlot.Plottables;


namespace NetworkProfiler;

/// <summary>
/// Interaction logic for MainWindow.xaml
/// </summary>
public partial class MainWindow : Window, INotifyPropertyChanged
{
    readonly string originalWindowTitle = "";

    #region Observable properties.

    public string? LocalPath { get; private set; } = null;

    public ObservableCollection<LocalFileViewModel> LocalFiles { get; } = [];

    private bool _localFilesAvailable = false;
    public bool LocalFilesAvailable
    {
        get => _localFilesAvailable;
        set { _localFilesAvailable = value; OnPropertyChanged(); }
    }

    private LoadedCaptureViewModel? _activeCapture = null;
    public LoadedCaptureViewModel? ActiveCapture
    {
        get => _activeCapture;
        set { _activeCapture = value;  OnPropertyChanged(); }
    }

    private bool _frameFreezeEnabled = false;
    public bool FrameFreezeEnabled
    {
        get => _frameFreezeEnabled;
        set { _frameFreezeEnabled = value; OnPropertyChanged(); }
    }

    #endregion

    #region Observable chart checkbox properties.

    // All properties relevant to the main chart update
    // must start with "Chart" and end with "Count" or "Size".

    public bool _chartActorCount = false;
    public bool ChartActorCount
    {
        get => _chartActorCount;
        set { _chartActorCount = value; OnPropertyChanged(); }
    }

    public bool _chartPropertyCount = false;
    public bool ChartPropertyCount
    {
        get => _chartPropertyCount;
        set { _chartPropertyCount = value; OnPropertyChanged(); }
    }

    public bool _chartPropertySize = false;
    public bool ChartPropertySize
    {
        get => _chartPropertySize;
        set { _chartPropertySize = value; OnPropertyChanged(); }
    }

    public bool _chartRemoteCallCount = false;
    public bool ChartRemoteCallCount
    {
        get => _chartRemoteCallCount;
        set { _chartRemoteCallCount = value; OnPropertyChanged(); }
    }

    public bool _chartRemoteCallSize = false;
    public bool ChartRemoteCallSize
    {
        get => _chartRemoteCallSize;
        set { _chartRemoteCallSize = value; OnPropertyChanged(); }
    }

    public bool _chartSendBunchCount = false;
    public bool ChartSendBunchCount
    {
        get => _chartSendBunchCount;
        set { _chartSendBunchCount = value; OnPropertyChanged(); }
    }

    public bool _chartSendBunchSize = false;
    public bool ChartSendBunchSize
    {
        get => _chartSendBunchSize;
        set { _chartSendBunchSize = value; OnPropertyChanged(); }
    }

    public bool _chartSocketSendCount = false;
    public bool ChartSocketSendCount
    {
        get => _chartSocketSendCount;
        set { _chartSocketSendCount = value; OnPropertyChanged(); }
    }

    public bool _chartSocketSendSize = false;
    public bool ChartSocketSendSize
    {
        get => _chartSocketSendSize;
        set { _chartSocketSendSize = value; OnPropertyChanged(); }
    }

    public bool _chartSocketRecvCount = false;
    public bool ChartSocketRecvCount
    {
        get => _chartSocketRecvCount;
        set { _chartSocketRecvCount = value; OnPropertyChanged(); }
    }

    public bool _chartSocketRecvSize = false;
    public bool ChartSocketRecvSize
    {
        get => _chartSocketRecvSize;
        set { _chartSocketRecvSize = value; OnPropertyChanged(); }
    }

    public bool _chartControlMessageCount = false;
    public bool ChartControlMessageCount
    {
        get => _chartControlMessageCount;
        set { _chartControlMessageCount = value; OnPropertyChanged(); }
    }

    #endregion

    public ICommand OpenLocalCommand { get; }
    public ICommand OpenFreeCommand { get; }
    public ICommand RefreshLocalsCommand { get; }
    public ICommand CloseCommand { get; }
    public ICommand SyncAxesCommand { get; }
    public ICommand ResetChartCommand { get; }
    public ICommand ToggleFreezeFrameCommand { get; }

    public RelayCommand FirstFrameCommand { get; }
    public RelayCommand PreviousFrameCommand { get; }
    public RelayCommand NextFrameCommand { get; }

    public MainWindow()
    {
        InitializeComponent();
        DataContext = this;

        originalWindowTitle = Title;

        // Default plot set.
        ChartPropertySize = true;
        ChartRemoteCallSize = true;
        ChartSendBunchSize = true;

        RebuildLocalFiles();

        #region Command initialization.

        OpenLocalCommand = new RelayCommand<LocalFileViewModel>((localFile) =>
        {
            if (localFile is not null && LocalPath is not null)
                LoadFile(System.IO.Path.Combine(LocalPath, localFile.Filename));
        });

        OpenFreeCommand = new RelayCommand(() =>
        {
            var dialog = new OpenFileDialog()
            {
                DefaultExt = ".mnpsdb",
                Filter = "Milan Netprof Capture|netprof-*.mnpsdb|All files|*.*"
            };

            if (LocalPath is not null)
                dialog.DefaultDirectory = LocalPath;

            if (dialog.ShowDialog() is true)
            {
                LoadFile(dialog.FileName);
            }

        });

        RefreshLocalsCommand = new RelayCommand(RebuildLocalFiles);
        CloseCommand = new RelayCommand(CloseFile);

        SyncAxesCommand = new RelayCommand(SyncAxesChart);
        ResetChartCommand = new RelayCommand(() =>
        {
            ResetMainChart();
            UpdateMainChart();
            UpdateFrameSelection(ActiveCapture?.CurrentFrame?.Index ?? 0);
        });

        ToggleFreezeFrameCommand = new RelayCommand(() =>
        {
            FrameFreezeEnabled = !FrameFreezeEnabled;
        });

        FirstFrameCommand = new RelayCommand(
            () =>
            {
                ActiveCapture?.FirstFrame();
                UpdateFrameSelection(ActiveCapture!.CurrentFrame!.Index);
            },
            () => ActiveCapture?.IsFirstFrameAvailable() ?? false);
        PreviousFrameCommand = new RelayCommand(
            () =>
            {
                ActiveCapture?.PreviousFrame();
                UpdateFrameSelection(ActiveCapture!.CurrentFrame!.Index);
            },
            () => ActiveCapture?.IsPreviousFrameAvailable() ?? false);
        NextFrameCommand = new RelayCommand(
            () =>
            {
                ActiveCapture?.NextFrame();
                UpdateFrameSelection(ActiveCapture!.CurrentFrame!.Index);
            },
            () => ActiveCapture?.IsNextFrameAvailable() ?? false);

        #endregion
    }

    private void Window_Closing(object sender, CancelEventArgs e)
    {
        CloseFile();
    }


    void UpdateFrameSelection(int index)
    {
        if (frameLine is not null)
            MainPlot.Plot.Remove(frameLine);

        frameLine = MainPlot.Plot.Add.VerticalLine(index, 1, ScottPlot.Color.Gray(125));
        ActiveCapture?.SelectFrame(index);
        MainPlot.Refresh();

        FirstFrameCommand?.NotifyCanExecuteChanged();
        PreviousFrameCommand?.NotifyCanExecuteChanged();
        NextFrameCommand?.NotifyCanExecuteChanged();
    }


    #region Main chart logic.

    const double CHART_HORIZONTAL_LIMIT = 1000;
    const double CHART_LEFT_LIMIT = 400;
    const double CHART_RIGHT_LIMIT = 1200;
    const double CHART_DIVISOR = 50.0;

    IYAxis? rightAxis = null;
    VerticalLine? frameLine = null;

    void MainPlot_MouseDown(object sender, MouseButtonEventArgs e)
    {
        if (FrameFreezeEnabled)
            return;

        var bottomAxis = MainPlot.Plot.Axes.Bottom;
        var leftAxis = MainPlot.Plot.Axes.Left;

        var mousePosition = e.GetPosition(MainPlot);
        var mousePixel = new Pixel(mousePosition.X + 1, mousePosition.Y);

        var leftCoord = MainPlot.Plot.GetCoordinates(mousePixel, bottomAxis, leftAxis);
        var rightCoord = MainPlot.Plot.GetCoordinates(mousePixel, bottomAxis, rightAxis);

        Debug.Assert(leftCoord.X == rightCoord.X);
        Debug.WriteLine($"Left={leftCoord} Right={rightCoord}");

        int nearestFrameIndex = -1;
        foreach (var plottable in MainPlot.Plot.GetPlottables())
        {
            if (plottable is Signal signal)
            {
                nearestFrameIndex = signal.Data.GetIndex(leftCoord.X, clamp: true);
                break;
            }
        }

        if (nearestFrameIndex < 0)
            return;

        UpdateFrameSelection(nearestFrameIndex);
    }

    void UpdateMainChart()
    {
        if (ActiveCapture is null)
        {
            // We can't really draw anything now.
            return;
        }

        // This is effectively plot initialization.
        if (rightAxis is null)
        {
            MainPlot.Plot.Axes.FrameWidth(0);

            MainPlot.Plot.Axes.Left.FrameLineStyle.Width = 1;
            MainPlot.Plot.Axes.Bottom.FrameLineStyle.Width = 1;

            rightAxis = MainPlot.Plot.Axes.AddRightAxis();
            rightAxis.Min = MainPlot.Plot.Axes.Left.Min;
            rightAxis.FrameLineStyle.Width = 0;

            var bottomLimitX = ActiveCapture.FrameCount <= CHART_HORIZONTAL_LIMIT ? ActiveCapture.FrameCount : CHART_HORIZONTAL_LIMIT;
            MainPlot.Plot.Axes.SetLimitsX(-ActiveCapture.FrameCount / (2*CHART_DIVISOR), bottomLimitX * (1 + 1/CHART_DIVISOR));
            MainPlot.Plot.Axes.SetLimitsY(-(CHART_LEFT_LIMIT / CHART_DIVISOR), CHART_LEFT_LIMIT, MainPlot.Plot.Axes.Left);
            MainPlot.Plot.Axes.SetLimitsY(-(CHART_RIGHT_LIMIT / CHART_DIVISOR), CHART_RIGHT_LIMIT, rightAxis);
        }

        MainPlot.MouseDown -= MainPlot_MouseDown;
        MainPlot.MouseDown += MainPlot_MouseDown;

        Debug.WriteLine($"Updating chart...");
        var frameStats = ActiveCapture.FrameStats;

        MainPlot.Plot.Clear();

        // Reuse this variable to always add all the plottable items.
        // Disabling visibility on a persistent set of items ensures that the palette stays consistent.
        Signal? signal = null;

        signal = MainPlot.Plot.Add.Signal(frameStats.ActorCount);
        signal.IsVisible = ChartActorCount;
        signal.LegendText = "Actor count";

        signal = MainPlot.Plot.Add.Signal(frameStats.PropertyCount);
        signal.IsVisible = ChartPropertyCount;
        signal.LegendText = "Property count";

        signal = MainPlot.Plot.Add.Signal(frameStats.PropertySize);
        //signal.Axes.YAxis = rightAxis;
        //signal.LinePattern = LinePattern.Dotted;
        signal.IsVisible = ChartPropertySize;
        signal.LegendText = "Property size (bytes)";

        signal = MainPlot.Plot.Add.Signal(frameStats.RemoteCallCount);
        signal.IsVisible = ChartRemoteCallCount;
        signal.LegendText = "RPC count";

        signal = MainPlot.Plot.Add.Signal(frameStats.RemoteCallSize);
        //signal.Axes.YAxis = rightAxis;
        //signal.LinePattern = LinePattern.Dotted;
        signal.IsVisible = ChartRemoteCallSize;
        signal.LegendText = "RPC size (bytes)";

        signal = MainPlot.Plot.Add.Signal(frameStats.SendBunchCount);
        signal.IsVisible = ChartSendBunchCount;
        signal.LegendText = "SendBunch count";

        signal = MainPlot.Plot.Add.Signal(frameStats.SendBunchSize);
        signal.Axes.YAxis = rightAxis;
        signal.LinePattern = LinePattern.Dotted;
        signal.IsVisible = ChartSendBunchSize;
        signal.LegendText = "SendBunch size (bytes)";

        signal = MainPlot.Plot.Add.Signal(frameStats.SocketSendCount);
        signal.IsVisible = ChartSocketSendCount;
        signal.LegendText = "Socket send count";

        signal = MainPlot.Plot.Add.Signal(frameStats.SocketSendSize);
        signal.Axes.YAxis = rightAxis;
        signal.LinePattern = LinePattern.Dotted;
        signal.IsVisible = ChartSocketSendSize;
        signal.LegendText = "Socket send size (bytes)";

        signal = MainPlot.Plot.Add.Signal(frameStats.SocketRecvCount);
        signal.IsVisible = ChartSocketRecvCount;
        signal.LegendText = "Socket recv count";

        signal = MainPlot.Plot.Add.Signal(frameStats.SocketRecvSize);
        signal.Axes.YAxis = rightAxis;
        signal.LinePattern = LinePattern.Dotted;
        signal.IsVisible = ChartSocketRecvSize;
        signal.LegendText = "Socket recv size (bytes)";

        signal = MainPlot.Plot.Add.Signal(frameStats.ControlMessageCount);
        signal.IsVisible = ChartControlMessageCount;
        signal.LegendText = "Control message count";

        MainPlot.Refresh();
    }

    void SyncAxesChart()
    {
        if (rightAxis != null)
        {
            var bottom = MainPlot.Plot.Axes.Bottom;
            var left = MainPlot.Plot.Axes.Left;
            var right = rightAxis;

            var leftLimits = MainPlot.Plot.Axes.GetLimits(bottom, left);
            var rightLimits = MainPlot.Plot.Axes.GetLimits(bottom, right);

            MainPlot.Plot.Axes.SetLimitsY(-(leftLimits.Top / CHART_DIVISOR), leftLimits.Top, left);
            MainPlot.Plot.Axes.SetLimitsY(-(rightLimits.Top / CHART_DIVISOR), rightLimits.Top, right);

            MainPlot.Refresh();
            UpdateFrameSelection(ActiveCapture?.CurrentFrame?.Index ?? 0);
        }
    }

    void ResetMainChart()
    {
        rightAxis = null;
        MainPlot.Reset();
    }

    #endregion


    void RebuildLocalFiles()
    {
        LocalPath = Environment.GetEnvironmentVariable("MILAN_GAME_WIN64");
        LocalFilesAvailable = LocalPath is not null;

        if (LocalPath is null)
        {
            LocalFiles.Clear();
        }
        else
        {
            var localPathList = Directory.GetFiles(LocalPath).ToList();

            static string selectPredicate(string path) => System.IO.Path.GetFileName(path);
            static bool filterPredicate(string name) => name.StartsWith("netprof-") && name.EndsWith(".mnpsdb");


            List<LocalFileViewModel> tempLocalFiles = [];
            foreach (var localPath in localPathList.Select(selectPredicate).Where(filterPredicate))
            {
                Debug.WriteLine(localPath);
                FileInfo localInfo = new(System.IO.Path.Combine(LocalPath, localPath));

                LocalFile localFile = new();
                localFile.Filename = localPath;
                localFile.Size = localInfo.Length;
                localFile.Timestamp = localInfo.LastWriteTime;

                tempLocalFiles.Add(new LocalFileViewModel(localFile));
            }

            LocalFiles.Clear();
            tempLocalFiles.Reverse();
            tempLocalFiles.ForEach(LocalFiles.Add);
        }
    }

    void LoadFile(string fullPath)
    {
        var fileName = System.IO.Path.GetFileName(fullPath);

        SetWindowStatus($"Loading capture {fileName}...");
        MainTabs.IsEnabled = false;
        FrameFreezeEnabled = true;
        ResetMainChart();

        Task.Run(() =>
        {
            string? loadError = null;

            var stopWatch = Stopwatch.StartNew();

            try
            {
                var sourceBytes = File.ReadAllBytes(fullPath);
                var sourceStream = new MemoryStream(sourceBytes);
                var networkCapture = new NetworkCapture(sourceStream);
                var loadedCapture = new LoadedCapture(networkCapture);

                ActiveCapture = new LoadedCaptureViewModel(loadedCapture);

                try { UpdateMainChart(); } finally { }

                Debug.WriteLine($"Loaded capture with {ActiveCapture.RecordCount} record(s) and {ActiveCapture.FrameCount} frame(s).");
                stopWatch.Stop();
            }
            catch (BadCaptureFormatException FormatException)
            {
                // TODO: Consider some special formatting?
                loadError = FormatException.Message;
            }
#if !DEBUG
            catch (Exception Exception)
            {
                loadError = Exception.Message;
            }
#endif

            // After loading is complete:

            Application.Current.Dispatcher.BeginInvoke(
                DispatcherPriority.Normal,
                () => {
                    MainTabs.IsEnabled = true;

                    if (loadError is null)
                    {
                        var elapsed = stopWatch.Elapsed.TotalMilliseconds;
                        SetWindowTitle(fullPath);
                        SetWindowStatus($"Loaded capture {fileName} in {(int)elapsed} ms.");
                        MainTabs.SelectedIndex = 1;

                        UpdateFrameSelection(0);
                    }
                    else
                    {
                        ResetWindowTitle();
                        SetWindowStatus($"Failed to load capture {fileName} ({loadError}).");
                        MainTabs.SelectedIndex = 0;

                        MessageBox.Show($"Failed to load capture.\n{loadError}", "Load failure",
                            MessageBoxButton.OK, MessageBoxImage.Error);
                    }
                });
        });
    }

    void CloseFile()
    {
        ActiveCapture?.Dispose();
        ActiveCapture = null;

        ResetWindowTitle();
        ResetWindowStatus();
        MainTabs.SelectedIndex = 0;

        GC.Collect();
    }


    #region Window utilities.

    void SetWindowTitle(string filePath)
    {
        var fileName = System.IO.Path.GetFileName(filePath);
        Title = $"{originalWindowTitle} - {fileName}";
    }

    void ResetWindowTitle()
    {
        Title = originalWindowTitle;
    }

    void SetWindowStatus(string status)
    {
        CurrentStatus.Text = status;
    }

    void ResetWindowStatus()
    {
        CurrentStatus.Text = "OK";
    }

    #endregion

    #region INotifyPropertyChanged implementation.

    public event PropertyChangedEventHandler? PropertyChanged;
    protected void OnPropertyChanged([CallerMemberName] string? name = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));

        // This is a bit of a dirty hack, but should work well enough for now.
        if (name is not null && name.StartsWith("Chart") && (name.EndsWith("Count") || name.EndsWith("Size")))
        {
            UpdateMainChart();
            UpdateFrameSelection(ActiveCapture?.CurrentFrame?.Index ?? 0);
        }
    }

    #endregion
}
