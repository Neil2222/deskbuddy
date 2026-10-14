using System.IO.Ports;
using Microsoft.Win32;

namespace DeskBuddy.Companion.Device;

public sealed record PortInfo(string Name, string Description, bool IsDeskBuddy)
{
    public override string ToString() => $"{Name}  {Description}";
}

/// <summary>Lists serial ports and recognises DeskBuddy's CH343 USB-UART (USB 1A86:55D3) by its USB ID.</summary>
public static class PortFinder
{
    private const string Ch343 = @"VID_1A86&PID_55D3";

    public static IReadOnlyList<PortInfo> List()
    {
        var present = SerialPort.GetPortNames().Distinct(StringComparer.OrdinalIgnoreCase).ToList();
        var boards = Ch343Ports();
        return present
            .Select(p => boards.Contains(p)
                ? new PortInfo(p, "DeskBuddy (CH343 UART)", true)
                : new PortInfo(p, "other serial port", false))
            .OrderByDescending(p => p.IsDeskBuddy)
            .ThenBy(p => p.Name.Length).ThenBy(p => p.Name, StringComparer.OrdinalIgnoreCase)
            .ToList();
    }

    /// <summary>COM port names Windows has assigned to CH343 devices (the registry also keeps unplugged ones).</summary>
    private static HashSet<string> Ch343Ports()
    {
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        using var device = Registry.LocalMachine.OpenSubKey($@"SYSTEM\CurrentControlSet\Enum\USB\{Ch343}");
        if (device is null)
        {
            return names;
        }
        foreach (var instance in device.GetSubKeyNames())
        {
            using var parameters = device.OpenSubKey($@"{instance}\Device Parameters");
            if (parameters?.GetValue("PortName") is string name)
            {
                names.Add(name);
            }
        }
        return names;
    }
}
