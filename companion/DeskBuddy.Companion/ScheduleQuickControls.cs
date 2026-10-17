using DeskBuddy.Companion.Schedule;
using DeskBuddy.Companion.UI;

namespace DeskBuddy.Companion;

/// <summary>
/// The schedule's state in the sidebar, under the connection status, on every page: a pill (Schedule Off / Schedule
/// Live / Paused until … / Paused until resumed) that opens the quick controls. It has no state of its own: it shows
/// the Schedule page's schedule and changes it only through that page (<see cref="SchedulePage.SetLive"/>,
/// <see cref="SchedulePage.Pause"/>, <see cref="SchedulePage.Resume"/>), which saves it and tells everyone, this pill too.
/// </summary>
public sealed class ScheduleQuickControls : IDisposable
{
    private readonly SchedulePage _page;
    private readonly ToolTip _tips;
    private readonly Func<bool> _connected;
    private readonly Action _openSchedule;
    private readonly TimeProvider _time;
    private readonly ContextMenuStrip _menu = new() { ShowImageMargin = false };

    public ScheduleQuickControls(Theme theme, ToolTip tips, SchedulePage page, Func<bool> connected, Action openSchedule, TimeProvider? time = null)
    {
        _page = page;
        _tips = tips;
        _connected = connected;
        _openSchedule = openSchedule;
        _time = time ?? TimeProvider.System;
        Pill = new FlatButton(theme, ButtonLook.Pill) { Height = 34, AccessibleName = "Schedule status" };
        _menu.Renderer = new DarkMenuRenderer();
        _menu.BackColor = Theme.Input;
        _menu.ForeColor = Theme.Text;
        _menu.Font = theme.Text10;
        Pill.Click += (_, _) => OpenMenu();
        UpdateStatus();
    }

    public FlatButton Pill { get; }

    /// <summary>The state shown (and the detail in the tooltip and menu).</summary>
    public ScheduleStatusInfo Status { get; private set; } = new(ScheduleState.Off, "", "");

    /// <summary>The quick controls that apply now.</summary>
    public IReadOnlyList<ScheduleCommand> Commands => ScheduleStatus.Commands(Status.State);

    /// <summary>Shows the schedule's effective state now (after a change, a tick, a connect or disconnect).</summary>
    public void UpdateStatus()
    {
        var status = ScheduleStatus.Of(_page.Schedule, _time.GetUtcNow(), _time.LocalTimeZone, _connected());
        if (status == Status)
        {
            return;
        }
        Status = status;
        Pill.Text = status.Text;
        Pill.DotColor = status.State switch
        {
            ScheduleState.Off => Theme.Faint,
            ScheduleState.Live => Theme.Green,
            _ => Theme.Amber,
        };
        Pill.AccessibleDescription = status.Detail;
        _tips.SetToolTip(Pill, status.Detail + " Click for the quick controls.");
        Pill.Invalidate();
    }

    /// <summary>Does what the menu item does, through the Schedule page (saved at once).</summary>
    public void Run(ScheduleCommand command)
    {
        switch (command.Kind)
        {
        case ScheduleCommandKind.TurnLiveOn:
            _page.SetLive(true);
            break;
        case ScheduleCommandKind.TurnOff:
            _page.SetLive(false);
            break;
        case ScheduleCommandKind.Resume:
            _page.Resume();
            break;
        case ScheduleCommandKind.OpenSchedule:
            _openSchedule();
            break;
        default:
            _page.Pause(ScheduleStatus.PauseUntil(command, _time.GetUtcNow(), _time.LocalTimeZone)!.Value);
            break;
        }
        UpdateStatus();
    }

    private void OpenMenu()
    {
        var menu = BuildMenu();
        menu.Show(Pill, new Point(0, -menu.GetPreferredSize(Size.Empty).Height)); // above: the pill sits at the bottom
        Menus.SelectFirst(menu); // the state's name is not an item: the keyboard starts on the first action
    }

    /// <summary>The quick controls for the state now: its name (and detail as the tooltip), then what applies.</summary>
    public ContextMenuStrip BuildMenu()
    {
        UpdateStatus();
        _menu.Items.Clear();
        _menu.Items.Add(new ToolStripMenuItem(Status.Text) { Enabled = false, ToolTipText = Status.Detail });
        _menu.Items.Add(new ToolStripSeparator());
        ToolStripMenuItem? away = null;
        foreach (var command in Commands)
        {
            var item = new ToolStripMenuItem(ScheduleStatus.Label(command), null, (_, _) => Run(command)) { ForeColor = Theme.Text };
            if (command.Kind == ScheduleCommandKind.Away)
            {
                if (away is null)
                {
                    away = new ToolStripMenuItem("Away for…") { ForeColor = Theme.Text };
                    away.DropDown.BackColor = Theme.Input;
                    away.DropDown.ForeColor = Theme.Text;
                    _menu.Items.Add(away);
                }
                away.DropDownItems.Add(item);
                continue;
            }
            if (command.Kind is ScheduleCommandKind.TurnOff or ScheduleCommandKind.OpenSchedule)
            {
                _menu.Items.Add(new ToolStripSeparator());
            }
            _menu.Items.Add(item);
        }
        return _menu;
    }

    public void Dispose() => _menu.Dispose();
}
