using DeskBuddy.Companion.Desktop;
using DeskBuddy.Companion.Pdc;
using Xunit;

namespace DeskBuddy.Companion.Tests;

// Tray mode, Start with Windows and the single instance: the rules, against a fake registry and a temporary
// settings.json (never the real Run key or the user's settings).
public sealed class DesktopTests : IDisposable
{
    private const string Exe = @"C:\Tools\DeskBuddy\DeskBuddy.Companion.exe";
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "db-desktop-" + Guid.NewGuid().ToString("N"));

    public void Dispose()
    {
        if (Directory.Exists(_dir))
        {
            Directory.Delete(_dir, recursive: true);
        }
    }

    private sealed class FakeRegistry : IStartupRegistry
    {
        public readonly Dictionary<string, string> Values = [];
        public readonly HashSet<string> Disabled = [];
        public string? Read(string name) => Values.GetValueOrDefault(name);
        public void Write(string name, string command) { Values[name] = command; Disabled.Remove(name); }
        public void Delete(string name) { Values.Remove(name); Disabled.Remove(name); }
        public bool IsDisabledInWindows(string name) => Disabled.Contains(name);
    }

    private static Autostart Make(FakeRegistry registry, string exe = Exe, bool published = true, params string[] existing) =>
        new(registry, exe, published, path => existing.Length == 0 || existing.Contains(path, StringComparer.OrdinalIgnoreCase));

    [Theory]
    [InlineData(CloseReason.UserClosing, true, false, true)]   // the X in tray mode: hide
    [InlineData(CloseReason.UserClosing, false, false, false)] // no tray mode: quit, as before
    [InlineData(CloseReason.UserClosing, true, true, false)]   // Quit from the icon's menu
    [InlineData(CloseReason.WindowsShutDown, true, false, false)]
    [InlineData(CloseReason.TaskManagerClosing, true, false, false)]
    [InlineData(CloseReason.ApplicationExitCall, true, false, false)]
    public void Closing_the_window_hides_it_only_in_tray_mode_when_the_user_closed_it(CloseReason reason, bool tray, bool quitting, bool hide) =>
        Assert.Equal(hide, WindowPolicy.HideOnClose(reason, tray, quitting));

    [Fact]
    public void Only_the_startup_argument_starts_minimized()
    {
        Assert.True(WindowPolicy.StartMinimized(["--minimized"]));
        Assert.True(WindowPolicy.StartMinimized(["--MINIMIZED"]));
        Assert.False(WindowPolicy.StartMinimized([]));
        Assert.False(WindowPolicy.StartMinimized(["--minimize"]));
    }

    [Fact]
    public void Autostart_is_off_until_enabled_and_registers_this_copy_minimized()
    {
        var registry = new FakeRegistry();
        var autostart = Make(registry);
        Assert.Equal(AutostartState.Off, autostart.Status().State);
        Assert.Null(autostart.Problem(autostart.Status()));

        autostart.Enable();
        Assert.Equal($"\"{Exe}\" --minimized", registry.Values[Autostart.ValueName]);
        Assert.Equal(new AutostartStatus(AutostartState.On, Exe), autostart.Status());
        Assert.Null(autostart.Problem(autostart.Status()));

        autostart.Disable();
        Assert.Empty(registry.Values);
        Assert.Equal(AutostartState.Off, autostart.Status().State);
    }

    [Fact]
    public void Republishing_to_the_same_place_keeps_autostart_valid()
    {
        var registry = new FakeRegistry();
        Make(registry).Enable();
        Assert.Equal(AutostartState.On, Make(registry, @"c:\tools\deskbuddy\.\DeskBuddy.Companion.exe").Status().State);
    }

    [Fact]
    public void A_relocated_companion_reports_the_stale_entry_and_can_take_it_over()
    {
        var registry = new FakeRegistry();
        Make(registry, @"C:\Old\DeskBuddy.Companion.exe").Enable();
        var moved = Make(registry, Exe, true, Exe, @"C:\Old\DeskBuddy.Companion.exe");
        var status = moved.Status();
        Assert.Equal(new AutostartStatus(AutostartState.OtherCopy, @"C:\Old\DeskBuddy.Companion.exe"), status);
        Assert.Contains(@"C:\Old\DeskBuddy.Companion.exe", moved.Problem(status));
        Assert.Contains(Exe, moved.Problem(status));

        moved.Enable(); // "Use this copy"
        Assert.Equal(AutostartState.On, moved.Status().State);
    }

    [Fact]
    public void An_entry_for_a_file_that_is_gone_is_reported()
    {
        var registry = new FakeRegistry();
        registry.Values[Autostart.ValueName] = @"""D:\Gone\DeskBuddy.Companion.exe"" --minimized";
        var autostart = Make(registry, Exe, true, Exe);
        Assert.Equal(new AutostartStatus(AutostartState.Missing, @"D:\Gone\DeskBuddy.Companion.exe"), autostart.Status());
        Assert.Contains("no longer exists", autostart.Problem(autostart.Status()));
    }

    [Fact]
    public void An_entry_switched_off_in_Task_Manager_is_reported_and_switched_on_again_by_enabling()
    {
        var registry = new FakeRegistry();
        var autostart = Make(registry);
        autostart.Enable();
        registry.Disabled.Add(Autostart.ValueName);
        Assert.Equal(AutostartState.DisabledInWindows, autostart.Status().State);
        Assert.Contains("Task Manager", autostart.Problem(autostart.Status()));
        autostart.Enable();
        Assert.Equal(AutostartState.On, autostart.Status().State);
    }

    [Fact]
    public void A_development_build_cannot_register_but_can_still_remove_an_entry()
    {
        var registry = new FakeRegistry();
        var dev = Make(registry, published: false);
        Assert.Throws<InvalidOperationException>(dev.Enable);
        Assert.Empty(registry.Values);
        Assert.Contains("published", dev.Problem(dev.Status()));

        Make(registry).Enable();
        dev.Disable();
        Assert.Empty(registry.Values);
    }

    [Theory]
    [InlineData(@"""C:\Program Files\DeskBuddy\DeskBuddy.Companion.exe"" --minimized", @"C:\Program Files\DeskBuddy\DeskBuddy.Companion.exe")]
    [InlineData(@"C:\Tools\DeskBuddy.Companion.exe --minimized", @"C:\Tools\DeskBuddy.Companion.exe")]
    [InlineData(@"C:\Tools\DeskBuddy.Companion.exe", @"C:\Tools\DeskBuddy.Companion.exe")]
    public void Reads_the_executable_of_a_startup_command(string command, string exe) =>
        Assert.Equal(exe, Autostart.ExeOf(command));

    [Fact]
    public void Tray_settings_are_off_by_default_and_keep_the_other_settings()
    {
        Directory.CreateDirectory(_dir);
        var store = new PdcStore(_dir);
        Assert.Equal((false, false), store.LoadTray());

        File.WriteAllText(store.SettingsPath, """{"pdcEnabled": false, "clockFace": "classic", "lastDev": "441bf68d1178"}""");
        store.SaveTray(true, false);
        Assert.Equal((true, false), store.LoadTray());
        store.SaveTray(true, true);
        Assert.Equal((true, true), new PdcStore(_dir).LoadTray());
        Assert.Equal("classic", store.LoadClockFace()?.Face);
        Assert.Contains("\"lastDev\": \"441bf68d1178\"", File.ReadAllText(store.SettingsPath));
        Assert.False(store.LoadSettings().Enabled);
    }

    [Fact]
    public void The_data_folder_comes_from_DESKBUDDY_DATA_DIR_when_set()
    {
        var before = Environment.GetEnvironmentVariable(PdcStore.DataDirectoryVariable);
        try
        {
            Environment.SetEnvironmentVariable(PdcStore.DataDirectoryVariable, _dir);
            Assert.Equal(Path.GetFullPath(_dir), PdcStore.DefaultDirectory);
            Environment.SetEnvironmentVariable(PdcStore.DataDirectoryVariable, null);
            Assert.Null(PdcStore.DataDirectoryOverride);
            Assert.Equal(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "DeskBuddy"), PdcStore.DefaultDirectory);
        }
        finally
        {
            Environment.SetEnvironmentVariable(PdcStore.DataDirectoryVariable, before);
        }
    }

    [Fact]
    public void A_second_launch_reaches_the_first_instead_of_starting_twice()
    {
        var name = @"Local\DeskBuddy.Tests." + Guid.NewGuid().ToString("N");
        Assert.False(SingleInstance.SignalFirst(name)); // nobody to reach
        using var first = SingleInstance.TryAcquire(name);
        Assert.NotNull(first);
        using var shown = new ManualResetEventSlim();
        first.OnShowRequested(shown.Set);

        Assert.Null(SingleInstance.TryAcquire(name));
        Assert.True(SingleInstance.SignalFirst(name));
        Assert.True(shown.Wait(TimeSpan.FromSeconds(5)));
    }
}
