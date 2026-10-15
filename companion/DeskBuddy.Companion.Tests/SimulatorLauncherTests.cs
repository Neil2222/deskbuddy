using DeskBuddy.Companion.Device;
using Xunit;

namespace DeskBuddy.Companion.Tests;

// The companion finds the repository it was built in by walking up from its exe (bin\Release\... or publish).
public class SimulatorLauncherTests
{
    [Fact]
    public void Finds_the_script_in_a_folder_above_and_only_in_a_DeskBuddy_checkout()
    {
        var root = Directory.CreateTempSubdirectory("deskbuddy-repo").FullName;
        try
        {
            var exeDir = Directory.CreateDirectory(Path.Combine(root, "companion", "publish")).FullName;
            Directory.CreateDirectory(Path.Combine(root, "tools"));
            var script = Path.Combine(root, "tools", "simulator.ps1");
            File.WriteAllText(script, "");

            // A tools\simulator.ps1 without the simulator's sources is someone else's folder.
            Assert.Null(SimulatorLauncher.FindScript(exeDir));

            Directory.CreateDirectory(Path.Combine(root, "firmware", "simulator"));
            Assert.Equal(script, SimulatorLauncher.FindScript(exeDir));
            Assert.Equal(script, SimulatorLauncher.FindScript(root));
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }
}
