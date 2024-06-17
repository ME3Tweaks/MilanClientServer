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

    #endregion

    public ICommand OpenLocalCommand { get; }
    public ICommand OpenFreeCommand { get; }
    public ICommand RefreshLocalsCommand { get; }
    public ICommand CloseCommand { get; }

    public MainWindow()
    {
        InitializeComponent();
        DataContext = this;

        originalWindowTitle = Title;
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

        #endregion
    }

    private void Window_Closing(object sender, CancelEventArgs e)
    {
        CloseFile();
    }

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

        Debug.WriteLine($"Loading file '{fullPath}'");
        SetWindowStatus($"Loading {fullPath}...");
        MainTabs.IsEnabled = false;

        Task.Run(() =>
        {
            bool bValidLoad = false;
            string loadError = string.Empty;

            var stopWatch = Stopwatch.StartNew();

            try
            {
                using var sourceStream = File.OpenRead(fullPath);
                using var networkCapture = new NetworkCapture(sourceStream);
                using var loadedCapture = new LoadedCapture(networkCapture);

                ActiveCapture = new LoadedCaptureViewModel(loadedCapture);
                stopWatch.Stop();
                bValidLoad = true;
            }
            catch (BadCaptureFormatException FormatException)
            {
                loadError = FormatException.Message;
            }

            // After loading is complete:

            Application.Current.Dispatcher.BeginInvoke(
                DispatcherPriority.Normal,
                () => {
                    MainTabs.IsEnabled = true;

                    if (bValidLoad)
                    {
                        var elapsed = stopWatch.Elapsed.TotalMilliseconds;
                        SetWindowTitle(fullPath);
                        SetWindowStatus($"Loaded capture {fileName} in {(int)elapsed} ms.");
                        MainTabs.SelectedIndex = 1;
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
    }

    #endregion
}
