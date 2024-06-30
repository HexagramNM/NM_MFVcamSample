using System;
using System.Runtime.InteropServices;

namespace SampleDriverApp
{
    public static class NM_CaptureWindow
    {
        private static class NativeMethods
        {
            [DllImport("NM_CaptureWindow.dll")]
            internal static extern IntPtr createCaptureWindowObject(IntPtr hwnd);

            [DllImport("NM_CaptureWindow.dll")]
            internal static extern void deleteCaptureWindowObject(IntPtr captureWindowObj);
        }

        public static IntPtr createCaptureWindowObject(IntPtr hwnd)
        {
            return NativeMethods.createCaptureWindowObject(hwnd);
        }

        public static void deleteCaptureWindowObject(IntPtr captureWindowObj)
        {
            if (captureWindowObj == IntPtr.Zero)
            {
                return;
            }
            NativeMethods.deleteCaptureWindowObject(captureWindowObj);
        }
    }
}
