namespace DeskBuddy.Companion;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        // One companion at a time: a second one could not open the port anyway.
        using var single = new Mutex(true, @"Local\DeskBuddy.Companion", out var first);
        if (!first)
        {
            MessageBox.Show("The DeskBuddy Companion is already running.", "DeskBuddy Companion");
            return;
        }
        ApplicationConfiguration.Initialize();
        Application.Run(new MainForm());
    }
}
