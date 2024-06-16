using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
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
using CommunityToolkit.Mvvm;
using CommunityToolkit.Mvvm.Input;
using Microsoft.Win32;
using NetworkProfiler.Models;


namespace NetworkProfiler;

/// <summary>
/// Interaction logic for MainWindow.xaml
/// </summary>
public partial class MainWindow : Window
{
    public string? LocalPath { get; private set; } = null;
    public ObservableCollection<LocalFileViewModel> LocalFiles { get; } = [];
    public bool LocalFilesAvailable { get; private set; } = false;
    public bool LocalFilesHint => !LocalFilesAvailable;

    public ICommand OpenLocalCommand { get; }
    public ICommand OpenFreeCommand { get; }
    public ICommand RefreshLocalsCommand { get; }

    public MainWindow()
    {
        InitializeComponent();
        DataContext = this;

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

        RebuildLocalFiles();
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

            LocalFiles.Clear();
            foreach (var localPath in localPathList.Select(selectPredicate).Where(filterPredicate))
            {
                Debug.WriteLine(localPath);
                FileInfo localInfo = new(System.IO.Path.Combine(LocalPath, localPath));

                LocalFile localFile = new();
                localFile.Filename = localPath;
                localFile.Size = localInfo.Length;
                localFile.Timestamp = localInfo.LastWriteTime;

                LocalFiles.Add(new LocalFileViewModel(localFile));
            }
        }
    }

    void LoadFile(string fullPath)
    {
        Debug.WriteLine($"Loading file '{fullPath}'");
        //Thread.Sleep(5000);
    }
}
