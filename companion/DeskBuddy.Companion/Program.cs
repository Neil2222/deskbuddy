using DeskBuddy.Companion.Desktop;

namespace DeskBuddy.Companion;

internal static class Program
{
    [STAThread]
    private static void Main(string[] args)
    {
        var minimized = WindowPolicy.StartMinimized(args);
        // One companion at a time: a second one could not open the port anyway. A second launch brings the first
        // one's window up (also from the notification area); started by Windows at sign-in, it just leaves.
        using var instance = SingleInstance.TryAcquire();
        if (instance is null)
        {
            if (!minimized && !SingleInstance.SignalFirst())
            {
                MessageBox.Show("The DeskBuddy Companion is already running.", "DeskBuddy Companion");
            }
            return;
        }
        ApplicationConfiguration.Initialize();
        Application.Run(new MainForm(instance, minimized));
    }
}
