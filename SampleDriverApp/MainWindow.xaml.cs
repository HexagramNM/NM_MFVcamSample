
using System;
using System.Windows;
using System.Windows.Threading;
using SampleDriverApp;

namespace SampleGUI
{
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MainWindow : Window
    {
        private IntPtr _captureWindow;
        private int _frameCount = 0;
        private DispatcherTimer _timer;

        public MainWindow()
        {
            InitializeComponent();

            _timer = new DispatcherTimer();
            _timer.Interval = new TimeSpan(0, 0, 0, 0, 30);
            _timer.Tick += async (s, e) => {
                _frameCount++;
                if (_frameCount > 100)
                {
                    _frameCount = 0;
                }
                await Dispatcher.BeginInvoke(new Action(() =>
                {
                    DisplayBar.Value = _frameCount;
                }));
            };

            Loaded += (s, e) => { 
                SetUpVirtualCamera();
                _timer.Start();
            };

            Closing += (s, e) => { 
                OnWindowClosing();
                _timer?.Stop();
            };
        }

        private void SetUpVirtualCamera()
        {
            var helper = new System.Windows.Interop.WindowInteropHelper(this);
            _captureWindow = NM_CaptureWindow.createCaptureWindowObject(helper.Handle);
        }

        private void OnWindowClosing()
        { 
            NM_CaptureWindow.deleteCaptureWindowObject(_captureWindow);
        }
    }
}
